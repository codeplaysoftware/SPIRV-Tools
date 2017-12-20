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
#include <vector>

#include "opt/iterator.h"
#include "opt/loop_descriptor.h"
#include "opt/make_unique.h"
#include "opt/tree_iterator.h"

namespace spvtools {
namespace opt {

Loop::Loop(ir::IRContext* context, opt::DominatorAnalysis* dom_analysis,
           ir::BasicBlock* header, ir::BasicBlock* continue_target,
           ir::BasicBlock* merge_target)
    : ir_context_(context),
      dom_analysis_(dom_analysis),
      loop_header_(header),
      loop_continue_(continue_target),
      loop_merge_(merge_target),
      loop_preheader_(nullptr),
      parent_(nullptr),
      induction_variable_() {
  assert(dom_analysis_);
  SetLoopPreheader();
  AddBasicBlockToLoop(header);
  AddBasicBlockToLoop(continue_target);
}

void Loop::SetLoopPreheader() {
  ir::CFG* cfg = ir_context_->cfg();
  DominatorTree& dom_tree = dom_analysis_->GetDomTree();
  DominatorTreeNode* header_node = dom_tree[loop_header_];

  // The loop predecessor basic block.
  ir::BasicBlock* loop_pred = nullptr;

  auto header_pred = cfg->preds(loop_header_->id());
  for (uint32_t p_id : header_pred) {
    DominatorTreeNode* node = dom_tree[p_id];
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

InductionVariable* Loop::GetInductionVariable() {
  if (!induction_variable_.def_) {
    FindInductionVariable();
  }
  if (induction_variable_.def_) return nullptr;
  return &induction_variable_;
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

      induction_variable_ = InductionVariable(
          variable_inst, step_value, step_value, const_value, condition);
    }
  }
}

LoopDescriptor::LoopDescriptor(const ir::Function* f) { PopulateList(f); }

void LoopDescriptor::PopulateList(const ir::Function* f) {
  ir::IRContext* context = f->GetParent()->context();

  opt::DominatorAnalysis* dom_analysis_ =
      context->GetDominatorAnalysis(f, *context->cfg());

  loops_.clear();

  // Traverse the tree and apply the above functor to find all the OpLoopMerge
  // instructions. Instructions will be in domination order of BasicBlocks.
  // However, this does not mean that dominance is implied by the order of
  // loop_merge_inst you still need to check dominance between each block
  // manually.
  DominatorTree& dom_tree = dom_analysis_->GetDomTree();
  // Post-order traversal of the dominator tree: inner loop will be inserted
  // first.
  for (DominatorTreeNode& node : dom_tree.PostorderRange()) {
    ir::Instruction* merge_inst = node.bb_->GetLoopMergeInst();
    if (merge_inst) {
      // The id of the continue basic block of this loop.
      uint32_t merge_bb_id = merge_inst->GetSingleWordOperand(0);

      // The id of the continue basic block of this loop.
      uint32_t continue_bb_id = merge_inst->GetSingleWordOperand(1);

      // The continue target of this loop.
      ir::BasicBlock* merge_bb = context->cfg()->block(merge_bb_id);

      // The continue target of this loop.
      ir::BasicBlock* continue_bb = context->cfg()->block(continue_bb_id);

      // The basicblock containing the merge instruction.
      ir::BasicBlock* header_bb = context->get_instr_block(merge_inst);

      // Add the loop the list of all the loops in the function.
      loops_.emplace_back(MakeUnique<Loop>(context, dom_analysis_, header_bb,
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
        if (!dom_analysis_->Dominates(header_bb,
                                      previous_loop->GetHeaderBlock()))
          break;
        // If the current loop merge dominates the previous loop then it is
        // not nested loop.
        if (dom_analysis_->Dominates(merge_bb, previous_loop->GetHeaderBlock()))
          break;

        current_loop->AddNestedLoop(previous_loop);
      }
      DominatorTreeNode* dom_merge_node = dom_tree[merge_bb];
      for (DominatorTreeNode& loop_node :
           ir::make_range(node.df_begin(), node.df_end())) {
        // Check if we are in the loop.
        if (dom_tree.Dominates(dom_merge_node, &loop_node)) continue;
        current_loop->AddBasicBlockToLoop(loop_node.bb_);
      }
    }
  }
}

}  // namespace opt
}  // namespace spvtools
