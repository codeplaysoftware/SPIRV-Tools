// Copyright (c) 2017 Google Inc.
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
        std::unique_ptr<ir::Instruction> new_label{
            new ir::Instruction(ir_context_, SpvOp::SpvOpLabel, 0,
                                ir_context_->TakeNextUniqueId(), {})};

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
  uint32_t new_label_id = ir_context_->TakeNextUniqueId();
  new_inst[BB->GetLabelInst()->result_id()] = new_label_id;
  BB->GetLabelInst()->SetResultId(new_label_id);

  ir::Instruction* new_induction = nullptr;
  for (ir::Instruction& inst : *BB) {
    uint32_t old_id = inst.result_id();

    if (old_id == 0) {
      continue;
    }

    inst.SetResultId(ir_context_->TakeNextUniqueId());
    new_inst[old_id] = inst.result_id();
    if (loop.GetInductionVariable()->def_->result_id() == old_id) {
      new_induction = &inst;
    }
  }

  return new_induction;
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

void LoopUtils::RemapOperands(ir::BasicBlock* BB, uint32_t old_header,
                              std::map<uint32_t, uint32_t>& new_inst) const {
  for (ir::Instruction& inst : *BB) {
    auto remap_operands_to_new_ids = [&new_inst, old_header](uint32_t* id) {
      auto itr = new_inst.find(*id);
      if (itr != new_inst.end() && *id != old_header) {
        *id = itr->second;
      }
    };

    inst.ForEachInId(remap_operands_to_new_ids);
  }
}

ir::Instruction* LoopUtils::CopyBody(ir::Loop& loop, int,
                                     ir::Instruction* previous_phi) {
  // Map of basic blocks ids
  std::map<uint32_t, ir::BasicBlock*> new_blocks;

  std::map<uint32_t, uint32_t> new_inst;
  const ir::Loop::BasicBlockOrderedListTy& basic_blocks = loop.GetBlocks();

  uint32_t new_header_id = 0;
  uint32_t latch_block_id = 0;

  ir::Instruction* phi = nullptr;
  ir::BasicBlock* new_continue_block = nullptr;
  for (const ir::BasicBlock* itr : basic_blocks) {
    // Copy the loop basicblock.
    ir::BasicBlock* BB = itr->Clone(ir_context_);
    // Assign each result a new unique ID and keep a mapping of the old ids to
    // the new ones.
    ir::Instruction* new_phi = RemapResultIDs(loop, BB, new_inst);

    if (itr == loop.GetLatchBlock()) {
      ir::Instruction* merge_inst = loop.GetHeaderBlock()->GetLoopMergeInst();
      merge_inst->SetInOperand(1, {BB->id()});

      latch_block_id = BB->id();
      new_continue_block = BB;
    }

    if (itr == loop.GetHeaderBlock()) {
      new_header_id = BB->id();
      phi = new_phi;
    }

    ir::Instruction* merge_inst = BB->GetLoopMergeInst();
    if (merge_inst) ir_context_->KillInst(merge_inst);

    blocks_to_add_.push_back(std::unique_ptr<ir::BasicBlock>(BB));
    new_blocks[itr->id()] = BB;
  }

  ir::Instruction& branch = *loop.GetPreHeaderBlock()->tail();

  uint32_t old_header = branch.GetSingleWordOperand(0);

  // Set the previous continue block to point to the new header.
  ir::Instruction& continue_branch = *previous_continue_block_->tail();
  continue_branch.SetInOperand(0, {new_header_id});

  // As the algorithm copies the original loop blocks exactly, the tail of the
  // latch block on iterations after one will be a branch to the new header and
  // not the actual loop header.
  ir::Instruction& new_continue_branch = *new_continue_block->tail();
  new_continue_branch.SetInOperand(0, {loop.GetHeaderBlock()->id()});

  // Update the previous continue block tracker.
  previous_continue_block_ = new_continue_block;

  // Update references to the old phi node with the actual variable.
  ir::Instruction* induction = loop.GetInductionVariable()->def_;
  new_inst[induction->result_id()] =
      GetPhiVariableID(previous_phi, previous_latch_block_id_);

  // Only reference to the header block is the backedge in the latch block,
  // don't change this.
  new_inst[loop.GetHeaderBlock()->id()] = loop.GetHeaderBlock()->id();

  previous_latch_block_id_ = latch_block_id;
  for (auto& pair : new_blocks) {
    RemapOperands(pair.second, old_header, new_inst);
  }
  return phi;
}

void LoopUtils::RemoveLoopFromFunction(ir::Loop& loop,
                                       ir::BasicBlock* preheader) {
  // Change preheader branch to branch to the merge block rather than the header
  // block.
  ir::Instruction& branch = *preheader->tail();

  branch.RemoveOperand(0);
  branch.AddOperand({SPV_OPERAND_TYPE_ID, {loop.GetMergeBlock()->id()}});
}

bool LoopUtils::PartiallyUnroll(ir::Loop& loop, int) {
  ir::Loop::LoopVariable* induction = loop.GetInductionVariable();

  blocks_to_add_.clear();

  if (!induction) return false;
  previous_latch_block_id_ = loop.GetLatchBlock()->id();
  ir::Instruction* phi = induction->def_;
  previous_continue_block_ = loop.GetLatchBlock();
  for (int i = 0; i < 10; ++i) {
    phi = CopyBody(loop, i + 1, phi);
  }

  uint32_t phi_index = 5;
  uint32_t phi_variable = phi->GetSingleWordOperand(phi_index - 1);
  uint32_t phi_label = phi->GetSingleWordOperand(phi_index);

  ir::Instruction* original_phi = induction->def_;
  // SetInOperands are offset by two.
  original_phi->SetInOperand(phi_index - 3, {phi_variable});
  original_phi->SetInOperand(phi_index - 2, {phi_label});

  for (auto basic_block_iterator = function_.begin();
       basic_block_iterator != function_.end(); ++basic_block_iterator) {
    if (basic_block_iterator->id() == loop.GetMergeBlock()->id()) {
      basic_block_iterator.InsertBefore(&blocks_to_add_);
      break;
    }
  }

  // Invalidate all.
  ir_context_->InvalidateAnalysesExceptFor(
      ir::IRContext::Analysis::kAnalysisNone);

  return true;
}

bool LoopUtils::FullyUnroll(ir::Loop& loop) {
  ir::Loop::LoopVariable* induction = loop.GetInductionVariable();

  if (!induction) return false;
/*
  ir::BasicBlock* preheader = loop.GetPreHeaderBlock();
  for (int i = 0; i < 1; ++i) {
    preheader = CopyLoop(loop, preheader);
  }

  RemoveLoopFromFunction(loop, preheader);*/
  return true;
}

Pass::Status LoopUnroller::Process(ir::IRContext* c) {
  context_ = c;
  for (ir::Function& f : *c->module()) {
    LoopUtils loop_utils{f, c};
    //    loop_utils.InsertLoopClosedSSA();

    for (auto& loop : loop_utils.GetLoopDescriptor()) {
      loop_utils.PartiallyUnroll(loop, 2);
    }
  }
  return Pass::Status::SuccessWithChange;
}

}  // namespace opt
}  // namespace spvtools
