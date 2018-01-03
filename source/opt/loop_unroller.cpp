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

// TODO: Roll these static functions into utility classes.
static void insertLoopClosedSSAExit(ir::Function& func) {
  ir::IRContext* context = func.GetParent()->context();

  opt::analysis::DefUseManager* def_use_manager = context->get_def_use_mgr();

  for (ir::BasicBlock& bb : func) {
    for (ir::Instruction& inst : bb) {
      if (inst.opcode() == SpvOpLoopMerge) {
        std::unique_ptr<ir::Instruction> new_label{new ir::Instruction(
            context, SpvOp::SpvOpLabel, 0, context->TakeNextUniqueId(), {})};

        std::unique_ptr<ir::BasicBlock> new_exit_bb{
            new ir::BasicBlock(std::move(new_label))};

        uint32_t merge_block_id = inst.GetSingleWordOperand(0);
        ir::BasicBlock* current_loop_exit =
            context->get_instr_block(def_use_manager->GetDef(merge_block_id));

        ir::BasicBlock* new_exit_bb_soft_copy = new_exit_bb.get();
        uint32_t new_id = new_exit_bb->id();
        func.AddBasicBlock(current_loop_exit, std::move(new_exit_bb));

        context->InvalidateAnalysesExceptFor(
            ir::IRContext::Analysis::kAnalysisNone);

        context->ReplaceAllUsesWith(merge_block_id, new_id);

        std::unique_ptr<ir::Instruction> new_branch{
            new ir::Instruction(context, SpvOp::SpvOpBranch, 0, 0,
                                {{SPV_OPERAND_TYPE_ID, {merge_block_id}}})};

        new_exit_bb_soft_copy->AddInstruction(std::move(new_branch));
      }
    }
  }
}

static void remapResultIds(ir::Loop&, ir::BasicBlock* BB,ir::IRContext* context,
                           std::map<uint32_t, uint32_t>& new_inst) {
  // Label instructions aren't covered by normal traversal of the instructions.
  uint32_t new_label_id = context->TakeNextUniqueId();
  new_inst[BB->GetLabelInst()->result_id()] = new_label_id;
  BB->GetLabelInst()->SetResultId(new_label_id);

  for (ir::Instruction& inst : *BB) {
    uint32_t old_id = inst.result_id();

    if (old_id == 0) {
      continue;
    }
    inst.SetResultId(context->TakeNextUniqueId());
    new_inst[old_id] = inst.result_id();
  }
}

static void remapOperands(ir::BasicBlock* BB, uint32_t old_header,
                          std::map<uint32_t, uint32_t>& new_inst) {
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

static void copyEachBB(ir::Loop& loop, ir::IRContext* context) {
  // Map of basic blocks ids
  std::map<uint32_t, ir::BasicBlock*> new_blocks;

  std::map<uint32_t, uint32_t> new_inst;
  const ir::Loop::BasicBlockListTy& basic_blocks = loop.GetBlocks();

  for (uint32_t id : basic_blocks) {
    const ir::BasicBlock* itr = context->get_instr_block(id);

    ir::BasicBlock* BB = itr->Clone(context);
    remapResultIds(loop, BB, context, new_inst);

    ir::Instruction* merge_inst = BB->GetLoopMergeInst();
    if (merge_inst) context->KillInst(merge_inst);

    itr->GetParent()->AddBasicBlock(loop.GetHeaderBlock(),
                                    std::unique_ptr<ir::BasicBlock>(BB));

    new_blocks[itr->id()] = BB;
  }

  ir::BasicBlock* preheader = loop.GetPreHeaderBlock();

  ir::Instruction& branch = *preheader->tail();

  // TODO: Move this up.
  if (branch.opcode() != SpvOpBranch) {
    return;
  }
  uint32_t old_header = branch.GetSingleWordOperand(0);

  // Make all jumps to the loop merge be the Loop Closure SSA exit node.
  ir::BasicBlock* merge = loop.GetMergeBlock();
  new_inst[merge->id()] = merge->tail()->GetSingleWordOperand(0);

  for (auto& pair : new_blocks) {
    remapOperands(pair.second, old_header, new_inst);
  }

  uint32_t new_pre_header_block = new_blocks[old_header]->id();
  branch.SetInOperand(0, {new_pre_header_block});
}

static bool unroll(ir::Loop& loop, ir::IRContext* context) {
//  ir::Loop::LoopVariable* induction = loop.GetInductionVariable();

//  if (!induction) return false;

  copyEachBB(loop, context);
  return true;
}

Pass::Status LoopUnroller::Process(ir::IRContext* c) {
  context_ = c;
  for (ir::Function& f : *c->module()) {
    insertLoopClosedSSAExit(f);
    RunOnFunction(f);
  }
  return Pass::Status::SuccessWithChange;
}

bool LoopUnroller::RunOnFunction(ir::Function& f) {
  ir::LoopDescriptor LD{&f};
  for (auto& loop : LD) {
    RunOnLoop(loop);
  }
  return true;
}

bool LoopUnroller::RunOnLoop(ir::Loop& loop) { return unroll(loop, context_); }

}  // namespace opt
}  // namespace spvtools
