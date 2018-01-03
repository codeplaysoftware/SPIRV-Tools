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
#include <type_traits>
#include <utility>
#include <vector>

#include "opt/iterator.h"
#include "opt/loop_descriptor.h"
#include "opt/make_unique.h"
#include "opt/tree_iterator.h"

namespace spvtools {
namespace ir {

Loop::Loop(IRContext* context, opt::DominatorAnalysis* dom_analysis,
           BasicBlock* header, BasicBlock* continue_target,
           BasicBlock* merge_target)
    : loop_header_(header),
      loop_continue_(continue_target),
      loop_merge_(merge_target),
      loop_preheader_(nullptr),
      parent_(nullptr) {
  assert(context);
  assert(dom_analysis);
  SetLoopPreheader(context, dom_analysis);
  AddBasicBlockToLoop(header);
  AddBasicBlockToLoop(continue_target);
}

void Loop::SetLoopPreheader(IRContext* ir_context,
                            opt::DominatorAnalysis* dom_analysis) {
  CFG* cfg = ir_context->cfg();
  opt::DominatorTree& dom_tree = dom_analysis->GetDomTree();
  opt::DominatorTreeNode* header_node = dom_tree[loop_header_];

  // The loop predecessor.
  BasicBlock* loop_pred = nullptr;

  auto header_pred = cfg->preds(loop_header_->id());
  for (uint32_t p_id : header_pred) {
    opt::DominatorTreeNode* node = dom_tree[p_id];
    if (node && !dom_tree.Dominates(header_node, node)) {
      // The predecessor is not part of the loop, so potential loop preheader.
      if (loop_pred && node->bb_ != loop_pred) {
        // If we saw 2 distinct predecessors that are outside the loop, we don't
        // have a loop preheader.
        return;
      }
      loop_pred = node->bb_;
    }
  }
  // Safe guard against invalid code, SPIR-V spec forbids loop with the entry
  // node as header.
  assert(loop_pred && "The header node is the entry block ?");

  // So we have a unique basic block that can enter this loop.
  // If this loop is the unique successor of this block, then it is a loop
  // preheader.
  //
  // FIXME: if instead of having "ForEach*" functions, we had iterators, the
  // standard library would be usable...
  bool is_preheader = true;
  uint32_t loop_header_id = loop_header_->id();
  loop_pred->ForEachSuccessorLabel(
      [&is_preheader, loop_header_id](const uint32_t id) {
        if (id != loop_header_id) is_preheader = false;
      });
  if (is_preheader) loop_preheader_ = loop_pred;
}

LoopDescriptor::LoopDescriptor(const Function* f) { PopulateList(f); }

void LoopDescriptor::PopulateList(const Function* f) {
  IRContext* context = f->GetParent()->context();

  opt::DominatorAnalysis* dom_analysis =
      context->GetDominatorAnalysis(f, *context->cfg());

  loops_.clear();

  // Traverse the tree and apply the above functor to find all the OpLoopMerge
  // instructions. Instructions will be in domination order of BasicBlocks.
  // However, this does not mean that dominance is implied by the order of
  // loop_merge_inst you still need to check dominance between each block
  // manually.
  opt::DominatorTree& dom_tree = dom_analysis->GetDomTree();
  // Post-order traversal of the dominator tree: inner loop will be inserted
  // first.
  for (opt::DominatorTreeNode& node :
       ir::make_range(dom_tree.post_begin(), dom_tree.post_end())) {
    Instruction* merge_inst = node.bb_->GetLoopMergeInst();
    if (merge_inst) {
      // The id of the continue basic block of this loop.
      uint32_t merge_bb_id = merge_inst->GetSingleWordOperand(0);

      // The id of the continue basic block of this loop.
      uint32_t continue_bb_id = merge_inst->GetSingleWordOperand(1);

      // The continue target of this loop.
      BasicBlock* merge_bb = context->cfg()->block(merge_bb_id);

      // The continue target of this loop.
      BasicBlock* continue_bb = context->cfg()->block(continue_bb_id);

      // The basicblock containing the merge instruction.
      BasicBlock* header_bb = context->get_instr_block(merge_inst);

      // Add the loop the list of all the loops in the function.
      loops_.emplace_back(MakeUnique<Loop>(context, dom_analysis, header_bb,
                                           continue_bb, merge_bb));
      Loop* current_loop = loops_.back().get();

      // We have a bottom-up construction, so if this loop has nested-loops,
      // they are by construction at the tail of the loop list.
      for (auto itr = loops_.rbegin() + 1; itr != loops_.rend(); ++itr) {
        Loop* previous_loop = itr->get();

        // If the loop already has a parent, then it has been processed.
        if (previous_loop->HasParent()) break;

        // If the current loop does not dominates the previous loop then it is
        // not nested loop.
        if (!dom_analysis->Dominates(header_bb,
                                     previous_loop->GetHeaderBlock()))
          break;
        // If the current loop merge dominates the previous loop then it is
        // not nested loop.
        if (dom_analysis->Dominates(merge_bb, previous_loop->GetHeaderBlock()))
          break;

        current_loop->AddNestedLoop(previous_loop);
      }
      opt::DominatorTreeNode* dom_merge_node = dom_tree[merge_bb];
      for (opt::DominatorTreeNode& loop_node :
           make_range(node.df_begin(), node.df_end())) {
        // Check if we are in the loop.
        if (dom_tree.Dominates(dom_merge_node, &loop_node)) continue;
        current_loop->AddBasicBlockToLoop(loop_node.bb_);
        basic_block_to_loop_.insert(
            std::make_pair(loop_node.bb_->id(), current_loop));
      }
    }
  }
  for (std::unique_ptr<Loop>& loop : loops_) {
    if (!loop->HasParent()) dummy_top_loop_.nested_loops_.push_back(loop.get());
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
  opt::analysis::DefUseManager* def_use_manager =
      ir_context_->get_def_use_mgr();
  ir::Instruction* var =
      def_use_manager->GetDef(load_inst->GetSingleWordOperand(2));

  return var;
}

void Loop::FindLoopBasicBlocks() {
  loop_basic_blocks_.clear();

  opt::DominatorTree& tree = dom_analysis_->GetDomTree();

  // Starting the loop header BasicBlock, traverse the dominator tree until we
  // reach the merge blockand add every node we traverse to the set of blocks
  // which we consider to be the loop.
  auto begin_itr = tree.get_iterator(loop_header_);
  for (; begin_itr != tree.end(); ++begin_itr) {
    if (!dom_analysis_->Dominates(loop_merge_, begin_itr->bb_)) {
      loop_basic_blocks_.insert(begin_itr->bb_->id());
    }
  };
}

bool Loop::IsLoopInvariant(const ir::Instruction* variable_inst) {
  opt::analysis::DefUseManager* def_use_manager =
      ir_context_->get_def_use_mgr();

  FindLoopBasicBlocks();

  bool is_invariant = true;
  auto find_stores = [&is_invariant, this](ir::Instruction* user) {
    if (user->opcode() == SpvOp::SpvOpStore) {
      // Get the BasicBlock this block belongs to.
      const ir::BasicBlock* parent_block =
          user->context()->get_instr_block(user);

      // If any of the stores are in the loop.
      if (loop_basic_blocks_.count(parent_block->id()) != 0) {
        // Then the variable is variant to the loop.
        is_invariant = false;
      }
    }
  };

  def_use_manager->ForEachUser(variable_inst, find_stores);

  return is_invariant;
}

ir::Instruction* Loop::GetInductionStepOperation(
    const ir::Instruction* variable_inst) const {
  ir::BasicBlock* bb = loop_continue_;

  ir::Instruction* store = nullptr;

  // Move over every store in the BasicBlock to find the store assosiated with
  // the given BB.
  auto find_store = [&store, &variable_inst](ir::Instruction* inst) {
    if (inst->opcode() == SpvOp::SpvOpStore &&
        inst->GetSingleWordOperand(0) == variable_inst->result_id()) {
      store = inst;
    }
  };

  bb->ForEachInst(find_store);
  if (!store) return nullptr;

  opt::analysis::DefUseManager* def_use_manager =
      ir_context_->get_def_use_mgr();

  ir::Instruction* inst =
      def_use_manager->GetDef(store->GetSingleWordOperand(1));

  if (!inst || inst->opcode() != SpvOp::SpvOpIAdd) {
    return nullptr;
  }

  return inst;
}

bool Loop::GetInductionInitValue(const ir::Instruction* variable_inst,
                                 uint32_t* value) const {
  // We assume that the immediate dominator of the loop start block should
  // contain the initialiser for the induction variables.
  ir::BasicBlock* bb = dom_analysis_->ImmediateDominator(loop_header_);
  if (!bb) return false;

  ir::Instruction* store = nullptr;
  auto find_store = [&store, &variable_inst](ir::Instruction* inst) {
    if (inst->opcode() == SpvOp::SpvOpStore &&
        inst->GetSingleWordOperand(0) == variable_inst->result_id()) {
      store = inst;
    }
  };

  // Find the storing of the induction variable.
  bb->ForEachInst(find_store);
  if (!store) return false;

  opt::analysis::DefUseManager* def_use_manager =
      ir_context_->get_def_use_mgr();

  ir::Instruction* constant =
      def_use_manager->GetDef(store->GetSingleWordOperand(1));
  if (!constant) return false;

  return GetConstant(constant, value);
}

Loop::LoopVariable* Loop::GetInductionVariable() {
  if (!induction_variable_) {
    FindInductionVariable();
  }

  return induction_variable_.get();
}

void Loop::FindInductionVariable() {
  // Get the basic block which branches to the merge block.
  const ir::BasicBlock* bb = dom_analysis_->ImmediateDominator(loop_merge_);

  // Find the branch instruction.
  const ir::Instruction& branch_inst = *bb->ctail();
  if (branch_inst.opcode() == SpvOp::SpvOpBranchConditional) {
    // From the branch instruction find the branch condition.
    opt::analysis::DefUseManager* def_use_manager =
        ir_context_->get_def_use_mgr();
    ir::Instruction* condition =
        def_use_manager->GetDef(branch_inst.GetSingleWordOperand(0));

    if (condition && condition->opcode() == SpvOp::SpvOpSLessThan) {
      // The right hand side operand of the operation.
      const ir::Instruction* rhs_inst =
          def_use_manager->GetDef(condition->GetSingleWordOperand(3));

      uint32_t const_value = 0;
      // Exit out if we couldn't resolve the rhs to be a constant integer.
      // TODO: Make this work for other values on rhs.
      if (!GetConstant(rhs_inst, &const_value)) return;

      // The left hand side operand of the operation.
      const ir::Instruction* lhs_inst =
          def_use_manager->GetDef(condition->GetSingleWordOperand(2));

      ir::Instruction* variable_inst = GetVariable(lhs_inst);

      if (IsLoopInvariant(variable_inst)) {
        return;
      }

      uint32_t init_value = 0;
      GetInductionInitValue(variable_inst, &init_value);

      ir::Instruction* step_inst = GetInductionStepOperation(variable_inst);

      if (!step_inst) return;

      // The instruction representing the constant value.
      const ir::Instruction* step_amount_inst =
          def_use_manager->GetDef(step_inst->GetSingleWordOperand(3));

      uint32_t step_value = 0;
      // Exit out if we couldn't resolve the rhs to be a constant integer.
      if (!GetConstant(step_amount_inst, &step_value)) return;

      induction_variable_ =
          std::unique_ptr<Loop::LoopVariable>(new Loop::LoopVariable(
              variable_inst, step_value, step_value, const_value, condition));
    }
  }
}

}  // namespace ir
}  // namespace spvtools
