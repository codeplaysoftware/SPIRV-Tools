// Copyright (c) 2018 Google Inc.
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

#include "opt/loop_unroller.h"
#include <map>

namespace spvtools {
namespace opt {

void LoopUtils::InsertLoopClosedSSA() {
  opt::analysis::DefUseManager* def_use_manager =
      ir_context_->get_def_use_mgr();

  for (ir::BasicBlock& bb : function_) {
    for (ir::Instruction& inst : bb) {
      if (inst.opcode() == SpvOpLoopMerge) {
        std::unique_ptr<ir::Instruction> new_label{new ir::Instruction(
            ir_context_, SpvOp::SpvOpLabel, 0, ir_context_->TakeNextId(), {})};

        std::unique_ptr<ir::BasicBlock> new_exit_bb{
            new ir::BasicBlock(std::move(new_label))};

        uint32_t merge_block_id = inst.GetSingleWordOperand(0);
        ir::BasicBlock* current_loop_exit = ir_context_->get_instr_block(
            def_use_manager->GetDef(merge_block_id));

        ir::BasicBlock* new_exit_bb_soft_copy = new_exit_bb.get();
        uint32_t new_id = new_exit_bb->id();
        function_.AddBasicBlock(current_loop_exit, std::move(new_exit_bb));

        ir_context_->InvalidateAnalysesExceptFor(
            ir::IRContext::Analysis::kAnalysisNone);

        ir_context_->ReplaceAllUsesWith(merge_block_id, new_id);

        std::unique_ptr<ir::Instruction> new_branch{
            new ir::Instruction(ir_context_, SpvOp::SpvOpBranch, 0, 0,
                                {{SPV_OPERAND_TYPE_ID, {merge_block_id}}})};

        new_exit_bb_soft_copy->AddInstruction(std::move(new_branch));
      }
    }
  }

  ir_context_->InvalidateAnalysesExceptFor(
      ir::IRContext::Analysis::kAnalysisNone);
}

ir::Instruction* LoopUtils::RemapResultIDs(
    ir::Loop& loop, ir::BasicBlock* BB,
    std::map<uint32_t, uint32_t>& new_inst) const {
  // Label instructions aren't covered by normal traversal of the instructions.
  uint32_t new_label_id = ir_context_->TakeNextId();
  new_inst[BB->GetLabelInst()->result_id()] = new_label_id;
  BB->GetLabelInst()->SetResultId(new_label_id);

  ir::Instruction* new_induction = nullptr;
  for (ir::Instruction& inst : *BB) {
    uint32_t old_id = inst.result_id();

    if (old_id == 0) {
      continue;
    }

    inst.SetResultId(ir_context_->TakeNextId());
    new_inst[old_id] = inst.result_id();
    if (loop.GetInductionVariable()->result_id() == old_id) {
      new_induction = &inst;
    }
  }

  return new_induction;
}

bool LoopUtils::CanPerformPartialUnroll(ir::Loop& loop) {
  // Loop descriptor must be able to extract the induction variable.
  const ir::Instruction* induction = loop.GetInductionVariable();
  if (!induction || induction->opcode() != SpvOpPhi) return false;

  return true;
}

uint32_t LoopUtils::GetPhiVariableID(const ir::Instruction* phi,
                                     uint32_t label) const {
  for (uint32_t operand = 3; operand < phi->NumOperands(); operand += 2) {
    if (phi->GetSingleWordOperand(operand) == label) {
      return phi->GetSingleWordOperand(operand - 1);
    }
  }

  return 0;
}

void LoopUtils::RemapOperands(ir::BasicBlock* BB,
                              std::map<uint32_t, uint32_t>& new_inst) const {
  for (ir::Instruction& inst : *BB) {
    auto remap_operands_to_new_ids = [&new_inst](uint32_t* id) {
      auto itr = new_inst.find(*id);
      if (itr != new_inst.end()) {
        *id = itr->second;
      }
    };

    inst.ForEachInId(remap_operands_to_new_ids);
  }
}

void LoopUtils::FoldConditionBlock(ir::BasicBlock* condition_block,
                                   uint32_t operand_label) {
  ir::Instruction& old_branch = *condition_block->tail();

  uint32_t new_target = old_branch.GetSingleWordOperand(operand_label);
  std::unique_ptr<ir::Instruction> new_branch{
      new ir::Instruction(ir_context_, SpvOp::SpvOpBranch, 0, 0,
                          {{SPV_OPERAND_TYPE_ID, {new_target}}})};
  ir_context_->KillInst(&old_branch);
  condition_block->AddInstruction(std::move(new_branch));
}

ir::Loop LoopUtils::DuplicateLoop(ir::Loop& old_loop) {
  // Map of basic blocks ids
  std::map<uint32_t, ir::BasicBlock*> new_blocks;

  std::map<uint32_t, uint32_t> new_inst;
  const ir::Loop::BasicBlockOrderedListTy& basic_blocks =
      old_loop.GetOrderedBlocks();

  ir::Loop new_loop = old_loop;

  ir::Loop::BasicBlockOrderedListTy& new_block_order =
      new_loop.GetOrderedBlocksRef();
  new_block_order.clear();
  for (const ir::BasicBlock* itr : basic_blocks) {
    // Copy the loop basicblock.
    ir::BasicBlock* BB = itr->Clone(ir_context_);
    // Assign each result a new unique ID and keep a mapping of the old ids to
    // the new ones.
    ir::Instruction* new_induction = RemapResultIDs(old_loop, BB, new_inst);

    if (itr == old_loop.GetLatchBlock()) {
      new_loop.SetLatchBlock(BB);
    }

    if (itr == old_loop.GetHeaderBlock()) {
      new_loop.SetHeaderBlock(BB);
      new_loop.SetInductionVariable(new_induction);
    }

    if (itr == old_loop.GetConditionBlock()) {
      new_loop.SetConditionBlock(BB);
    }

    blocks_to_add_.push_back(std::unique_ptr<ir::BasicBlock>(BB));
    new_blocks[itr->id()] = BB;
    new_block_order.push_back(BB);
  }

  ir::BasicBlock* new_merge = old_loop.GetMergeBlock()->Clone(ir_context_);
  RemapResultIDs(old_loop, new_merge, new_inst);
  new_blocks[old_loop.GetMergeBlock()->id()] = new_merge;
  new_loop.SetMergeBlock(new_merge);

  for (auto& pair : new_blocks) {
    RemapOperands(pair.second, new_inst);
  }

  return new_loop;
}

void LoopUtils::CopyBody(ir::Loop& loop, int, bool eliminate_conditions) {
  // Map of basic blocks ids
  std::map<uint32_t, ir::BasicBlock*> new_blocks;

  std::map<uint32_t, uint32_t> new_inst;
  const ir::Loop::BasicBlockOrderedListTy& basic_blocks =
      loop.GetOrderedBlocks();

  uint32_t new_header_id = 0;
  uint32_t latch_block_id = 0;

  ir::Instruction* new_phi = nullptr;
  ir::BasicBlock* new_continue_block = nullptr;
  ir::BasicBlock* new_condition_block = nullptr;
  for (const ir::BasicBlock* itr : basic_blocks) {
    // Copy the loop basicblock.
    ir::BasicBlock* BB = itr->Clone(ir_context_);
    // Assign each result a new unique ID and keep a mapping of the old ids to
    // the new ones.
    ir::Instruction* phi = RemapResultIDs(loop, BB, new_inst);

    if (itr == loop.GetLatchBlock()) {
      ir::Instruction* merge_inst = loop.GetHeaderBlock()->GetLoopMergeInst();
      merge_inst->SetInOperand(1, {BB->id()});

      latch_block_id = BB->id();
      new_continue_block = BB;
    }

    if (itr == loop.GetHeaderBlock()) {
      new_header_id = BB->id();
      new_phi = phi;
    }

    if (itr == loop.GetConditionBlock()) {
      new_condition_block = BB;
    }

    ir::Instruction* merge_inst = BB->GetLoopMergeInst();
    if (merge_inst) ir_context_->KillInst(merge_inst);

    blocks_to_add_.push_back(std::unique_ptr<ir::BasicBlock>(BB));
    new_blocks[itr->id()] = BB;
  }

  // Set the previous continue block to point to the new header.
  ir::Instruction& continue_branch = *previous_continue_block_->tail();
  continue_branch.SetInOperand(0, {new_header_id});

  // As the algorithm copies the original loop blocks exactly, the tail of the
  // latch block on iterations after one will be a branch to the new header and
  // not the actual loop header.
  ir::Instruction& new_continue_branch = *new_continue_block->tail();
  new_continue_branch.SetInOperand(0, {loop.GetHeaderBlock()->id()});

  // Update references to the old phi node with the actual variable.
  const ir::Instruction* induction = loop.GetInductionVariable();
  new_inst[induction->result_id()] =
      GetPhiVariableID(previous_phi_, previous_continue_block_->id());

  if (eliminate_conditions &&
      previous_condition_block_ != loop.GetConditionBlock()) {
    FoldConditionBlock(previous_condition_block_, 1);
  }

  // Only reference to the header block is the backedge in the latch block,
  // don't change this.
  new_inst[loop.GetHeaderBlock()->id()] = loop.GetHeaderBlock()->id();

  for (auto& pair : new_blocks) {
    RemapOperands(pair.second, new_inst);
  }

  // Update the basic block and phi instruction trackers to reflect the new
  // loop.
  previous_condition_block_ = new_condition_block;
  previous_phi_ = new_phi;
  previous_continue_block_ = new_continue_block;
}

void LoopUtils::RemoveLoopFromFunction(ir::Loop& loop,
                                       ir::BasicBlock* preheader) {
  // Change preheader branch to branch to the merge block rather than the header
  // block.
  ir::Instruction& branch = *preheader->tail();

  branch.RemoveOperand(0);
  branch.AddOperand({SPV_OPERAND_TYPE_ID, {loop.GetMergeBlock()->id()}});
}

void LoopUtils::CloseUnrolledLoop(ir::Loop& loop) {
  // Remove the OpLoopMerge instruction from the function.
  ir::Instruction* merge_inst = loop.GetHeaderBlock()->GetLoopMergeInst();
  ir_context_->KillInst(merge_inst);

  // Remove the final backedge to the header and make it point instead to the
  // merge block.
  previous_continue_block_->tail()->SetInOperand(0,
                                                 {loop.GetMergeBlock()->id()});
}

void LoopUtils::PartiallyUnrollImpl(ir::Loop& loop, int factor) {
  PartiallyUnrollImpl(loop, factor, loop.GetInductionVariable(),
                      loop.GetLatchBlock(), loop.GetConditionBlock());
}

void LoopUtils::PartiallyUnrollImpl(ir::Loop& loop, int factor,
                                    ir::Instruction* induction,
                                    ir::BasicBlock* initial_continue_block,
                                    ir::BasicBlock* initial_condition) {
  previous_phi_ = induction;
  previous_continue_block_ = initial_continue_block;
  previous_condition_block_ = initial_condition;

  for (int i = 0; i < factor; ++i) {
    CopyBody(loop, i + 1, true);
  }

  // The first condition block is perserved until now so it can be copied.
  FoldConditionBlock(initial_condition, 1);

  uint32_t phi_index = 5;
  uint32_t phi_variable = previous_phi_->GetSingleWordOperand(phi_index - 1);
  uint32_t phi_label = previous_phi_->GetSingleWordOperand(phi_index);

  ir::Instruction* original_phi = induction;

  // SetInOperands are offset by two.
  // TODO: Work out why.
  original_phi->SetInOperand(phi_index - 3, {phi_variable});
  original_phi->SetInOperand(phi_index - 2, {phi_label});
}

bool LoopUtils::PartiallyUnrollUnevenFactor(ir::Loop& loop, int factor) {
  blocks_to_add_.clear();

  // Create a new merge block for the first loop.
  std::unique_ptr<ir::Instruction> new_label{new ir::Instruction(
      ir_context_, SpvOp::SpvOpLabel, 0, ir_context_->TakeNextId(), {})};
  std::unique_ptr<ir::BasicBlock> new_exit_bb{
      new ir::BasicBlock(std::move(new_label))};

  uint32_t new_merge_id = new_exit_bb->id();

  blocks_to_add_.push_back(std::move(new_exit_bb));

  ir::Loop new_loop = DuplicateLoop(loop);

  // Make the first loop branch to the second.
  std::unique_ptr<ir::Instruction> new_branch{new ir::Instruction(
      ir_context_, SpvOp::SpvOpBranch, 0, 0,
      {{SPV_OPERAND_TYPE_ID, {new_loop.GetHeaderBlock()->id()}}})};
  blocks_to_add_[0]->AddInstruction(std::move(new_branch));

  PartiallyUnrollImpl(new_loop, factor);

  // Add the merge block to the back of the binary.
  blocks_to_add_.push_back(
      std::unique_ptr<ir::BasicBlock>(new_loop.GetMergeBlock()));
  AddBlocksToFunction(loop.GetMergeBlock());

  ir_context_->InvalidateAnalysesExceptFor(
      ir::IRContext::Analysis::kAnalysisNone);

  ir_context_->ReplaceAllUsesWith(loop.GetMergeBlock()->id(), new_merge_id);
  return true;
}

bool LoopUtils::PartiallyUnroll(ir::Loop& loop, int factor) {
  if (factor == 0) return false;

  if (!CanPerformPartialUnroll(loop)) return false;

  // If the unrolling factor is larger than or the same size as the loop just
  // fully unroll the loop.
  if (factor >= loop.NumIterations() - 1) {
    return FullyUnroll(loop);
  }

  // If the loop unrolling factor is an uneven number of iterations we need to
  // let run the loop for the uneven part then let it branch into the unrolled
  // remaining part.
  if (factor % loop.NumIterations() != 0) {
    return PartiallyUnrollUnevenFactor(loop, factor);
  }

  blocks_to_add_.clear();

  PartiallyUnrollImpl(loop, factor);

  AddBlocksToFunction(loop.GetMergeBlock());

  return true;
}

void LoopUtils::AddBlocksToFunction(const ir::BasicBlock* insert_point) {
  for (auto basic_block_iterator = function_.begin();
       basic_block_iterator != function_.end(); ++basic_block_iterator) {
    if (basic_block_iterator->id() == insert_point->id()) {
      basic_block_iterator.InsertBefore(&blocks_to_add_);
      break;
    }
  }
}

bool LoopUtils::FullyUnroll(ir::Loop& loop) {
  blocks_to_add_.clear();

  PartiallyUnrollImpl(loop, loop.NumIterations() - 1);

  // When fully unrolling we can eliminate the last condition block.
  FoldConditionBlock(previous_condition_block_, 1);

  // Delete the OpLoopMerge and remove the backedge to the header.
  CloseUnrolledLoop(loop);

  // Add the blocks to the function.
  AddBlocksToFunction(loop.GetMergeBlock());

  // Invalidate all analyses.
  ir_context_->InvalidateAnalysesExceptFor(
      ir::IRContext::Analysis::kAnalysisNone);
  return true;
}

Pass::Status LoopUnroller::Process(ir::IRContext* c) {
  context_ = c;
  bool changed = false;
  for (ir::Function& f : *c->module()) {
    LoopUtils loop_utils{f, c};

    for (auto& loop : loop_utils.GetLoopDescriptor()) {
      if (!loop_utils.CanPerformPartialUnroll(loop)) continue;

      loop_utils.PartiallyUnroll(loop, 3);
      // loop_utils.FullyUnroll(loop);
      changed = true;
    }
  }

  if (changed) return Pass::Status::SuccessWithChange;
  return Pass::Status::SuccessWithoutChange;
}

}  // namespace opt
}  // namespace spvtools
