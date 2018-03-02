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
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ir_builder.h"
#include "ir_context.h"
#include "loop_descriptor.h"
#include "loop_utils.h"

namespace spvtools {
namespace opt {

namespace {

// Utility class to perform the actual peeling of a given loop.
class LoopPeeling {
 public:
  LoopPeeling(LoopUtils* loop_utils)
      : context_(loop_utils->GetContext()),
        loop_(loop_utils->GetLoop()),
        loop_utils_(loop_utils),
        extra_iv_(nullptr) {}

  void DuplicateLoop() {
    ir::CFG& cfg = *context_->cfg();

    // Fixme: add assertion for legality.
    assert(loop_->GetMergeBlock());
    assert(loop_->IsLCSSA());
    assert(cfg.preds(loop_->GetMergeBlock()->id()).size() > 1 &&
           "This loops have breaks.");

    LoopUtils::LoopCloningResult clone_results;

    std::vector<ir::BasicBlock*> ordered_loop_blocks;
    ir::BasicBlock* pre_header = loop_->GetOrCreatePreHeaderBlock();

    loop_->ComputeLoopStructuredOrder(&ordered_loop_blocks);

    new_loop_ = loop_utils_->CloneLoop(&clone_results, ordered_loop_blocks);

    // FIXME: fix the cfg.
    // FIXME: fix the dominator tree

    // Add the basic block to the function.
    ir::Function::iterator it =
        loop_utils_->GetFunction()->FindBlock(pre_header->id());
    assert(it != loop_utils_->GetFunction()->end());
    loop_utils_->GetFunction()->AddBasicBlocks(
        clone_results.cloned_bb_.begin(), clone_results.cloned_bb_.end(), it);
    // Make the |loop_|'s preheader the |new_loop| one.
    ir::BasicBlock* clonedHeader = new_loop_->GetHeaderBlock();
    pre_header->ForEachSuccessorLabel(
        [clonedHeader](uint32_t* succ) { *succ = clonedHeader->id(); });

    // When cloning the loop, we didn't cloned the merge block, so currently
    // |new_loop| shares the same block as |loop_|.
    // We mutate all branches form |new_loop| block to |loop_|'s merge into a
    // branch to |loop_|'s header (so header will also be the merge of
    // |new_loop|).
    for (uint32_t pred_id : cfg.preds(loop_->GetMergeBlock()->id())) {
      ir::BasicBlock* bb = clone_results.old_to_new_bb_[pred_id];
      bb->ForEachSuccessorLabel([this](uint32_t* succ) {
        if (*succ == loop_->GetMergeBlock()->id())
          *succ = loop_->GetHeaderBlock()->id();
      });
    }

    ConnectIterators(clone_results);
  }

  // Insert an induction variable into the first loop as a simplified counter.
  void InsertIterator(ir::Instruction* factor) {
    analysis::Type* factor_type =
        context_->get_type_mgr()->GetType(factor->type_id());
    assert(factor_type->kind() == analysis::Type::kInteger);
    analysis::Integer* int_type = factor_type->AsInteger();
    assert(int_type->width() == 32);

    InstructionBuilder builder(context_, &*GetBeforeLoop()->GetLatchBlock(),
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

    builder.SetInsertPoint(&*GetBeforeLoop()->GetHeaderBlock()->begin());

    extra_iv_ = builder.AddPhi(
        factor->type_id(),
        {builder.Add32BitConstantInteger<uint32_t>(0, int_type->IsSigned())
             ->result_id(),
         GetBeforeLoop()->GetPreHeaderBlock()->id(), iv_inc->result_id(),
         GetBeforeLoop()->GetLatchBlock()->id()});
    // Connect everything.
    iv_inc->SetInOperand(0, {extra_iv_->result_id()});
  }

  ir::Loop* GetBeforeLoop() { return new_loop_; }
  ir::Loop* GetAfterLoop() { return loop_; }
  ir::Instruction* GetExtraInductionVariable() { return extra_iv_; }

 private:
  ir::IRContext* context_;
  // The original loop.
  ir::Loop* loop_;
  // Peeled loop.
  ir::Loop* new_loop_;
  LoopUtils* loop_utils_;

  ir::Instruction* extra_iv_;

  // Connects iterating values so that loop like
  // int z = 0;
  // for (int i = 0; i++ < M; i += cst1) {
  //   if (cond)
  //     z += cst2;
  // }
  //
  // Becomes:
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
  //
  // That basically means taking as initializer for the second loops iterators
  // the phi nodes or the value involved into the exit condition of the first
  // loop.
  // We have 3 main cases:
  // - The iterator also escape the loop (the last value is used outside the
  // loop), because the loop is LCSSA, its only use is in the merge block;
  // - The iterator is used in the exit condition;
  // - The iterator is not used in the exit condition.
  void ConnectIterators(const LoopUtils::LoopCloningResult& clone_results) {
    // std::unordered_map<uint32_t, uint32_t> phi_to_init;
    // ir::BasicBlock* merge = loop_->GetMergeBlock();
    // analysis::DefUseManager* def_use_mgr = context_->get_def_use_mgr();

    // merge->ForEachPhiInst([&clone_results, &phi_to_init, def_use_mgr,
    // &clone_results, this](Instruction* phi) {
    //     uint32_t value_id = phi->GetSingleWordInOperand(i);
    //     ir::Instruction* value = def_use_mgr->GetDef(value_id);
    //     if (context_->get_instr_block(value) == loop_->GetMergeBlock()) {
    //       phi_to_init[value->id()] = clone_results.value_map_[value->id()];
    //     }
    //   });
    ir::BasicBlock* header = loop_->GetHeaderBlock();
    header->ForEachPhiInst([&clone_results, this](ir::Instruction* phi) {
      for (uint32_t i = 0; i < phi->NumInOperands(); i += 2) {
        uint32_t pred_id = phi->GetSingleWordInOperand(i + 1);
        if (!loop_->IsInsideLoop(pred_id)) {
          // FIXME: temporary measure ...
          phi->SetInOperand(
              i, {clone_results.value_map_.at(phi->GetSingleWordInOperand(i))});
        }
      }
    });
  }
};

}  // namespace

void LoopUtils::PeelBefore(ir::Instruction* factor) {
  ir::CFG& cfg = *context_->cfg();
  LoopPeeling loop_peeler(this);
  loop_peeler.DuplicateLoop();

  loop_peeler.InsertIterator(factor);
  ir::Instruction* iv = loop_peeler.GetExtraInductionVariable();

  uint32_t condition_block_id = 0;
  for (uint32_t id :
       cfg.preds(loop_peeler.GetAfterLoop()->GetHeaderBlock()->id())) {
    if (loop_peeler.GetAfterLoop()->IsInsideLoop(id)) {
      condition_block_id = id;
    }
  }
  assert(condition_block_id != 0 && "2nd loop in improperly connected");

  analysis::Type* factor_type =
    context_->get_type_mgr()->GetType(factor->type_id());
  assert(factor_type->kind() == analysis::Type::kInteger);
  analysis::Integer* int_type = factor_type->AsInteger();

  ir::BasicBlock* condition_block = cfg.block(condition_block_id);
  assert(condition_block->terminator()->opcode() == SpvOpBranchConditional);
  InstructionBuilder builder(context_, &*condition_block->tail(),
                             ir::IRContext::kAnalysisDefUse |
                                 ir::IRContext::kAnalysisInstrToBlockMapping);
  // check that stuff branch accordingly to the check.
  // Build the following check: iv < factor
  condition_block->terminator()->SetInOperand(
      0, {builder.AddLessThan(int_type, iv->result_id(), factor->result_id())
              ->result_id()});
}

void LoopUtils::PeelAfter(ir::Instruction* factor,
                          ir::Instruction* iteration_count) {
  ir::CFG& cfg = *context_->cfg();
  LoopPeeling loop_peeler(this);
  loop_peeler.DuplicateLoop();

  loop_peeler.InsertIterator(factor);
  ir::Instruction* iv = loop_peeler.GetExtraInductionVariable();

  uint32_t condition_block_id = 0;
  for (uint32_t id :
       cfg.preds(loop_peeler.GetAfterLoop()->GetHeaderBlock()->id())) {
    if (loop_peeler.GetAfterLoop()->IsInsideLoop(id)) {
      condition_block_id = id;
    }
  }
  assert(condition_block_id != 0 && "2nd loop in improperly connected");

  analysis::Type* factor_type =
    context_->get_type_mgr()->GetType(factor->type_id());
  assert(factor_type->kind() == analysis::Type::kInteger);
  analysis::Integer* int_type = factor_type->AsInteger();

  ir::BasicBlock* condition_block = cfg.block(condition_block_id);
  assert(condition_block->terminator()->opcode() == SpvOpBranchConditional);
  InstructionBuilder builder(context_, &*condition_block->tail(),
                             ir::IRContext::kAnalysisDefUse |
                                 ir::IRContext::kAnalysisInstrToBlockMapping);
  // Check that stuff branch accordingly to the check.
  // Build the following check: iv + factor < iteration_count (do the add to
  // avoid any issues with unsigned)
  condition_block->terminator()->SetInOperand(
      0,
      {builder
           .AddLessThan(int_type, builder
                                      .AddIAdd(iv->type_id(), iv->result_id(),
                                               factor->result_id())
                                      ->result_id(),
                        iteration_count->result_id())
           ->result_id()});
}

}  // namespace opt
}  // namespace spvtools
