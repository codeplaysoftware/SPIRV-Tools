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

#include "opt/loop_descriptor.h"
#include <iostream>

namespace spvtools {
namespace opt {

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

  opt::DominatorTree& tree = dom_analysis->GetDomTree();

  auto begin_itr = tree.get_iterator(loop_start_);
  for (; begin_itr != tree.end(); ++begin_itr) {
    if (dom_analysis->Dominates(loop_merge_, begin_itr->bb_)) break;
    loop_basic_blocks.insert(begin_itr->bb_);
  };
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

  opt::DominatorAnalysis* dom_analysis =
      context->GetDominatorAnalysis(f, *context->cfg());

  std::vector<ir::Instruction*> loop_merge_inst;
  // Function to find OpLoopMerge instructions inside the dominator tree.
  auto find_merge_inst_in_dom_order =
      [&loop_merge_inst](const DominatorTreeNode* node) {
        if (node->id() == 0) return true;
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
  dom_tree.Visit(find_merge_inst_in_dom_order);

  loops_.clear();
  loops_.reserve(loop_merge_inst.size());

  // Populate the loop vector from the merge instructions found in the dominator
  // tree.
  for (ir::Instruction* merge_inst : loop_merge_inst) {
    // The id of the continue basic block of this loop.
    uint32_t merge_bb_id = merge_inst->GetSingleWordOperand(0);

    // The id of the continue basic block of this loop.
    uint32_t continue_bb_id = merge_inst->GetSingleWordOperand(1);

    // The continue target of this loop.
    ir::BasicBlock* merge_bb = context->cfg()->block(merge_bb_id);

    // The continue target of this loop.
    ir::BasicBlock* continue_bb = context->cfg()->block(continue_bb_id);

    // The basicblock containing the merge instruction.
    ir::BasicBlock* start_bb = context->get_instr_block(merge_inst);

    // Add the loop the list of all the loops in the function.
    loops_.push_back({start_bb, continue_bb, merge_bb, context, dom_analysis});

    // If this is the first loop don't check for dominating nesting loop.
    // Otherwise, move through the loops in reverse order to check if this is a
    // nested loop. If it isn't a nested loop this for will exit on the first
    // iteration.
    for (auto itr = loops_.rbegin() + 1; itr != loops_.rend(); ++itr) {
      Loop& previous_loop = *itr;

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
        previous_loop.AddNestedLoop(&loops_.back());
        loops_.back().SetParent(&previous_loop);
        break;
      }
    }
  }
}

}  // opt
}  // spvtools
