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

#include "loop_descriptor.h"
#include <iostream>

namespace spvtools {
namespace opt {

namespace {

// TODO: Move this into ir::Function
static const spvtools::ir::BasicBlock* GetBasicBlock(
    const spvtools::ir::Function* fn, uint32_t id) {
  for (const spvtools::ir::BasicBlock& bb : *fn) {
    if (bb.id() == id) {
      return &bb;
    }
  }
  return nullptr;
}
}

bool Loop::GetConstant(const ir::Instruction* inst, uint32_t* value) const {
  if (inst->opcode() != SpvOp::SpvOpConstant) {
    return false;
  }

  const ir::Operand& operand = inst->GetOperand(2);

  if (operand.type != SPV_OPERAND_TYPE_TYPED_LITERAL_NUMBER ||
      operand.words.size() != 1) {
    return false;
  }

  *value = operand.words[0];
  return true;
}

// Returns an OpVariable instruction or null from a load_inst.
ir::Instruction* Loop::GetVariable(const ir::Instruction* load_inst) {
  if (!load_inst || load_inst->opcode() != SpvOp::SpvOpLoad) {
    return nullptr;
  }

  // From the branch instruction find the branch condition.
  opt::analysis::DefUseManager* def_use_manager = ir_context->get_def_use_mgr();
  ir::Instruction* var =
      def_use_manager->GetDef(load_inst->GetSingleWordOperand(2));

  return var;
}

void Loop::FindLoopBasicBlocks() {
  loop_basic_blocks.clear();

  auto find_all_blocks_in_loop = [&](const DominatorTreeNode* node) {
    if (dom_analysis->Dominates(loop_merge_, node->bb_)) return false;
    loop_basic_blocks.insert(node->bb_);
    return true;
  };

  const DominatorTree& dom_tree = dom_analysis->GetDomTree();
  dom_tree.Visit(loop_start_, find_all_blocks_in_loop);
}

bool Loop::IsLoopInvariant(const ir::Instruction* variable_inst) {
  opt::analysis::DefUseManager* def_use_manager = ir_context->get_def_use_mgr();

  FindLoopBasicBlocks();

  bool is_invariant = true;
  auto find_stores = [&is_invariant, this](ir::Instruction* user) {
    if (user->opcode() == SpvOp::SpvOpStore) {
      // Get the BasicBlock this block belongs to.
      const ir::BasicBlock* parent_block =
          user->context()->get_instr_block(user);

      // If any of the stores are in the loop.
      if (loop_basic_blocks.count(parent_block) != 0) {
        // Then the variable is variant to the loop.
        is_invariant = false;
      }
    }
  };

  def_use_manager->ForEachUser(variable_inst, find_stores);

  return is_invariant;
}

void Loop::FindInductionVariable() {
  // Get the basic block which branches to the merge block.
  const ir::BasicBlock* bb = dom_analysis->ImmediateDominator(loop_merge_);

  // Find the branch instruction.
  const ir::Instruction& branch_inst = *bb->ctail();
  if (branch_inst.opcode() == SpvOp::SpvOpBranchConditional) {
    // From the branch instruction find the branch condition.
    opt::analysis::DefUseManager* def_use_manager =
        ir_context->get_def_use_mgr();
    const ir::Instruction* condition =
        def_use_manager->GetDef(branch_inst.GetSingleWordOperand(0));

    if (condition && condition->opcode() == SpvOp::SpvOpSLessThan) {
      // The right hand side operand of the operation.
      const ir::Instruction* rhs_inst =
          def_use_manager->GetDef(condition->GetSingleWordOperand(3));

      uint32_t const_value = 0;
      // Exit out if we could resolve the rhs to be a constant integer.
      // TODO: Make this work for other values on rhs.
      if (!GetConstant(rhs_inst, &const_value)) return;

      // The left hand side operand of the operation.
      const ir::Instruction* lhs_inst =
          def_use_manager->GetDef(condition->GetSingleWordOperand(2));

      ir::Instruction* variable_inst = GetVariable(lhs_inst);

      if (IsLoopInvariant(variable_inst)) {
        return;
      }

      if (variable_inst) {
        std::cout << "Variable " << variable_inst->result_id()
                  << " found with upper range: " << const_value << "\n";
      }
    }
  }
}

LoopDescriptor::LoopDescriptor(const ir::Function* f) { PopulateList(f); }

void LoopDescriptor::PopulateList(const ir::Function* f) {
  ir::IRContext* context = f->GetParent()->context();

  ir::CFG cfg{f->GetParent()};
  opt::DominatorAnalysis* dom_analysis = context->GetDominatorAnalysis(f, cfg);

  dom_analysis->DumpAsDot(std::cout);

  std::vector<ir::Instruction*> loop_merge_inst;
  // Function to find OpLoopMerge instructions inside the dominator tree.
  auto find_merge_inst_in_dom_order =
      [&loop_merge_inst](const DominatorTreeNode* node) {
        ir::Instruction* merge_inst = node->bb_->GetLoopMergeInst();
        if (merge_inst) {
          loop_merge_inst.push_back(merge_inst);
        }
        return true;
      };

  // Traverse the tree and apply the above functor to find all the OpLoopMerge
  // instructions. Instructions will be in domination order of BasicBlocks.
  // However, this does not mean that dominance is implied by the order of
  // loop_merge_inst you still need to check dominance between each block
  // manually.
  const DominatorTree& dom_tree = dom_analysis->GetDomTree();
  dom_tree.Visit(dom_tree.GetRoot(), find_merge_inst_in_dom_order);

  // Populate the loop vector from the merge instructions found in the dominator
  // tree.
  for (ir::Instruction* merge_inst : loop_merge_inst) {
    // The id of the continue basic block of this loop.
    uint32_t merge_bb_id = merge_inst->GetSingleWordOperand(0);

    // The id of the continue basic block of this loop.
    uint32_t continue_bb_id = merge_inst->GetSingleWordOperand(1);

    // The continue target of this loop.
    const ir::BasicBlock* merge_bb = GetBasicBlock(f, merge_bb_id);

    // The continue target of this loop.
    const ir::BasicBlock* continue_bb = GetBasicBlock(f, continue_bb_id);

    // The basicblock containing the merge instruction.
    const ir::BasicBlock* start_bb = context->get_instr_block(merge_inst);

    bool is_nested = false;

    // The index of the top most parent loop of a nested loop.
    signed long parent_loop_index = -1;

    // If this is the first loop don't check for dominating nesting loop.
    // Otherwise, move through the loops in reverse order to check if this is a
    // nested loop. If it isn't a nested loop this for will exit on the first
    // iteration.
    for (signed long i = loops_.size() - 1; i >= 0; --i) {
      Loop& previous_loop = loops_[i];

      // If this loop is dominated by the entry of the previous loop it could be
      // a nested loop of that loop or a nested loop of a parent of that loop.
      // Otherwise it's not nested at all.
      if (!dom_analysis->Dominates(previous_loop.GetStartBB(), start_bb)) break;

      // If this loop is dominated by the merge block of the previous loop it's
      // a nested loop of the parent of the previous loop. Otherwise it's just a
      // nested loop of the parent.
      if (dom_analysis->Dominates(previous_loop.GetMergeBB(), start_bb)) {
        continue;
      } else {
        parent_loop_index = i;
        is_nested = true;
        break;
      }
    }

    // Add the loop the list of all the loops in the function.
    loops_.push_back(
        {is_nested, start_bb, continue_bb, merge_bb, context, dom_analysis});

    // If it's nested add a reference to it to the parent loop.
    if (is_nested) {
      assert(parent_loop_index != -1);
      loops_[parent_loop_index].AddNestedLoop(&loops_.back());
    }
  }

  for (Loop& loop : loops_) {
    loop.GetInductionVariable();
  }
}

}  // opt
}  // spvtools
