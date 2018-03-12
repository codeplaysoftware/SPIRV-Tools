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

namespace spvtools {
namespace opt {

void LoopPeeling::DuplicateAndConnectLoop() {
  ir::CFG& cfg = *context_->cfg();
  analysis::DefUseManager* def_use_mgr = context_->get_def_use_mgr();

  assert(CanPeelLoop() && "Cannot peel loop!");

  LoopUtils::LoopCloningResult clone_results;

  std::vector<ir::BasicBlock*> ordered_loop_blocks;
  ir::BasicBlock* pre_header = loop_->GetOrCreatePreHeaderBlock();

  loop_->ComputeLoopStructuredOrder(&ordered_loop_blocks);

  cloned_loop_ = loop_utils_.CloneLoop(&clone_results, ordered_loop_blocks);

  // Add the basic block to the function.
  ir::Function::iterator it =
      loop_utils_.GetFunction()->FindBlock(pre_header->id());
  assert(it != loop_utils_.GetFunction()->end() &&
         "Pre-header not found in the function.");
  loop_utils_.GetFunction()->AddBasicBlocks(
      clone_results.cloned_bb_.begin(), clone_results.cloned_bb_.end(), ++it);

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

  // Set the merge block of the cloned loop as the original loop's header block.
  cloned_loop_->SetMergeBlock(loop_->GetHeaderBlock());

  // Patch the phi of the header.
  loop_->GetHeaderBlock()->ForEachPhiInst(
      [cloned_loop_exit, def_use_mgr, this](ir::Instruction* phi) {
        for (uint32_t i = 1; i < phi->NumInOperands(); i += 2) {
          if (!loop_->IsInsideLoop(phi->GetSingleWordInOperand(i))) {
            phi->SetInOperand(i, {cloned_loop_exit});
            def_use_mgr->AnalyzeInstUse(phi);
            return;
          }
        }
      });

  // Patch the iterating value initializers of the original loop using the
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
  loop_->GetHeaderBlock()->ForEachPhiInst(
      [&clone_results, def_use_mgr, this](ir::Instruction* phi) {
        for (uint32_t i = 0; i < phi->NumInOperands(); i += 2) {
          uint32_t pred_id = phi->GetSingleWordInOperand(i + 1);
          if (!loop_->IsInsideLoop(pred_id)) {
            phi->SetInOperand(
                i, {clone_results.value_map_.at(
                       exit_value_.at(phi->result_id())->result_id())});
            def_use_mgr->AnalyzeInstUse(phi);
          }
        }
      });
}

void LoopPeeling::InsertCanonicalInductionVariable(ir::Instruction* factor) {
  analysis::Type* factor_type =
      context_->get_type_mgr()->GetType(factor->type_id());
  assert(factor_type->kind() == analysis::Type::kInteger);
  analysis::Integer* int_type = factor_type->AsInteger();
  assert(int_type->width() == 32);

  InstructionBuilder builder(context_,
                             &*GetClonedLoop()->GetLatchBlock()->tail(),
                             ir::IRContext::kAnalysisDefUse |
                                 ir::IRContext::kAnalysisInstrToBlockMapping);
  // Create the increment.
  // Note that "factor->result_id()" is wrong, the proper id should the phi
  // value but we don't have it yet. The operand will be set latter, leave
  // "factor->result_id()" so that the id is a valid and so avoid any assert
  // that's could be added.
  ir::Instruction* iv_inc = builder.AddIAdd(
      factor->type_id(), factor->result_id(),
      builder.Add32BitConstantInteger<uint32_t>(1, int_type->IsSigned())
          ->result_id());

  builder.SetInsertPoint(&*GetClonedLoop()->GetHeaderBlock()->begin());

  canonical_induction_variable_ = builder.AddPhi(
      factor->type_id(),
      {builder.Add32BitConstantInteger<uint32_t>(0, int_type->IsSigned())
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
  for (uint32_t id : cfg.preds(GetOriginalLoop()->GetHeaderBlock()->id())) {
    if (!GetOriginalLoop()->IsInsideLoop(id)) {
      condition_block_id = id;
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

  uint32_t to_continue_block = exit_condition->GetSingleWordInOperand(
      exit_condition->GetSingleWordInOperand(1) ==
              GetOriginalLoop()->GetHeaderBlock()->id()
          ? 2
          : 1);
  exit_condition->SetInOperand(1, {to_continue_block});
  exit_condition->SetInOperand(2, {GetOriginalLoop()->GetHeaderBlock()->id()});

  // Update def/use manager.
  context_->get_def_use_mgr()->AnalyzeInstUse(exit_condition);
}

void LoopPeeling::PeelBefore(ir::Instruction* factor) {
  // Clone the loop and insert the cloned one before the loop.
  DuplicateAndConnectLoop();

  // Add a canonical induction variable "canonical_induction_variable_".
  InsertCanonicalInductionVariable(factor);

  // Change the exit condition of the cloned loop to be (exit when become
  // false):
  //  "canonical_induction_variable_" < "factor"
  FixExitCondition([factor, this](ir::BasicBlock* condition_block) {
    InstructionBuilder builder(context_, &*condition_block->tail(),
                               ir::IRContext::kAnalysisDefUse |
                                   ir::IRContext::kAnalysisInstrToBlockMapping);
    return builder
        .AddLessThan(canonical_induction_variable_->result_id(),
                     factor->result_id())
        ->result_id();
  });
}

void LoopPeeling::PeelAfter(ir::Instruction* factor,
                            ir::Instruction* iteration_count) {
  // Clone the loop and insert the cloned one before the loop.
  DuplicateAndConnectLoop();

  // Add a canonical induction variable "canonical_induction_variable_".
  InsertCanonicalInductionVariable(factor);

  // Change the exit condition of the cloned loop to be (exit when become
  // false):
  //  "canonical_induction_variable_" + "factor" < "iteration_count"
  FixExitCondition([factor, iteration_count,
                    this](ir::BasicBlock* condition_block) {
    InstructionBuilder builder(context_, &*condition_block->tail(),
                               ir::IRContext::kAnalysisDefUse |
                                   ir::IRContext::kAnalysisInstrToBlockMapping);
    // Build the following check: canonical_induction_variable_ + factor <
    // iteration_count
    return builder
        .AddLessThan(builder
                         .AddIAdd(canonical_induction_variable_->type_id(),
                                  canonical_induction_variable_->result_id(),
                                  factor->result_id())
                         ->result_id(),
                     iteration_count->result_id())
        ->result_id();
  });
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
    if (loop_size.roi_size_ * 2 < code_grow_threshold_) {
      continue;
    }
    LoopPeeling peeler(loop);

    if (!peeler.CanPeelLoop()) {
      continue;
    }

    std::unordered_map<ir::BasicBlock*, std::pair<PeelDirection, uint32_t> >
        CanPeelConditions;

    ir::BasicBlock* exit_block = loop->FindConditionBlock();

    ir::Instruction* exiting_iv = loop->FindConditionVariable(exit_block);
    size_t iterations = 0;
    if (!loop->FindNumberOfIterations(exiting_iv, &*exit_block->tail(),
                                      &iterations)) {
      continue;
    }
    if (!iterations) continue;

    for (uint32_t block : loop->GetBlocks()) {
      if (block != exit_block->id()) continue;
    }
  }

  return modified;
}

uint32_t LoopPeelingPass::LoopPeelingInfo::GetFirstLoopInvariantOperand(
    ir::Instruction* condition) const {
  for (uint32_t i = 0; i < condition->NumInOperands(); i++) {
    if (loop_->IsInsideLoop(
            context_->get_instr_block(condition->GetSingleWordInOperand(i)))) {
      return condition->GetSingleWordInOperand(i);
    }
  }

  return 0;
}

uint32_t LoopPeelingPass::LoopPeelingInfo::GetFirstNonLoopInvariantOperand(
    ir::Instruction* condition) const {
  for (uint32_t i = 0; i < condition->NumInOperands(); i++) {
    if (!loop_->IsInsideLoop(
            context_->get_instr_block(condition->GetSingleWordInOperand(i)))) {
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

LoopPeelingPass::LoopPeelingInfo::LoopPeelDirection
LoopPeelingPass::LoopPeelingInfo::GetPeelingInfo(ir::BasicBlock* bb) {
  if (bb->terminator()->opcode() != SpvOpBranchConditional) {
    return GetNoneDirection();
  }

  opt::analysis::DefUseManager* def_use_mgr = context_->get_def_use_mgr();

  ir::Instruction* condition =
      def_use_mgr->GetDef(bb->terminator()->GetSingleWordInOperand(0));

  if (!IsHandledCondition(condition->opcode())) {
    return GetNoneDirection();
  }

  uint32_t invariant_op = GetFirstLoopInvariantOperand(condition);
  if (!invariant_op) {
    // No loop invariant, it cannot be peeled by this pass.
    return GetNoneDirection();
  }
  uint32_t iv_op = GetFirstNonLoopInvariantOperand(condition);
  if (!iv_op) {
    // Seems to be a job for the unswitch pass.
    return GetNoneDirection();
  }

  SENode* invariant_scev =
      scev_analysis_->AnalyzeInstruction(def_use_mgr->GetDef(invariant_op));
  if (invariant_scev->GetType() == SENode::CanNotCompute) {
    // Can't make any conclusion.
    return GetNoneDirection();
  }

  SENode* iv_scev =
      scev_analysis_->AnalyzeInstruction(def_use_mgr->GetDef(iv_op));
  if (iv_scev->GetType() == SENode::CanNotCompute) {
    // Can't make any conclusion.
    return GetNoneDirection();
  }

  switch (condition->opcode()) {
    case SpvOpIEqual:
    case SpvOpUGreaterThan:
    case SpvOpSGreaterThan:
    case SpvOpUGreaterThanEqual:
    case SpvOpSGreaterThanEqual:
    case SpvOpULessThan:
    case SpvOpSLessThan:
    case SpvOpULessThanEqual:
    case SpvOpSLessThanEqual:
    default:
      return GetNoneDirection();
  }
}

bool LoopPeelingPass::LoopPeelingInfo::DivideNodes(SENode* lhs, SENode* rhs,
                                                   int64_t* result) const {
  if (SEConstantNode* rhs_cst = rhs->GetCoefficient()->AsSEConstantNode()) {
    int64_t rhs_val = rhs_cst->FoldToSingleValue();
    if (!rhs_val) return false;
    if (SEConstantNode* lhs_cst = lhs->GetCoefficient()->AsSEConstantNode()) {
      *result = lhs_cst->FoldToSingleValue() / rhs_val;
      return true;
    }
  }

  return false;
}

SENode* LoopPeelingPass::LoopPeelingInfo::GetLastIterationValue(
    SERecurrentNode* rec) const {
  return (SENodeDSL(rec->GetCoefficient(), scev_analysis_) *
          (loop_max_iterations_ - 1)) +
         rec->GetOffset();
}

SENode* LoopPeelingPass::LoopPeelingInfo::GetIterationValueAt(
    SERecurrentNode* rec, SENode* x) const {
  return (SENodeDSL(rec->GetCoefficient(), scev_analysis_) * x) +
         rec->GetOffset();
}

LoopPeelingPass::LoopPeelingInfo::LoopPeelDirection
LoopPeelingPass::LoopPeelingInfo::HandleEqual(SpvOp opcode, SENode* lhs,
                                              SENode* rhs) const {
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
      return LoopPeelDirection{LoopPeelingPass::PeelDirection::Before, 1};
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
      return LoopPeelDirection{LoopPeelingPass::PeelDirection::After, 1};
    }
  }

  return GetNoneDirection();
}

LoopPeelingPass::LoopPeelingInfo::LoopPeelDirection
LoopPeelingPass::LoopPeelingInfo::HandleLessThan(SpvOp opcode, bool handle_ge,
                                                 SENode* lhs,
                                                 SENode* rhs) const {
  // FIXME: check the current loop for scev nodes
  assert(rhs->AsSERecurrentNode() == nullptr);
  SENode* last_value = GetLastIterationValue(rec_node);
  // FIXME: Get iteration for cross point:
  // rec: a X + b
  // rhs: c
  // iteration: (c - b) / a
  SENode* cross_point /* wrong */ = GetIterationValueAt(rec_node, rhs);
  SENode* distance_to_end =
      cross_point - SENodeDSL(last_value->GetCoefficient(), scev_analysis_);

  bool has_value = false;

  size_t factor = 0;
  LoopPeelingPass::PeelDirection direction =
      LoopPeelingPass::PeelDirection::None;

  if (SEConstantNode* dist = distance_to_end->AsSEConstantNode()) {
    assert(dist->FoldToSingleValue() >= 0);
    factor = dist->FoldToSingleValue();
    direction = LoopPeelingPass::PeelDirection::After;
  }

  if (SEConstantNode* dist = cross_point->AsSEConstantNode()) {
    assert(dist->FoldToSingleValue() >= 0);
    factor = dist->FoldToSingleValue();
    direction = LoopPeelingPass::PeelDirection::Before;
  }

  if (has_value) {
    if (factor >)
      return LoopPeelDirection{LoopPeelingPass::PeelDirection::Before, factor};
  }

  return GetNoneDirection();
}

LoopPeelingPass::LoopPeelingInfo::LoopPeelDirection
LoopPeelingPass::LoopPeelingInfo::HandleGreaterThan(SpvOp opcode,
                                                    bool handle_ge, SENode* lhs,
                                                    SENode* rhs) const {
  // FIXME: check the current loop for scev nodes
  assert(rhs->AsSERecurrentNode() == nullptr);
  {
    // Try peel before opportunity.
    size_t factor = 0;
    bool has_value = false;
    if (SERecurrentNode* rec_node = lhs->AsSERecurrentNode()) {
      has_value =
          DivideNodes(SENodeDSL(rhs, scev_analysis_) - rec_node->GetOffset(),
                      rec_node->GetCoefficient(), &factor);
      if (has_value) {
        factor += 1;
        if (handle_ge) {
          factor += 1;
        }
      }
    }

    if (has_value)
      return LoopPeelDirection{LoopPeelingPass::PeelDirection::After, factor};
    else
      return GetNoneDirection();
  }

  return GetNoneDirection();
}

}  // namespace opt
}  // namespace spvtools
