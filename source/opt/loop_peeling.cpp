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
        loop_utils_(loop_utils) {}

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

    new_loop_ =
        loop_utils_->CloneLoop(&clone_results, ordered_loop_blocks);

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
  }

 private:
  ir::IRContext* context_;
  // The original loop.
  ir::Loop* loop_;
  // Peeled loop.
  ir::Loop* new_loop_;
  LoopUtils* loop_utils_;

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
  // the phi nodes or the value involved into the exit condition of the first loop.
  // We have 3 main cases:
  // - The iterator also escape the loop (the last value is used outside the loop), because the loop is LCSSA, its only use is in the merge block;
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

void LoopUtils::PeelBefore(size_t /*factor*/) {
  LoopPeeling loop_peeler(this);
  loop_peeler.DuplicateLoop();
}

void LoopUtils::PeelAfter(size_t /*factor*/) {
// Fixme: add assertion for legality.

}

}  // namespace opt
}  // namespace spvtools
