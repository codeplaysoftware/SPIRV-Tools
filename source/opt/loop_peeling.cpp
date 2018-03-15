// Copyright (c) 2018 Google LLC.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ir_builder.h"
#include "ir_context.h"
#include "loop_descriptor.h"
#include "loop_peeling.h"
#include "loop_utils.h"
#include "scalar_analysis.h"
#include "scalar_analysis_nodes.h"

namespace spvtools {
namespace opt {
size_t LoopPeelingPass::code_grow_threshold_ = 1000;

void LoopPeeling::DuplicateAndConnectLoop(
    LoopUtils::LoopCloningResult* clone_results) {
  ir::CFG& cfg = *context_->cfg();
  analysis::DefUseManager* def_use_mgr = context_->get_def_use_mgr();

  assert(CanPeelLoop() && "Cannot peel loop!");

  std::vector<ir::BasicBlock*> ordered_loop_blocks;
  ir::BasicBlock* pre_header = loop_->GetOrCreatePreHeaderBlock();

  loop_->ComputeLoopStructuredOrder(&ordered_loop_blocks);

  cloned_loop_ = loop_utils_.CloneLoop(clone_results, ordered_loop_blocks);

  // Add the basic block to the function.
  ir::Function::iterator it =
      loop_utils_.GetFunction()->FindBlock(pre_header->id());
  assert(it != loop_utils_.GetFunction()->end() &&
         "Pre-header not found in the function.");
  loop_utils_.GetFunction()->AddBasicBlocks(
      clone_results->cloned_bb_.begin(), clone_results->cloned_bb_.end(), ++it);

  // Make the |loop_|'s preheader the |cloned_loop_| one.
  ir::BasicBlock* cloned_header = cloned_loop_->GetHeaderBlock();
  pre_header->ForEachSuccessorLabel(
      [cloned_header](uint32_t* succ) { *succ = cloned_header->id(); });

  // Update cfg.
  cfg.RemoveEdge(pre_header->id(), loop_->GetHeaderBlock()->id());
  cloned_loop_->SetPreHeaderBlock(pre_header);
  loop_->SetPreHeaderBlock(nullptr);

  // When cloning the loop, we didn't cloned the merge block, so currently
  // |cloned_loop_| shares the same block as |loop_|.
  // We mutate all branches from |cloned_loop_| block to |loop_|'s merge into a
  // branch to |loop_|'s header (so header will also be the merge of
  // |cloned_loop_|).
  uint32_t cloned_loop_exit = 0;
  for (uint32_t pred_id : cfg.preds(loop_->GetMergeBlock()->id())) {
    if (loop_->IsInsideLoop(pred_id)) continue;
    ir::BasicBlock* bb = cfg.block(pred_id);
    assert(cloned_loop_exit == 0 && "The loop has multiple exits.");
    cloned_loop_exit = bb->id();
    bb->ForEachSuccessorLabel([this](uint32_t* succ) {
      if (*succ == loop_->GetMergeBlock()->id())
        *succ = loop_->GetHeaderBlock()->id();
    });
  }

  // Update cfg.
  cfg.RemoveNonExistingEdges(loop_->GetMergeBlock()->id());
  cfg.AddEdge(cloned_loop_exit, loop_->GetHeaderBlock()->id());

  // Patch the phi of the original loop header:
  //  - Set the loop entry branch to come from the cloned loop exit block;
  //  - Set the initial value of the phi using the corresponding cloned loop
  //    exit values.
  //
  // We patch the iterating value initializers of the original loop using the
  // corresponding cloned loop exit values. Connects the cloned loop iterating
  // values to the original loop. This make sure that the initial value of the
  // second loop starts with the last value of the first loop.
  //
  // For example, loops like:
  //
  // int z = 0;
  // for (int i = 0; i++ < M; i += cst1) {
  //   if (cond)
  //     z += cst2;
  // }
  //
  // Will become:
  //
  // int z = 0;
  // int i = 0;
  // for (; i++ < M; i += cst1) {
  //   if (cond)
  //     z += cst2;
  // }
  // for (; i++ < M; i += cst1) {
  //   if (cond)
  //     z += cst2;
  // }
  loop_->GetHeaderBlock()->ForEachPhiInst([cloned_loop_exit, def_use_mgr,
                                           clone_results,
                                           this](ir::Instruction* phi) {
    for (uint32_t i = 0; i < phi->NumInOperands(); i += 2) {
      if (!loop_->IsInsideLoop(phi->GetSingleWordInOperand(i + 1))) {
        phi->SetInOperand(i,
                          {clone_results->value_map_.at(
                              exit_value_.at(phi->result_id())->result_id())});
        phi->SetInOperand(i + 1, {cloned_loop_exit});
        def_use_mgr->AnalyzeInstUse(phi);
        return;
      }
    }
  });

  // Force the creation of a new preheader for the original loop and set it as
  // the merge block for the cloned loop.
  cloned_loop_->SetMergeBlock(loop_->GetOrCreatePreHeaderBlock());
}

void LoopPeeling::InsertCanonicalInductionVariable() {
  InstructionBuilder builder(context_,
                             &*GetClonedLoop()->GetLatchBlock()->tail(),
                             ir::IRContext::kAnalysisDefUse |
                                 ir::IRContext::kAnalysisInstrToBlockMapping);
  ir::Instruction* uint_1_cst =
      builder.Add32BitConstantInteger<uint32_t>(1, int_type_->IsSigned());
  // Create the increment.
  // Note that we do "1 + 1" here, one of the operand should the phi
  // value but we don't have it yet. The operand will be set latter.
  ir::Instruction* iv_inc = builder.AddIAdd(
      uint_1_cst->type_id(), uint_1_cst->result_id(), uint_1_cst->result_id());

  builder.SetInsertPoint(&*GetClonedLoop()->GetHeaderBlock()->begin());

  canonical_induction_variable_ = builder.AddPhi(
      uint_1_cst->type_id(),
      {builder.Add32BitConstantInteger<uint32_t>(0, int_type_->IsSigned())
           ->result_id(),
       GetClonedLoop()->GetPreHeaderBlock()->id(), iv_inc->result_id(),
       GetClonedLoop()->GetLatchBlock()->id()});
  // Connect everything.
  iv_inc->SetInOperand(0, {canonical_induction_variable_->result_id()});

  // Update def/use manager.
  context_->get_def_use_mgr()->AnalyzeInstUse(iv_inc);

  // If do-while form, use the incremented value.
  if (do_while_form_) {
    canonical_induction_variable_ = iv_inc;
  }
}

void LoopPeeling::GetIteratorUpdateOperations(
    const ir::Loop* loop, ir::Instruction* iterator,
    std::unordered_set<ir::Instruction*>* operations) {
  opt::analysis::DefUseManager* def_use_mgr = context_->get_def_use_mgr();
  operations->insert(iterator);
  iterator->ForEachInId([def_use_mgr, loop, operations, this](uint32_t* id) {
    ir::Instruction* insn = def_use_mgr->GetDef(*id);
    if (insn->opcode() == SpvOpLabel) {
      return;
    }
    if (operations->count(insn)) {
      return;
    }
    if (!loop->IsInsideLoop(insn)) {
      return;
    }
    GetIteratorUpdateOperations(loop, insn, operations);
  });
}

void LoopPeeling::GetIteratingExitValues() {
  ir::CFG& cfg = *context_->cfg();

  loop_->GetHeaderBlock()->ForEachPhiInst([this](ir::Instruction* phi) {
    exit_value_[phi->result_id()] = nullptr;
  });

  if (!loop_->GetMergeBlock()) {
    return;
  }
  if (cfg.preds(loop_->GetMergeBlock()->id()).size() != 1) {
    return;
  }
  opt::analysis::DefUseManager* def_use_mgr = context_->get_def_use_mgr();

  uint32_t condition_block_id = cfg.preds(loop_->GetMergeBlock()->id())[0];

  auto& header_pred = cfg.preds(loop_->GetHeaderBlock()->id());
  do_while_form_ = std::find(header_pred.begin(), header_pred.end(),
                             condition_block_id) != header_pred.end();
  if (do_while_form_) {
    loop_->GetHeaderBlock()->ForEachPhiInst(
        [condition_block_id, def_use_mgr, this](ir::Instruction* phi) {
          std::unordered_set<ir::Instruction*> operations;

          for (uint32_t i = 0; i < phi->NumInOperands(); i += 2) {
            if (condition_block_id == phi->GetSingleWordInOperand(i + 1)) {
              exit_value_[phi->result_id()] =
                  def_use_mgr->GetDef(phi->GetSingleWordInOperand(i));
            }
          }
        });
  } else {
    DominatorTree* dom_tree =
        &context_->GetDominatorAnalysis(loop_utils_.GetFunction(), cfg)
             ->GetDomTree();
    ir::BasicBlock* condition_block = cfg.block(condition_block_id);

    loop_->GetHeaderBlock()->ForEachPhiInst(
        [dom_tree, condition_block, this](ir::Instruction* phi) {
          std::unordered_set<ir::Instruction*> operations;

          // Not the back-edge value, check if the phi instruction is the only
          // possible candidate.
          GetIteratorUpdateOperations(loop_, phi, &operations);

          for (ir::Instruction* insn : operations) {
            if (insn == phi) {
              continue;
            }
            if (dom_tree->Dominates(context_->get_instr_block(insn),
                                    condition_block)) {
              return;
            }
          }
          exit_value_[phi->result_id()] = phi;
        });
  }
}

void LoopPeeling::FixExitCondition(
    const std::function<uint32_t(ir::BasicBlock*)>& condition_builder) {
  ir::CFG& cfg = *context_->cfg();

  uint32_t condition_block_id = 0;
  for (uint32_t id : cfg.preds(GetClonedLoop()->GetMergeBlock()->id())) {
    if (GetClonedLoop()->IsInsideLoop(id)) {
      condition_block_id = id;
      break;
    }
  }
  assert(condition_block_id != 0 && "2nd loop in improperly connected");

  ir::BasicBlock* condition_block = cfg.block(condition_block_id);
  ir::Instruction* exit_condition = condition_block->terminator();
  assert(exit_condition->opcode() == SpvOpBranchConditional);
  InstructionBuilder builder(context_, &*condition_block->tail(),
                             ir::IRContext::kAnalysisDefUse |
                                 ir::IRContext::kAnalysisInstrToBlockMapping);

  exit_condition->SetInOperand(0, {condition_builder(condition_block)});

  uint32_t to_continue_block_idx =
      GetClonedLoop()->IsInsideLoop(exit_condition->GetSingleWordInOperand(1))
          ? 1
          : 2;
  exit_condition->SetInOperand(
      1, {exit_condition->GetSingleWordInOperand(to_continue_block_idx)});
  exit_condition->SetInOperand(2, {GetClonedLoop()->GetMergeBlock()->id()});

  // Update def/use manager.
  context_->get_def_use_mgr()->AnalyzeInstUse(exit_condition);
}

ir::BasicBlock* LoopPeeling::CreateBlockBefore(ir::BasicBlock* bb) {
  analysis::DefUseManager* def_use_mgr = context_->get_def_use_mgr();
  ir::CFG& cfg = *context_->cfg();
  assert(cfg.preds(bb->id()).size() == 1 && "More than one predecessor");

  std::unique_ptr<ir::BasicBlock> new_bb = MakeUnique<ir::BasicBlock>(
      std::unique_ptr<ir::Instruction>(new ir::Instruction(
          context_, SpvOpLabel, 0, context_->TakeNextId(), {})));
  new_bb->SetParent(loop_utils_.GetFunction());
  // Update the loop descriptor.
  ir::Loop* in_loop = (*loop_utils_.GetLoopDescriptor())[bb];
  if (in_loop) {
    in_loop->AddBasicBlock(new_bb.get());
    loop_utils_.GetLoopDescriptor()->SetBasicBlockToLoop(new_bb->id(), in_loop);
  }

  context_->set_instr_block(new_bb->GetLabelInst(), new_bb.get());
  def_use_mgr->AnalyzeInstDefUse(new_bb->GetLabelInst());

  ir::BasicBlock* bb_pred = cfg.block(cfg.preds(bb->id())[0]);
  bb_pred->tail()->ForEachInId([bb, &new_bb](uint32_t* id) {
    if (*id == bb->id()) {
      *id = new_bb->id();
    }
  });
  cfg.RemoveEdge(bb_pred->id(), bb->id());
  cfg.AddEdge(bb_pred->id(), new_bb->id());
  def_use_mgr->AnalyzeInstUse(&*bb_pred->tail());

  // Update the incoming branch.
  bb->ForEachPhiInst([&new_bb, def_use_mgr](ir::Instruction* phi) {
    phi->SetInOperand(1, {new_bb->id()});
    def_use_mgr->AnalyzeInstUse(phi);
  });
  InstructionBuilder(context_, new_bb.get(),
                     ir::IRContext::kAnalysisDefUse |
                         ir::IRContext::kAnalysisInstrToBlockMapping)
      .AddBranch(bb->id());
  cfg.AddEdge(new_bb->id(), bb->id());

  // Add the basic block to the function.
  ir::Function::iterator it = loop_utils_.GetFunction()->FindBlock(bb->id());
  assert(it != loop_utils_.GetFunction()->end() &&
         "Basic block not found in the function.");
  ir::BasicBlock* ret = new_bb.get();
  loop_utils_.GetFunction()->AddBasicBlock(std::move(new_bb), it);
  return ret;
}

ir::BasicBlock* LoopPeeling::ProtectLoop(ir::Loop* loop,
                                         ir::Instruction* condition,
                                         ir::BasicBlock* if_merge) {
  ir::BasicBlock* if_block = loop->GetOrCreatePreHeaderBlock();
  // Will no longer be a pre-header because of the if.
  loop->SetPreHeaderBlock(nullptr);
  // Kill the branch to the header.
  context_->KillInst(&*if_block->tail());

  InstructionBuilder builder(context_, if_block,
                             ir::IRContext::kAnalysisDefUse |
                                 ir::IRContext::kAnalysisInstrToBlockMapping);
  builder.AddConditionalBranch(condition->result_id(),
                               loop->GetHeaderBlock()->id(), if_merge->id(),
                               if_merge->id());

  return if_block;
}

void LoopPeeling::PeelBefore(uint32_t peel_factor) {
  assert(CanPeelLoop() && "Cannot peel loop");
  LoopUtils::LoopCloningResult clone_results;

  // Clone the loop and insert the cloned one before the loop.
  DuplicateAndConnectLoop(&clone_results);

  // Add a canonical induction variable "canonical_induction_variable_".
  InsertCanonicalInductionVariable();

  InstructionBuilder builder(context_,
                             &*cloned_loop_->GetPreHeaderBlock()->tail(),
                             ir::IRContext::kAnalysisDefUse |
                                 ir::IRContext::kAnalysisInstrToBlockMapping);
  ir::Instruction* factor =
      builder.Add32BitConstantInteger(peel_factor, int_type_->IsSigned());

  ir::Instruction* has_remaining_iteration = builder.AddLessThan(
      factor->result_id(), loop_iteration_count_->result_id());
  ir::Instruction* max_iteration = builder.AddSelect(
      factor->type_id(), has_remaining_iteration->result_id(),
      factor->result_id(), loop_iteration_count_->result_id());

  // Change the exit condition of the cloned loop to be (exit when become
  // false):
  //  "canonical_induction_variable_" < min("factor", "loop_iteration_count_")
  FixExitCondition([max_iteration, this](ir::BasicBlock* condition_block) {
    return InstructionBuilder(context_, &*condition_block->tail(),
                              ir::IRContext::kAnalysisDefUse |
                                  ir::IRContext::kAnalysisInstrToBlockMapping)
        .AddLessThan(canonical_induction_variable_->result_id(),
                     max_iteration->result_id())
        ->result_id();
  });

  // "Protect" the second loop: the second loop can only be executed if
  // |has_remaining_iteration| is true (i.e. factor < loop_iteration_count_).
  ir::BasicBlock* if_merge_block = loop_->GetMergeBlock();
  loop_->SetMergeBlock(CreateBlockBefore(loop_->GetMergeBlock()));
  // Prevent the second loop from being executed if we already executed all the
  // required iterations.
  ir::BasicBlock* if_block =
      ProtectLoop(loop_, has_remaining_iteration, if_merge_block);
  // Patch the phi of the merge block.
  if_merge_block->ForEachPhiInst(
      [&clone_results, if_block, this](ir::Instruction* phi) {
        // if_merge_block had previously only 1 predecessor.
        uint32_t incoming_value = phi->GetSingleWordInOperand(0);
        auto def_in_loop = clone_results.value_map_.find(incoming_value);
        if (def_in_loop != clone_results.value_map_.end())
          incoming_value = def_in_loop->second;
        phi->AddOperand(
            {spv_operand_type_t::SPV_OPERAND_TYPE_ID, {incoming_value}});
        phi->AddOperand(
            {spv_operand_type_t::SPV_OPERAND_TYPE_ID, {if_block->id()}});
        context_->get_def_use_mgr()->AnalyzeInstUse(phi);
      });

  context_->InvalidateAnalysesExceptFor(
      ir::IRContext::kAnalysisDefUse |
      ir::IRContext::kAnalysisInstrToBlockMapping |
      ir::IRContext::kAnalysisLoopAnalysis | ir::IRContext::kAnalysisCFG);
}

void LoopPeeling::PeelAfter(uint32_t peel_factor) {
  assert(CanPeelLoop() && "Cannot peel loop");
  LoopUtils::LoopCloningResult clone_results;

  // Clone the loop and insert the cloned one before the loop.
  DuplicateAndConnectLoop(&clone_results);

  // Add a canonical induction variable "canonical_induction_variable_".
  InsertCanonicalInductionVariable();

  InstructionBuilder builder(context_,
                             &*cloned_loop_->GetPreHeaderBlock()->tail(),
                             ir::IRContext::kAnalysisDefUse |
                                 ir::IRContext::kAnalysisInstrToBlockMapping);
  ir::Instruction* factor =
      builder.Add32BitConstantInteger(peel_factor, int_type_->IsSigned());

  ir::Instruction* has_remaining_iteration = builder.AddLessThan(
      factor->result_id(), loop_iteration_count_->result_id());

  // Change the exit condition of the cloned loop to be (exit when become
  // false):
  //  "canonical_induction_variable_" + "factor" < "loop_iteration_count_"
  FixExitCondition([factor, this](ir::BasicBlock* condition_block) {
    InstructionBuilder cond_builder(
        context_, &*condition_block->tail(),
        ir::IRContext::kAnalysisDefUse |
            ir::IRContext::kAnalysisInstrToBlockMapping);
    // Build the following check: canonical_induction_variable_ + factor <
    // iteration_count
    return cond_builder
        .AddLessThan(cond_builder
                         .AddIAdd(canonical_induction_variable_->type_id(),
                                  canonical_induction_variable_->result_id(),
                                  factor->result_id())
                         ->result_id(),
                     loop_iteration_count_->result_id())
        ->result_id();
  });

  // "Protect" the first loop: the first loop can only be executed if
  // factor < loop_iteration_count_.

  // The original loop's pre-header was the cloned loop merge block.
  GetClonedLoop()->SetMergeBlock(
      CreateBlockBefore(GetOriginalLoop()->GetPreHeaderBlock()));
  // Use the second loop preheader as if merge block.

  // Prevent the first loop if only the peeled loop needs it.
  ir::BasicBlock* if_block =
      ProtectLoop(cloned_loop_, has_remaining_iteration,
                  GetOriginalLoop()->GetPreHeaderBlock());

  // Patch the phi of the header block.
  // We added an if to enclose the first loop and because the phi node are
  // connected to the exit value of the first loop, the definition no longer
  // dominate the preheader.
  // We had to the preheader (our if merge block) the required phi instruction
  // and patch the header phi.
  GetOriginalLoop()->GetHeaderBlock()->ForEachPhiInst(
      [&clone_results, if_block, this](ir::Instruction* phi) {
        opt::analysis::DefUseManager* def_use_mgr = context_->get_def_use_mgr();

        auto find_value_idx = [](ir::Instruction* phi_inst, ir::Loop* loop) {
          uint32_t preheader_value_idx =
              !loop->IsInsideLoop(phi_inst->GetSingleWordInOperand(1)) ? 0 : 2;
          return preheader_value_idx;
        };

        ir::Instruction* cloned_phi =
            def_use_mgr->GetDef(clone_results.value_map_.at(phi->result_id()));
        uint32_t cloned_preheader_value = cloned_phi->GetSingleWordInOperand(
            find_value_idx(cloned_phi, GetClonedLoop()));

        ir::Instruction* new_phi =
            InstructionBuilder(context_,
                               &*GetOriginalLoop()->GetPreHeaderBlock()->tail(),
                               ir::IRContext::kAnalysisDefUse |
                                   ir::IRContext::kAnalysisInstrToBlockMapping)
                .AddPhi(phi->type_id(),
                        {phi->GetSingleWordInOperand(
                             find_value_idx(phi, GetOriginalLoop())),
                         GetClonedLoop()->GetMergeBlock()->id(),
                         cloned_preheader_value, if_block->id()});

        phi->SetInOperand(find_value_idx(phi, GetOriginalLoop()),
                          {new_phi->result_id()});
        def_use_mgr->AnalyzeInstUse(phi);
      });

  context_->InvalidateAnalysesExceptFor(
      ir::IRContext::kAnalysisDefUse |
      ir::IRContext::kAnalysisInstrToBlockMapping |
      ir::IRContext::kAnalysisLoopAnalysis | ir::IRContext::kAnalysisCFG);
}

Pass::Status LoopPeelingPass::Process(ir::IRContext* c) {
  InitializeProcessing(c);

  bool modified = false;
  ir::Module* module = c->module();

  // Process each function in the module
  for (ir::Function& f : *module) {
    modified |= ProcessFunction(&f);
  }

  return modified ? Status::SuccessWithChange : Status::SuccessWithoutChange;
}

bool LoopPeelingPass::ProcessFunction(ir::Function* f) {
  bool modified = false;
  ir::LoopDescriptor& loop_descriptor = *context()->GetLoopDescriptor(f);

  std::vector<ir::Loop*> to_process_loop;
  to_process_loop.reserve(loop_descriptor.NumLoops());
  for (ir::Loop& l : loop_descriptor) {
    to_process_loop.push_back(&l);
  }

  opt::ScalarEvolutionAnalysis scev_analysis(context());

  for (ir::Loop* loop : to_process_loop) {
    CodeMetrics loop_size;
    loop_size.Analyze(*loop);

    // This does not take into account branch elimination opportunities and the
    // unrolling.
    if (loop_size.roi_size_ * 2 > code_grow_threshold_) {
      continue;
    }

    ir::BasicBlock* exit_block = loop->FindConditionBlock();
    if (!exit_block) {
      continue;
    }

    ir::Instruction* exiting_iv = loop->FindConditionVariable(exit_block);
    if (!exiting_iv) {
      continue;
    }
    size_t iterations = 0;
    if (!loop->FindNumberOfIterations(exiting_iv, &*exit_block->tail(),
                                      &iterations)) {
      continue;
    }
    if (!iterations) continue;

    // FIXME
    LoopPeeling peeler(
        loop,
        InstructionBuilder(context(), loop->GetHeaderBlock(),
                           ir::IRContext::kAnalysisDefUse |
                               ir::IRContext::kAnalysisInstrToBlockMapping)
            .Add32BitConstantInteger<uint32_t>(
                static_cast<uint32_t>(iterations), false));

    if (!peeler.CanPeelLoop()) {
      continue;
    }

    // For each basic block in the loop, check if it is can be peeled. If it
    // can, get the direction (before/after) and by which factor.
    LoopPeelingInfo peel_info(loop, iterations, &scev_analysis);

    uint32_t peel_before_factor = 0;
    uint32_t peel_after_factor = 0;

    for (uint32_t block : loop->GetBlocks()) {
      if (block == exit_block->id()) continue;
      ir::BasicBlock* bb = cfg()->block(block);
      PeelDirection direction;
      uint32_t factor;
      std::tie(direction, factor) = peel_info.GetPeelingInfo(bb);

      if (direction == PeelDirection::kNone) {
        continue;
      }
      if (direction == PeelDirection::kBefore) {
        peel_before_factor = std::max(peel_before_factor, factor);
      } else {
        assert(direction == PeelDirection::kAfter);
        peel_after_factor = std::max(peel_after_factor, factor);
      }
    }

    // To build the constant, insert point will not be used.
    InstructionBuilder builder(context(), loop->GetHeaderBlock(),
                               ir::IRContext::kAnalysisDefUse |
                                   ir::IRContext::kAnalysisInstrToBlockMapping);
    if (peel_before_factor) {
      peeler.PeelBefore(peel_before_factor);
      modified = true;
    } else {
      if (peel_after_factor) {
        peeler.PeelAfter(peel_after_factor);
        modified = true;
      }
    }
  }

  return modified;
}

uint32_t LoopPeelingPass::LoopPeelingInfo::GetFirstLoopInvariantOperand(
    ir::Instruction* condition) const {
  for (uint32_t i = 0; i < condition->NumInOperands(); i++) {
    ir::BasicBlock* bb =
        context_->get_instr_block(condition->GetSingleWordInOperand(i));
    if (bb && loop_->IsInsideLoop(bb)) {
      return condition->GetSingleWordInOperand(i);
    }
  }

  return 0;
}

uint32_t LoopPeelingPass::LoopPeelingInfo::GetFirstNonLoopInvariantOperand(
    ir::Instruction* condition) const {
  for (uint32_t i = 0; i < condition->NumInOperands(); i++) {
    ir::BasicBlock* bb =
        context_->get_instr_block(condition->GetSingleWordInOperand(i));
    if (bb || !loop_->IsInsideLoop(bb)) {
      return condition->GetSingleWordInOperand(i);
    }
  }

  return 0;
}

static bool IsHandledCondition(SpvOp opcode) {
  switch (opcode) {
    case SpvOpIEqual:
    case SpvOpUGreaterThan:
    case SpvOpSGreaterThan:
    case SpvOpUGreaterThanEqual:
    case SpvOpSGreaterThanEqual:
    case SpvOpULessThan:
    case SpvOpSLessThan:
    case SpvOpULessThanEqual:
    case SpvOpSLessThanEqual:
      return true;
    default:
      return false;
  }
}

LoopPeelingPass::LoopPeelingInfo::Direction
LoopPeelingPass::LoopPeelingInfo::GetPeelingInfo(ir::BasicBlock* bb) const {
  if (bb->terminator()->opcode() != SpvOpBranchConditional) {
    return GetNoneDirection();
  }

  opt::analysis::DefUseManager* def_use_mgr = context_->get_def_use_mgr();

  ir::Instruction* condition =
      def_use_mgr->GetDef(bb->terminator()->GetSingleWordInOperand(0));

  if (!IsHandledCondition(condition->opcode())) {
    return GetNoneDirection();
  }

  if (!GetFirstLoopInvariantOperand(condition)) {
    // No loop invariant, it cannot be peeled by this pass.
    return GetNoneDirection();
  }
  if (!GetFirstNonLoopInvariantOperand(condition)) {
    // Seems to be a job for the unswitch pass.
    return GetNoneDirection();
  }

  // Left hand-side.
  SENode* lhs = scev_analysis_->AnalyzeInstruction(
      def_use_mgr->GetDef(condition->GetSingleWordInOperand(0)));
  if (lhs->GetType() == SENode::CanNotCompute) {
    // Can't make any conclusion.
    return GetNoneDirection();
  }

  // Right hand-side.
  SENode* rhs = scev_analysis_->AnalyzeInstruction(
      def_use_mgr->GetDef(condition->GetSingleWordInOperand(1)));
  if (rhs->GetType() == SENode::CanNotCompute) {
    // Can't make any conclusion.
    return GetNoneDirection();
  }

  // One side should be a recurrent expression over the current loop, the
  // other should be a constant over the loop.
  auto is_recurent_expr_over_current_loop =
      [this](const SERecurrentNode* rec_expr) -> bool {
    if (!rec_expr) return false;
    return std::any_of(
        rec_expr->graph_cbegin(), rec_expr->graph_cend(),
        [this](const SENode& node) {
          const SERecurrentNode* rec = node.AsSERecurrentNode();
          return loop_->IsInsideLoop(rec->GetLoop()->GetHeaderBlock());
        });
  };
  // Only take into account recurrent expression over the current loop.
  bool is_lhs_rec =
      is_recurent_expr_over_current_loop(lhs->AsSERecurrentNode());
  bool is_rhs_rec =
      is_recurent_expr_over_current_loop(rhs->AsSERecurrentNode());
  if ((is_lhs_rec && is_rhs_rec) || (!is_lhs_rec && !is_rhs_rec)) {
    return GetNoneDirection();
  }

  // If the op code is ==, then we try a peel before or after.
  // If opcode is not <, >, <= or >=, we bail out.
  //
  // For the remaining cases, we canonicalize the expression so that the
  // constant expression is on the left hand side and the recurring expression
  // is on the right hand side. If the we swap hand side, then < becomes >, <=
  // becomes >= etc.
  // If the opcode is <=, then we add 1 to the right hand side and do the peel
  // check on <.
  // If the opcode is >=, then we add 1 to the left hand side and do the peel
  // check on >.

  switch (condition->opcode()) {
    default:
      return GetNoneDirection();
    case SpvOpIEqual:
      return HandleEqual(lhs, rhs);
    case SpvOpUGreaterThan:
    case SpvOpSGreaterThan:
    case SpvOpULessThan:
    case SpvOpSLessThan:
      break;
    // We add one to transform >= into > and <= into <.
    case SpvOpUGreaterThanEqual:
    case SpvOpSGreaterThanEqual:
      rhs = SENodeDSL(rhs) + 1;
      break;
    case SpvOpULessThanEqual:
    case SpvOpSLessThanEqual:
      lhs = SENodeDSL(lhs) + 1;
      break;
  }

  // Force the left hand side to be the non recurring expression.
  if (is_lhs_rec) {
    std::swap(lhs, rhs);
    std::swap(is_lhs_rec, is_rhs_rec);
  }

  return HandleInequality(lhs, rhs->AsSERecurrentNode());
}

SENode* LoopPeelingPass::LoopPeelingInfo::GetLastIterationValue(
    SERecurrentNode* rec) const {
  return (SENodeDSL{rec->GetCoefficient()} * (loop_max_iterations_ - 1)) +
         rec->GetOffset();
}

LoopPeelingPass::LoopPeelingInfo::Direction
LoopPeelingPass::LoopPeelingInfo::HandleEqual(SENode* lhs, SENode* rhs) const {
  // FIXME: check the current loop for scev nodes
  {
    // Try peel before opportunity.
    SENode* lhs_cst = lhs;
    if (SERecurrentNode* rec_node = lhs->AsSERecurrentNode()) {
      lhs_cst = rec_node->GetOffset();
    }
    SENode* rhs_cst = rhs;
    if (SERecurrentNode* rec_node = rhs->AsSERecurrentNode()) {
      rhs_cst = rec_node->GetOffset();
    }

    if (lhs_cst == rhs_cst) {
      return Direction{LoopPeelingPass::PeelDirection::kBefore, 1};
    }
  }

  {
    // Try peel after opportunity.
    SENode* lhs_cst = lhs;
    if (SERecurrentNode* rec_node = lhs->AsSERecurrentNode()) {
      // rec_node(x) = a * x + b
      // assign to lhs: a * (loop_max_iterations_ - 1) + b
      lhs_cst = GetLastIterationValue(rec_node);
    }
    SENode* rhs_cst = rhs;
    if (SERecurrentNode* rec_node = rhs->AsSERecurrentNode()) {
      // rec_node(x) = a * x + b
      // assign to lhs: a * (loop_max_iterations_ - 1) + b
      rhs_cst = GetLastIterationValue(rec_node);
    }

    if (lhs_cst == rhs_cst) {
      return Direction{LoopPeelingPass::PeelDirection::kAfter, 1};
    }
  }

  return GetNoneDirection();
}

LoopPeelingPass::LoopPeelingInfo::Direction
LoopPeelingPass::LoopPeelingInfo::HandleInequality(SENodeDSL lhs,
                                                   SERecurrentNode* rhs) const {
  // Compute (cst - B) / A
  std::pair<SENodeDSL, int64_t> flip_iteration =
      (lhs - rhs->GetOffset()->AsSEConstantNode()) / rhs->GetCoefficient();
  if (!flip_iteration.first->AsSEConstantNode()) {
    return GetNoneDirection();
  }
  // note: !!flip_iteration.second normalize to 0/1 (via bool cast).
  int64_t iteration =
      std::abs(flip_iteration.first->AsSEConstantNode()->FoldToSingleValue()) +
      !!flip_iteration.second;
  if (loop_max_iterations_ <= static_cast<uint64_t>(iteration)) {
    // Always true or false within the loop bounds.
    return GetNoneDirection();
  }

  uint32_t factor = 0;
  /* sanity check: can we fit |iteration| in a uint32_t ? */
  if (static_cast<uint64_t>(iteration) < std::numeric_limits<uint32_t>::max()) {
    factor = static_cast<uint32_t>(iteration);
  }

  if (factor) {
    // Peel before if we are closer to the start, after if closer to the end.
    LoopPeelingPass::PeelDirection direction =
        loop_max_iterations_ / 2 > factor
            ? LoopPeelingPass::PeelDirection::kBefore
            : LoopPeelingPass::PeelDirection::kAfter;
    return Direction{direction, factor};
  }

  return GetNoneDirection();
}

}  // namespace opt
}  // namespace spvtools
