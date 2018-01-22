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

// Static helper functions to extract information from induction variable
// instructions.
namespace {

// Takes in an instruction |inst| and stores the constant integer found inside
// the instruction. Returns false if the |inst| does not contain a constant
// integer.
static bool GetConstant(const ir::Instruction* inst, uint32_t* value) {
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

// Takes in phi node |induction| and the loop |header| and returns the step
// operation of the loop.
static ir::Instruction* GetInductionStepOperation(
    const ir::Instruction* induction, const ir::BasicBlock* header,
    ir::IRContext* context, opt::DominatorAnalysis* analysis) {
  ir::Instruction* step = nullptr;

  opt::analysis::DefUseManager* def_use_manager = context->get_def_use_mgr();

  // Verify the phi node
  for (uint32_t operand_id = 3; operand_id < induction->NumOperands();
       operand_id += 2) {
    // Incoming edge.
    ir::BasicBlock* incoming_block =
        context->cfg()->block(induction->GetSingleWordOperand(operand_id));

    // Check if the block is dominated by header, and thus coming from within
    // the loop.
    if (analysis->Dominates(header, incoming_block)) {
      step = def_use_manager->GetDef(
          induction->GetSingleWordOperand(operand_id - 1));
    }
  }

  if (!step || step->opcode() != SpvOp::SpvOpIAdd) {
    return nullptr;
  }

  return step;
}

// Extract the initial value from the |induction| variable and store it in
// |value|. If the function couldn't find the initial value of |induction|
// return false.
static bool GetInductionInitValue(const ir::Instruction* induction,
                                  const ir::BasicBlock* header,
                                  ir::IRContext* context,
                                  opt::DominatorAnalysis* analysis,
                                  uint32_t* value) {
  // We assume that the immediate dominator of the loop start block should
  // contain the initialiser for the induction variables.

  ir::Instruction* constant = nullptr;
  opt::analysis::DefUseManager* def_use_manager = context->get_def_use_mgr();

  for (uint32_t operand_id = 3; operand_id < induction->NumOperands();
       operand_id += 2) {
    ir::BasicBlock* bb =
        context->cfg()->block(induction->GetSingleWordOperand(operand_id));

    if (!analysis->Dominates(header, bb)) {
      constant = def_use_manager->GetDef(
          induction->GetSingleWordOperand(operand_id - 1));
    }
  }

  if (!constant) return false;

  return GetConstant(constant, value);
}

}  // namespace

Loop::Loop(IRContext* context, opt::DominatorAnalysis* dom_analysis,
           BasicBlock* header, BasicBlock* continue_target,
           BasicBlock* merge_target)
    : loop_header_(header),
      loop_continue_(continue_target),
      loop_merge_(merge_target),
      loop_preheader_(nullptr),
      parent_(nullptr),
      ir_context_(context),
      dom_analysis_(dom_analysis) {
  assert(context);
  assert(dom_analysis);
  loop_preheader_ = FindLoopPreheader(context, dom_analysis);
}

BasicBlock* Loop::FindLoopPreheader(IRContext* ir_context,
                                    opt::DominatorAnalysis* dom_analysis) {
  CFG* cfg = ir_context->cfg();
  opt::DominatorTree& dom_tree = dom_analysis->GetDomTree();
  opt::DominatorTreeNode* header_node = dom_tree.GetTreeNode(loop_header_);

  // The loop predecessor.
  BasicBlock* loop_pred = nullptr;

  auto header_pred = cfg->preds(loop_header_->id());
  for (uint32_t p_id : header_pred) {
    opt::DominatorTreeNode* node = dom_tree.GetTreeNode(p_id);
    if (node && !dom_tree.Dominates(header_node, node)) {
      // The predecessor is not part of the loop, so potential loop preheader.
      if (loop_pred && node->bb_ != loop_pred) {
        // If we saw 2 distinct predecessors that are outside the loop, we don't
        // have a loop preheader.
        return nullptr;
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
  bool is_preheader = true;
  uint32_t loop_header_id = loop_header_->id();
  loop_pred->ForEachSuccessorLabel(
      [&is_preheader, loop_header_id](const uint32_t id) {
        if (id != loop_header_id) is_preheader = false;
      });
  if (is_preheader) return loop_pred;
  return nullptr;
}

LoopDescriptor::LoopDescriptor(const Function* f) { PopulateList(f); }

void LoopDescriptor::PopulateList(const Function* f) {
  IRContext* context = f->GetParent()->context();

  opt::DominatorAnalysis* dom_analysis =
      context->GetDominatorAnalysis(f, *context->cfg());

  loops_.clear();

  // Post-order traversal of the dominator tree to find all the OpLoopMerge
  // instructions.
  opt::DominatorTree& dom_tree = dom_analysis->GetDomTree();
  for (opt::DominatorTreeNode& node :
       ir::make_range(dom_tree.post_begin(), dom_tree.post_end())) {
    Instruction* merge_inst = node.bb_->GetLoopMergeInst();
    if (merge_inst) {
      // The id of the merge basic block of this loop.
      uint32_t merge_bb_id = merge_inst->GetSingleWordOperand(0);

      // The id of the continue basic block of this loop.
      uint32_t continue_bb_id = merge_inst->GetSingleWordOperand(1);

      // The merge target of this loop.
      BasicBlock* merge_bb = context->cfg()->block(merge_bb_id);

      // The continue target of this loop.
      BasicBlock* continue_bb = context->cfg()->block(continue_bb_id);

      // The basic block containing the merge instruction.
      BasicBlock* header_bb = context->get_instr_block(merge_inst);

      // Add the loop to the list of all the loops in the function.
      loops_.emplace_back(MakeUnique<Loop>(context, dom_analysis, header_bb,
                                           continue_bb, merge_bb));
      Loop* current_loop = loops_.back().get();

      // We have a bottom-up construction, so if this loop has nested-loops,
      // they are by construction at the tail of the loop list.
      for (auto itr = loops_.rbegin() + 1; itr != loops_.rend(); ++itr) {
        Loop* previous_loop = itr->get();

        // If the loop already has a parent, then it has been processed.
        if (previous_loop->HasParent()) continue;

        // If the current loop does not dominates the previous loop then it is
        // not nested loop.
        if (!dom_analysis->Dominates(header_bb,
                                     previous_loop->GetHeaderBlock()))
          continue;
        // If the current loop merge dominates the previous loop then it is
        // not nested loop.
        if (dom_analysis->Dominates(merge_bb, previous_loop->GetHeaderBlock()))
          continue;

        current_loop->AddNestedLoop(previous_loop);
      }
      opt::DominatorTreeNode* dom_merge_node = dom_tree.GetTreeNode(merge_bb);
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

void Loop::FindLoopBasicBlocks() {
  loop_basic_blocks_.clear();
  loop_basic_blocks_in_order_.clear();

  opt::DominatorTree& tree = dom_analysis_->GetDomTree();

  // Starting the loop header BasicBlock, traverse the dominator tree until we
  // reach the merge blockand add every node we traverse to the set of blocks
  // which we consider to be the loop.
  auto begin_itr = tree.GetTreeNode(loop_header_)->df_begin();
  for (; begin_itr != tree.end(); ++begin_itr) {
    if (!dom_analysis_->Dominates(loop_merge_, begin_itr->bb_)) {
      loop_basic_blocks_in_order_.push_back(begin_itr->bb_);
      AddBasicBlockToLoop(begin_itr->bb_);
    }
  };
}

ir::BasicBlock* Loop::FindConditionBlock(const ir::Function& function) {
  ir::BasicBlock* condition_block = nullptr;

  opt::DominatorAnalysis* dom_analysis =
      ir_context_->GetDominatorAnalysis(&function, *ir_context_->cfg());
  ir::BasicBlock* bb = dom_analysis->ImmediateDominator(loop_merge_);

  if (!bb) return nullptr;

  if (bb->ctail()->opcode() == SpvOpBranchConditional) {
    condition_block = bb;
  }

  return condition_block;
}

bool Loop::FindNumberOfIterations(const ir::Instruction* induction,
                                  const ir::Instruction* branch_inst,
                                  size_t* iterations) const {
  // From the branch instruction find the branch condition.
  opt::analysis::DefUseManager* def_use_manager =
      ir_context_->get_def_use_mgr();

  // Condition instruction from the OpConditionalBranch.
  ir::Instruction* condition =
      def_use_manager->GetDef(branch_inst->GetSingleWordOperand(0));

  // The right hand side operand of the operation.
  const ir::Instruction* rhs_inst =
      def_use_manager->GetDef(condition->GetSingleWordOperand(3));

  // Exit out if we couldn't resolve the rhs to be a constant integer.
  uint32_t condition_value = 0;
  if (!GetConstant(rhs_inst, &condition_value)) return false;

  // Find the instruction which is stepping through the loop.
  ir::Instruction* step_inst = GetInductionStepOperation(
      induction, loop_header_, ir_context_, dom_analysis_);
  if (!step_inst) return false;

  // The instruction representing the constant value being applied in the step
  // operation.
  const ir::Instruction* step_amount_inst =
      def_use_manager->GetDef(step_inst->GetSingleWordOperand(3));

  uint32_t step_value = 0;
  // Exit out if we couldn't resolve the rhs to be a constant integer.
  if (!GetConstant(step_amount_inst, &step_value)) return false;

  // Find the inital value of the loop and make sure it is a constant integer.
  uint32_t init_value = 0;
  if (!GetInductionInitValue(induction, loop_header_, ir_context_,
                             dom_analysis_, &init_value))
    return false;

  // If iterations is non null then store the value in that.
  if (iterations) {
    *iterations = (condition_value / step_value) - init_value;
  }

  return true;
}

ir::Instruction* Loop::FindInductionVariable(
    const ir::BasicBlock* condition_block) const {
  // Find the branch instruction.
  const ir::Instruction& branch_inst = *condition_block->ctail();

  ir::Instruction* induction = nullptr;
  // Verify that the branch instruction is a conditional branch.
  if (branch_inst.opcode() == SpvOp::SpvOpBranchConditional) {
    // From the branch instruction find the branch condition.
    opt::analysis::DefUseManager* def_use_manager =
        ir_context_->get_def_use_mgr();

    // Find the instruction representing the condition used in the conditional
    // branch.
    ir::Instruction* condition =
        def_use_manager->GetDef(branch_inst.GetSingleWordOperand(0));

    // Ensure that the condition is a less than operation.
    if (condition && condition->opcode() == SpvOp::SpvOpSLessThan) {
      // The left hand side operand of the operation.
      ir::Instruction* variable_inst =
          def_use_manager->GetDef(condition->GetSingleWordOperand(2));

      // Make sure the variable instruction used is a phi.
      if (!variable_inst || variable_inst->opcode() != SpvOpPhi) return nullptr;

      // .
      if (variable_inst->NumOperands() != 6 ||
          variable_inst->GetSingleWordOperand(3) != loop_preheader_->id() ||
          variable_inst->GetSingleWordOperand(5) != loop_continue_->id()) {
        return nullptr;
      }

      // const ir::Instruction* phi_rhs =
      // def_use_manager->GetDef(variable_inst->GetSingleWordOperand(4));

      if (!FindNumberOfIterations(variable_inst, &branch_inst, nullptr))
        return nullptr;
      induction = variable_inst;
    }
  }

  return induction;
}

}  // namespace ir
}  // namespace spvtools
