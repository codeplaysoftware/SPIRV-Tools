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
#include <memory>
#include <utility>
#include "opt/ir_builder.h"
#include "opt/loop_utils.h"

// Implements loop util unrolling functionality for fully and partially
// unrolling loops. Given a factor it will duplicate the loop that many times,
// appending each one to the end of the old loop and removing backedges, to
// create a new unrolled loop.
//
// 1 - User calls LoopUtils::FullyUnroll or LoopUtils::PartiallyUnroll with a
// loop they wish to unroll. LoopUtils::CanPerformUnroll is used to
// validate that a given loop can be unrolled. That method (alongwith the the
// constructor of loop) checks that the IR is in the expected canonicalised
// format.
//
// 2 - The LoopUtils methods create a LoopUnrollerUtilsImpl object to actually
// perform the unrolling. This implements helper methods to copy the loop basic
// blocks and remap the ids of instructions used inside them.
//
// 3 - The core of LoopUnrollerUtilsImpl is the Unroll method, this method
// actually performs the loop duplication. It does this by creating a
// LoopUnrollState object and then copying the loop as given by the factor
// parameter. The LoopUnrollState object retains the state of the unroller
// between the loop body copies as each iteration needs information on the last
// to adjust the phi induction variable, adjust the OpLoopMerge instruction in
// the main loop header, and change the previous continue block to point to the
// new header and the new continue block to the main loop header.
//
// 4 - If the loop is to be fully unrolled then it is simply closed after step
// 3, with the OpLoopMerge being deleted, the backedge removed, and the
// condition blocks folded.
//
// 5 - If it is being partially unrolled: if the unrolling factor leaves the
// loop with an even number of bodies with respect to the number of loop
// iterations then step 3 is all that is needed. If it is uneven then we need to
// duplicate the loop completely and unroll the duplicated loop to cover the
// "uneven"  part and adjust the first loop to cover only the "even" part. For
// instance if you request an unroll factor of 2 on a loop with 10 iterations
// then copying the body twice would leave you with three bodies in the loop
// while  the loop still iterates over each 4 times. So we make two loops one
// iterating once then a second loop of three iterating 3 times.
//
namespace spvtools {
namespace opt {
namespace {

// This utility class encapsulates some of the state we need to maintain between
// loop unrolls. Specifically it maintains key blocks and the induction variable
// in the current loop duplication step and the blocks from the previous one.
// This is because each step of the unroll needs to use data from both the
// preceding step and the original loop.
struct LoopUnrollState {
  LoopUnrollState()
      : previous_phi_(nullptr),
        previous_continue_block_(nullptr),
        previous_condition_block_(nullptr),
        new_phi(nullptr),
        new_continue_block(nullptr),
        new_condition_block(nullptr),
        new_header_block(nullptr) {}

  // Initialize from the loop descriptor class.
  LoopUnrollState(ir::Instruction* induction, ir::BasicBlock* continue_block,
                  ir::BasicBlock* condition)
      : previous_phi_(induction),
        previous_continue_block_(continue_block),
        previous_condition_block_(condition),
        new_phi(nullptr),
        new_continue_block(nullptr),
        new_condition_block(nullptr),
        new_header_block(nullptr) {}

  // Swap the state so that the new nodes are now the previous nodes.
  void NextIterationState() {
    previous_phi_ = new_phi;
    previous_continue_block_ = new_continue_block;
    previous_condition_block_ = new_condition_block;

    // Clear new nodes.
    new_phi = nullptr;
    new_continue_block = nullptr;
    new_condition_block = nullptr;
    new_header_block = nullptr;

    // Clear new block/instruction maps.
    new_blocks.clear();
    new_inst.clear();
  }

  // The induction variable from the immediately preceding loop body.
  ir::Instruction* previous_phi_;

  // The previous continue block. The backedge will be removed from this and
  // added to the new continue block.
  ir::BasicBlock* previous_continue_block_;

  // The previous condition block. This may be folded to flatten the loop.
  ir::BasicBlock* previous_condition_block_;

  // The new induction variable.
  ir::Instruction* new_phi;

  // The new continue block.
  ir::BasicBlock* new_continue_block;

  // The new condition block.
  ir::BasicBlock* new_condition_block;

  // The new header block.
  ir::BasicBlock* new_header_block;

  // A mapping of new block ids to the original blocks which they were copied
  // from.
  std::unordered_map<uint32_t, ir::BasicBlock*> new_blocks;

  // A mapping of new instruction ids to the instruction ids from which they
  // were copied.
  std::unordered_map<uint32_t, uint32_t> new_inst;
};

// This class implements the actual unrolling. It uses a LoopUnrollState to
// maintain the state of the unrolling inbetween steps.
class LoopUnrollerUtilsImpl {
 public:
  LoopUnrollerUtilsImpl(ir::IRContext* c, ir::Function* function)
      : ir_context_(c),
        function_(*function),
        loop_condition_block_(nullptr),
        loop_induction_variable_(nullptr),
        number_of_loop_iterations_(0) {}

  // Unroll the |loop| by given |factor| by copying the whole body |factor|
  // times. The resulting basicblock structure will remain a loop.
  void PartiallyUnroll(ir::Loop*, size_t factor);

  // If partially unrolling the |loop| would leave the loop with too many bodies
  // for its number of iterations then this method should be used. This method
  // will duplicate the |loop| compltetely, makingthe duplicated loop the
  // successor of the originals merge block. The original loop will have its
  // condition changed to loop over the uneven part and the duplicate will be
  // partially unrolled. The resulting structure will be two loops.
  void PartiallyUnrollUnevenFactor(ir::Loop* loop, size_t factor);

  // Fully unroll the |loop| by copying the full body by the total number of
  // loop iterations, folding all conditions, and removing the backedge from the
  // continue block to the header.
  void FullyUnroll(ir::Loop* loop);

  // Get the ID of the variable in the |phi| paired with |label|.
  uint32_t GetPhiDefID(const ir::Instruction* phi, uint32_t label) const;

  // Close the loop by removing the OpLoopMerge from the |loop| header block and
  // making the backedge point to the merge block.
  void CloseUnrolledLoop(ir::Loop* loop);

  // Remove the OpConditionalBranch instruction inside |conditional_block| used
  // to branch to either exit or continue the loop and replace it with an
  // unconditional OpBranch to block |new_target|.
  void FoldConditionBlock(ir::BasicBlock* condtion_block, uint32_t new_target);

  // Add all blocks_to_add_ to function_ at the |insert_point|.
  void AddBlocksToFunction(const ir::BasicBlock* insert_point);

  void DuplicateLoop(ir::Loop* old_loop, ir::Loop* new_loop);

  inline size_t GetLoopIterationCount() const {
    return number_of_loop_iterations_;
  }

  void Init(ir::Loop* loop);

 private:
  // Remap all the in |basic_block| to new IDs and keep the mapping of new ids
  // to old
  // ids. |loop| is used to identify special loop blocks (header, continue,
  // ect).
  void AssignNewResultIds(ir::BasicBlock* basic_block);

  // Using the map built by AssignNewResultIds, for each instruction in
  // |basic_block| use
  // that map to substitute the IDs used by instructions (in the operands) with
  // the new ids.
  void RemapOperands(ir::BasicBlock* basic_block);

  // Copy the whole body of the loop, all blocks dominated by the |loop| header
  // and not dominated by the |loop| merge. The copied body will be linked to by
  // the old |loop| continue block and the new body will link to the |loop|
  // header via the new continue block. |eliminate_conditions| is used to decid
  // whether or not to fold all the condition blocks other than the last one.
  void CopyBody(ir::Loop* loop, bool eliminate_conditions);

  // Copy a given |block_to_copy| in the |loop| and record the mapping of the
  // old/new ids. |preserve_instructions| determines whether or not the method
  // will modify (other than result_id) instructions which are copied.
  void CopyBasicBlock(ir::Loop* loop, const ir::BasicBlock* block_to_copy,
                      bool preserve_instructions);

  //
  void Unroll(ir::Loop*, size_t);

  // Fills the loop_blocks_inorder_ field with the ordered list of basic blocks
  // as computed by the method ComputeLoopOrderedBlocks.
  void ComputeLoopOrderedBlocks(ir::Loop* loop);

  // A pointer to the IRContext. Used to add/remove instructions and for usedef
  // chains.
  ir::IRContext* ir_context_;

  // A reference the function the loop is within.
  ir::Function& function_;

  // A list of basic blocks to be added to the loop at the end of an unroll
  // step.
  LoopUtils::BasicBlockListTy blocks_to_add_;

  // Maintains the current state of the transform between calls to unroll.
  LoopUnrollState state_;

  // An ordered list containing the loop basic blocks.
  std::vector<ir::BasicBlock*> loop_blocks_inorder_;

  // The block containing the condition check which contains a conditional
  // branch to the merge and continue block.
  ir::BasicBlock* loop_condition_block_;

  // The induction variable of the loop.
  ir::Instruction* loop_induction_variable_;

  // The number of loop iterations that the loop would preform pre-unroll.
  size_t number_of_loop_iterations_;
};

/*
 * Static helper functions.
 */

// Retrieve the index of the OpPhi instruction |phi| which corresponds to the
// incoming |block| id.
static uint32_t GetPhiIndexFromLabel(const ir::BasicBlock* block,
                                     const ir::Instruction* phi) {
  for (uint32_t i = 1; i < phi->NumInOperands(); i += 2) {
    if (block->id() == phi->GetSingleWordInOperand(i)) {
      return i;
    }
  }
  assert(false && "Could not find operand in instruction.");
  return 0;
}

void LoopUnrollerUtilsImpl::Init(ir::Loop* loop) {
  loop_condition_block_ = loop->FindConditionBlock(function_);

  // When we reinit the second loop during PartiallyUnrollUnevenFactor we need
  // to use the cached value from the duplicate step as the dominator tree
  // basded solution, loop->FindConditionBlock, requires all the nodes to be
  // connected up with the correct branches. They won't be at this point.
  if (!loop_condition_block_) {
    loop_condition_block_ = state_.new_condition_block;
  }
  assert(loop_condition_block_);

  loop_induction_variable_ = loop->FindInductionVariable(loop_condition_block_);
  assert(loop_induction_variable_);

  assert(loop->FindNumberOfIterations(loop_induction_variable_,
                                      &*loop_condition_block_->ctail(),
                                      &number_of_loop_iterations_));

  ComputeLoopOrderedBlocks(loop);
}

// This function is used to partially unroll the loop when the factor provided
// would normally lead to an illegal optimization. Instead of just unrolling the
// loop it creates two loops and unrolls one and adjusts the condition on the
// other. The end result being that the new loop pair iterates over the correct
// number of bodies.
void LoopUnrollerUtilsImpl::PartiallyUnrollUnevenFactor(ir::Loop* loop,
                                                        size_t factor) {
  // Create a new merge block for the first loop.
  std::unique_ptr<ir::Instruction> new_label{new ir::Instruction(
      ir_context_, SpvOp::SpvOpLabel, 0, ir_context_->TakeNextId(), {})};
  std::unique_ptr<ir::BasicBlock> new_exit_bb{
      new ir::BasicBlock(std::move(new_label))};

  // Save the id of the block before we move it.
  uint32_t new_merge_id = new_exit_bb->id();

  // Add the block the the list of blocks to add, we want this merge block to be
  // right at the start of the new blocks.
  blocks_to_add_.push_back(std::move(new_exit_bb));
  ir::BasicBlock* new_exit_bb_raw = blocks_to_add_[0].get();
  ir::BasicBlock* original_condition_block = loop_condition_block_;

  // Duplicate the loop, providing access to the blocks of both loops.
  ir::Loop new_loop = *loop;
  DuplicateLoop(loop, &new_loop);

  // Add the blocks to the function.
  AddBlocksToFunction(loop->GetMergeBlock());
  blocks_to_add_.clear();

  InstructionBuilder<ir::IRContext::kAnalysisNone> builder{ir_context_,
                                                           new_exit_bb_raw};
  // Make the first loop branch to the second.
  builder.AddBranch(new_loop.GetHeaderBlock()->id());

  loop_condition_block_ = state_.new_condition_block;
  loop_induction_variable_ = state_.new_phi;

  // Unroll the new loop by the factor with the usual -1 to account for the
  // existing block iteration.
  Unroll(&new_loop, factor - 1u);

  // We need to account for the initial body when calculating the remainder.
  size_t remainder = number_of_loop_iterations_ % (factor + 1u);

  ir::Instruction* new_constant =
      builder.Add32BitUnsignedIntegerConstant(static_cast<uint32_t>(remainder));
  uint32_t constant_id = new_constant->result_id();

  // Add the merge block to the back of the binary.
  blocks_to_add_.push_back(
      std::unique_ptr<ir::BasicBlock>(new_loop.GetMergeBlock()));

  // Add the blocks to the function.
  AddBlocksToFunction(loop->GetMergeBlock());

  // Reset the usedef analysis.
  ir_context_->InvalidateAnalysesExceptFor(
      ir::IRContext::Analysis::kAnalysisNone);
  opt::analysis::DefUseManager* def_use_manager =
      ir_context_->get_def_use_mgr();

  // Update the condition check.
  ir::Instruction& conditional_branch = *original_condition_block->tail();
  ir::Instruction* condition_check =
      def_use_manager->GetDef(conditional_branch.GetSingleWordOperand(0));

  // This should have been checked by the LoopUtils::CanPerformUnroll function
  // before entering this.
  assert(condition_check->opcode() == SpvOpSLessThan);
  condition_check->SetInOperand(1, {constant_id});

  // Update the next phi node. The phi will have a constant value coming in from
  // the preheader block. For the duplicated loop we need to update the constant
  // to be the amount of iterations covered by the first loop and the incoming
  // block to be the first loops new merge block.
  uint32_t phi_incoming_index =
      GetPhiIndexFromLabel(loop->GetPreHeaderBlock(), loop_induction_variable_);
  loop_induction_variable_->SetInOperand(phi_incoming_index - 1, {constant_id});
  loop_induction_variable_->SetInOperand(phi_incoming_index, {new_merge_id});

  ir_context_->InvalidateAnalysesExceptFor(
      ir::IRContext::Analysis::kAnalysisNone);

  ir_context_->ReplaceAllUsesWith(loop->GetMergeBlock()->id(), new_merge_id);
}

// Duplicate the |loop| body |factor| number of times while keeping the loop
// backedge intact.
void LoopUnrollerUtilsImpl::PartiallyUnroll(ir::Loop* loop, size_t factor) {
  Unroll(loop, factor);
  AddBlocksToFunction(loop->GetMergeBlock());
}

// Duplicate the |loop| body |factor| number of times while keeping the loop
// backedge intact.
void LoopUnrollerUtilsImpl::Unroll(ir::Loop* loop, size_t factor) {
  state_ = LoopUnrollState{loop_induction_variable_, loop->GetLatchBlock(),
                           loop_condition_block_};
  for (size_t i = 0; i < factor; ++i) {
    CopyBody(loop, true);
  }

  // The first condition block is perserved until now so it can be copied.
  FoldConditionBlock(loop_condition_block_, 1);

  uint32_t phi_index = GetPhiIndexFromLabel(state_.previous_continue_block_,
                                            state_.previous_phi_);
  uint32_t phi_variable =
      state_.previous_phi_->GetSingleWordInOperand(phi_index - 1);
  uint32_t phi_label = state_.previous_phi_->GetSingleWordInOperand(phi_index);

  ir::Instruction* original_phi = loop_induction_variable_;

  // SetInOperands are offset by two.
  original_phi->SetInOperand(phi_index - 1, {phi_variable});
  original_phi->SetInOperand(phi_index, {phi_label});
}

// Fully unroll the loop by partially unrolling it by the number of loop
// iterations minus one for the body already accounted for.
void LoopUnrollerUtilsImpl::FullyUnroll(ir::Loop* loop) {
  // We unroll the loop by number of iterations - 1, this is due to the fact
  // that a loop already has one iteration.
  Unroll(loop, number_of_loop_iterations_ - 1);

  // When fully unrolling we can eliminate the last condition block.
  FoldConditionBlock(state_.previous_condition_block_, 1);

  // Delete the OpLoopMerge and remove the backedge to the header.
  CloseUnrolledLoop(loop);

  // Add the blocks to the function.
  AddBlocksToFunction(loop->GetMergeBlock());

  // Invalidate all analyses.
  ir_context_->InvalidateAnalysesExceptFor(
      ir::IRContext::Analysis::kAnalysisNone);
}

// Copy a given basic block, give it a new result_id, and store the new block
// and the id mapping in the state.
void LoopUnrollerUtilsImpl::CopyBasicBlock(ir::Loop* loop,
                                           const ir::BasicBlock* itr,
                                           bool preserve_instructions) {
  // Clone the block exactly, including the IDs.
  ir::BasicBlock* basic_block = itr->Clone(ir_context_);

  // Assign each result a new unique ID and keep a mapping of the old ids to
  // the new ones.
  AssignNewResultIds(basic_block);

  // If this is the continue block we are copying.
  if (itr == loop->GetLatchBlock()) {
    // Make the OpLoopMerge point to this block for the continue.
    if (!preserve_instructions) {
      ir::Instruction* merge_inst = loop->GetHeaderBlock()->GetLoopMergeInst();
      merge_inst->SetInOperand(1, {basic_block->id()});
    }

    state_.new_continue_block = basic_block;
  }

  // If this is the header block we are copying.
  if (itr == loop->GetHeaderBlock()) {
    state_.new_header_block = basic_block;

    if (!preserve_instructions) {
      // Remove the loop merge instruction if it exists.
      ir::Instruction* merge_inst = basic_block->GetLoopMergeInst();
      if (merge_inst) ir_context_->KillInst(merge_inst);
    }
  }

  // If this is the condition block we are copying.
  if (itr == loop_condition_block_) {
    state_.new_condition_block = basic_block;
  }

  // Add this block to the list of blocks to add to the function at the end of
  // the unrolling process.
  blocks_to_add_.push_back(std::unique_ptr<ir::BasicBlock>(basic_block));

  // Keep tracking the old block via a map.
  state_.new_blocks[itr->id()] = basic_block;
}

void LoopUnrollerUtilsImpl::CopyBody(ir::Loop* loop,
                                     bool eliminate_conditions) {
  // Copy each basic block in the loop, give them new ids, and save state
  // information.
  for (const ir::BasicBlock* itr : loop_blocks_inorder_) {
    CopyBasicBlock(loop, itr, false);
  }

  // Set the previous continue block to point to the new header.
  ir::Instruction& continue_branch = *state_.previous_continue_block_->tail();
  continue_branch.SetInOperand(0, {state_.new_header_block->id()});

  // As the algorithm copies the original loop blocks exactly, the tail of the
  // latch block on iterations after the first one will be a branch to the new
  // header and not the actual loop header. The last continue block in the loop
  // should always be a backedge to the global header.
  ir::Instruction& new_continue_branch = *state_.new_continue_block->tail();
  new_continue_branch.SetInOperand(0, {loop->GetHeaderBlock()->id()});

  // Update references to the old phi node with the actual variable.
  const ir::Instruction* induction = loop_induction_variable_;
  state_.new_inst[induction->result_id()] =
      GetPhiDefID(state_.previous_phi_, state_.previous_continue_block_->id());

  if (eliminate_conditions &&
      state_.previous_condition_block_ != loop_condition_block_) {
    FoldConditionBlock(state_.previous_condition_block_, 1);
  }

  // Only reference to the header block is the backedge in the latch block,
  // don't change this.
  state_.new_inst[loop->GetHeaderBlock()->id()] = loop->GetHeaderBlock()->id();

  for (auto& pair : state_.new_blocks) {
    RemapOperands(pair.second);
  }

  // Swap the state so the new is now the previous.
  state_.NextIterationState();
}

uint32_t LoopUnrollerUtilsImpl::GetPhiDefID(const ir::Instruction* phi,
                                            uint32_t label) const {
  for (uint32_t operand = 3; operand < phi->NumOperands(); operand += 2) {
    if (phi->GetSingleWordOperand(operand) == label) {
      return phi->GetSingleWordOperand(operand - 1);
    }
  }

  return 0;
}

void LoopUnrollerUtilsImpl::FoldConditionBlock(ir::BasicBlock* condition_block,
                                               uint32_t operand_label) {
  // Remove the old conditional branch to the merge and continue blocks.
  ir::Instruction& old_branch = *condition_block->tail();
  uint32_t new_target = old_branch.GetSingleWordOperand(operand_label);
  ir_context_->KillInst(&old_branch);

  // Add the new unconditional branch to the merge block.
  InstructionBuilder<ir::IRContext::kAnalysisNone> builder{ir_context_,
                                                           condition_block};
  builder.AddBranch(new_target);
}

void LoopUnrollerUtilsImpl::CloseUnrolledLoop(ir::Loop* loop) {
  // Remove the OpLoopMerge instruction from the function.
  ir::Instruction* merge_inst = loop->GetHeaderBlock()->GetLoopMergeInst();
  ir_context_->KillInst(merge_inst);

  // Remove the final backedge to the header and make it point instead to the
  // merge block.
  state_.previous_continue_block_->tail()->SetInOperand(
      0, {loop->GetMergeBlock()->id()});
}

// Uses the first loop to create a copy of the loop with new IDs.
void LoopUnrollerUtilsImpl::DuplicateLoop(ir::Loop* old_loop,
                                          ir::Loop* new_loop) {
  std::vector<ir::BasicBlock*> new_block_order;

  // Copy every block in the old loop.
  for (const ir::BasicBlock* itr : loop_blocks_inorder_) {
    CopyBasicBlock(old_loop, itr, true);
    new_block_order.push_back(blocks_to_add_.back().get());
  }

  new_loop->SetLatchBlock(state_.new_continue_block);
  new_loop->SetHeaderBlock(state_.new_header_block);

  ir::BasicBlock* new_merge = old_loop->GetMergeBlock()->Clone(ir_context_);
  AssignNewResultIds(new_merge);
  state_.new_blocks[old_loop->GetMergeBlock()->id()] = new_merge;
  new_loop->SetMergeBlock(new_merge);
  for (auto& pair : state_.new_blocks) {
    RemapOperands(pair.second);
  }

  loop_blocks_inorder_ = std::move(new_block_order);
}

void LoopUnrollerUtilsImpl::AddBlocksToFunction(
    const ir::BasicBlock* insert_point) {
  for (auto basic_block_iterator = function_.begin();
       basic_block_iterator != function_.end(); ++basic_block_iterator) {
    if (basic_block_iterator->id() == insert_point->id()) {
      basic_block_iterator.InsertBefore(&blocks_to_add_);
      return;
    }
  }

  assert(
      false &&
      "Could not add basic blocks to function as insert point was not found.");
}

// Assign all result_ids in |basic_block| instructions to new IDs and preserve
// the mapping of new ids to old ones.
void LoopUnrollerUtilsImpl::AssignNewResultIds(ir::BasicBlock* basic_block) {
  // Label instructions aren't covered by normal traversal of the
  // instructions.
  uint32_t new_label_id = ir_context_->TakeNextId();

  // Assign a new id to the label.
  state_.new_inst[basic_block->GetLabelInst()->result_id()] = new_label_id;
  basic_block->GetLabelInst()->SetResultId(new_label_id);

  for (ir::Instruction& inst : *basic_block) {
    uint32_t old_id = inst.result_id();

    // Ignore stores etc.
    if (old_id == 0) {
      continue;
    }

    // Give the instruction a new id.
    inst.SetResultId(ir_context_->TakeNextId());

    // Save the mapping of old_id -> new_id.
    state_.new_inst[old_id] = inst.result_id();

    // Check if this instruction is the induction variable.
    if (loop_induction_variable_->result_id() == old_id) {
      // Save a pointer to the new copy of it.
      state_.new_phi = &inst;
    }
  }
}

// For all instructions in |basic_block| check if the operands used are from a
// copied
// instruction and if so swap out the operand for the copy of it.
void LoopUnrollerUtilsImpl::RemapOperands(ir::BasicBlock* basic_block) {
  for (ir::Instruction& inst : *basic_block) {
    auto remap_operands_to_new_ids = [this](uint32_t* id) {
      auto itr = state_.new_inst.find(*id);
      if (itr != state_.new_inst.end()) {
        *id = itr->second;
      }
    };

    inst.ForEachInId(remap_operands_to_new_ids);
  }
}

// Generate the ordered list of basic blocks in the |loop| and cache it for
// later use.
void LoopUnrollerUtilsImpl::ComputeLoopOrderedBlocks(ir::Loop* loop) {
  loop_blocks_inorder_.clear();

  opt::DominatorAnalysis* analysis =
      ir_context_->GetDominatorAnalysis(&function_, *ir_context_->cfg());
  opt::DominatorTree& tree = analysis->GetDomTree();

  // Starting the loop header BasicBlock, traverse the dominator tree until we
  // reach the merge blockand add every node we traverse to the set of blocks
  // which we consider to be the loop.
  auto begin_itr = tree.GetTreeNode(loop->GetHeaderBlock())->df_begin();
  const ir::BasicBlock* merge = loop->GetMergeBlock();
  auto func = [merge, &tree, this](DominatorTreeNode* node) {
    if (!tree.Dominates(merge->id(), node->id())) {
      this->loop_blocks_inorder_.push_back(node->bb_);
      return true;
    }
    return false;
  };

  tree.VisitChildrenIf(func, begin_itr);
}

/*
 * End LoopUtilsImpl.
 */

}  // namespace

/*
 *
 *  Begin Utils.
 *
 * */

bool LoopUtils::CanPerformUnroll(ir::Loop* loop) {
  // The loop is expected to be in structured order.
  if (!loop->GetHeaderBlock()->GetMergeInst()) {
    return false;
  }

  // Find check the loop has a condition we can find and evaluate.
  const ir::BasicBlock* condition = loop->FindConditionBlock(function_);
  if (!condition) return false;

  // Check that we can find and process the induction variable.
  const ir::Instruction* induction = loop->FindInductionVariable(condition);
  if (!induction || induction->opcode() != SpvOpPhi) return false;

  // Check that we can find the number of loop iterations.
  if (!loop->FindNumberOfIterations(induction, &*condition->ctail(), nullptr))
    return false;

  // Make sure the continue block is a unconditional branch to the header
  // block.
  const ir::Instruction& branch = *loop->GetLatchBlock()->ctail();
  bool branching_assumtion =
      branch.opcode() == SpvOpBranch &&
      branch.GetSingleWordInOperand(0) == loop->GetHeaderBlock()->id();
  if (!branching_assumtion) {
    return false;
  }

  // Make sure the induction is the only phi instruction we have in the loop
  // header.
  for (const ir::Instruction& inst : *loop->GetHeaderBlock()) {
    if (inst.opcode() == SpvOpPhi &&
        inst.result_id() != induction->result_id()) {
      return false;
    }
  }

  return true;
}

bool LoopUtils::PartiallyUnroll(ir::Loop* loop, size_t factor) {
  if (factor == 0 || !CanPerformUnroll(loop)) return false;

  // Create the unroller utility.
  LoopUnrollerUtilsImpl unroller{ir_context_, &function_};
  unroller.Init(loop);

  // If the unrolling factor is larger than or the same size as the loop just
  // fully unroll the loop.
  if (factor >= unroller.GetLoopIterationCount() - 1) {
    unroller.FullyUnroll(loop);
    return true;
  }

  // If the loop unrolling factor is an uneven number of iterations we need to
  // let run the loop for the uneven part then let it branch into the unrolled
  // remaining part. We add add one when calucating the remainder to take into
  // account the one iteration already in the loop.
  if (unroller.GetLoopIterationCount() % (factor + 1u) != 0) {
    unroller.PartiallyUnrollUnevenFactor(loop, factor);
  } else {
    unroller.PartiallyUnroll(loop, factor);
  }
  return true;
}

bool LoopUtils::FullyUnroll(ir::Loop* loop) {
  if (!CanPerformUnroll(loop)) return false;

  LoopUnrollerUtilsImpl unroller{ir_context_, &function_};

  unroller.Init(loop);
  unroller.FullyUnroll(loop);

  return true;
}

/*
 *
 * Begin Pass.
 *
 */

Pass::Status LoopUnroller::Process(ir::IRContext* c) {
  context_ = c;
  bool changed = false;
  for (ir::Function& f : *c->module()) {
    LoopUtils loop_utils{&f, c};

    for (auto& loop : loop_utils.GetLoopDescriptor()) {
      if (!loop.HasUnrollLoopControl() || !loop_utils.CanPerformUnroll(&loop)) {
        continue;
      }

      loop_utils.FullyUnroll(&loop);
      changed = true;
    }
  }

  if (changed) return Pass::Status::SuccessWithChange;
  return Pass::Status::SuccessWithoutChange;
}

}  // namespace opt
}  // namespace spvtools
