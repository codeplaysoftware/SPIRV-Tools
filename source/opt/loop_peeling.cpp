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

void LoopUtils::PeelBefore(size_t /*factor*/) {
// Fixme: add assertion for legality.
  assert(loop_->GetMergeBlock());

  ir::CFG& cfg = *context_->cfg();

  LoopCloningResult clone_result;

  std::vector<ir::BasicBlock*> ordered_loop_blocks;
  ir::BasicBlock* pre_header = loop_->GetOrCreatePreHeaderBlock();

  loop_->ComputeLoopStructuredOrder(&ordered_loop_blocks);

  ir::Loop* new_loop = CloneLoop(&clone_result, ordered_loop_blocks);

  // Add the basic block to the function.
  ir::Function::iterator it = function_.FindBlock(pre_header->id());
  assert(it != function_.end());
  function_.AddBasicBlocks(clone_result.cloned_bb_.begin(),
                           clone_result.cloned_bb_.end(), it);
  // Make the |loop_|'s preheader the |new_loop| one.
  ir::BasicBlock* clonedHeader = new_loop->GetHeaderBlock();
  pre_header->ForEachSuccessorLabel(
      [clonedHeader](uint32_t* succ) { *succ = clonedHeader->id(); });

  // When cloning the loop, we didn't cloned the merge block, so currently
  // |new_loop| shares the same block as |loop_|.
  // We mutate all branches form |new_loop| block to |loop_|'s merge into a
  // branch to |loop_|'s header (so header will also be the merge of
  // |new_loop|).
  for (uint32_t pred_id : cfg.preds(loop_->GetMergeBlock()->id())) {
    ir::BasicBlock* bb = clone_result.old_to_new_bb_[pred_id];
    bb->ForEachSuccessorLabel([this](uint32_t* succ) {
        if (*succ == loop_->GetMergeBlock()->id())
          *succ = loop_->GetHeaderBlock()->id();
      });
  }
}

void LoopUtils::PeelAfter(size_t /*factor*/) {
// Fixme: add assertion for legality.

}

}  // namespace opt
}  // namespace spvtools
