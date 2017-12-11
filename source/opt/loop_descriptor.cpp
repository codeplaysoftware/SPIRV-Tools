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

#include <iostream>
#include <vector>

#include "opt/loop_descriptor.h"
#include "opt/make_unique.h"

namespace spvtools {
namespace opt {

LoopDescriptor::LoopDescriptor(const ir::Function* f) { PopulateList(f); }

void LoopDescriptor::PopulateList(const ir::Function* f) {
  ir::IRContext* context = f->GetParent()->context();

  opt::DominatorAnalysis* dom_analysis =
      context->GetDominatorAnalysis(f, *context->cfg());

  std::vector<ir::Instruction*> loop_merge_inst;

  // Traverse the tree and apply the above functor to find all the OpLoopMerge
  // instructions. Instructions will be in domination order of BasicBlocks.
  // However, this does not mean that dominance is implied by the order of
  // loop_merge_inst you still need to check dominance between each block
  // manually.
  const DominatorTree& dom_tree = dom_analysis->GetDomTree();
  // The root node of the dominator tree is a pseudo-block, ignore it.
  for (DominatorTree::const_iterator node = ++dom_tree.begin();
       node != dom_tree.end(); ++node) {
    ir::Instruction* merge_inst = node->bb_->GetLoopMergeInst();
    if (merge_inst) {
      loop_merge_inst.push_back(merge_inst);
    }
  }

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
    loops_.push_back(MakeUnique<Loop>(start_bb, continue_bb, merge_bb));

    // If this is the first loop don't check for dominating nesting loop.
    // Otherwise, move through the loops in reverse order to check if this is a
    // nested loop. If it isn't a nested loop this for will exit on the first
    // iteration.
    for (auto itr = loops_.rbegin() + 1; itr != loops_.rend(); ++itr) {
      Loop* previous_loop = itr->get();

      // If this loop is dominated by the entry of the previous loop it could be
      // a nested loop of that loop or a nested loop of a parent of that loop.
      // Otherwise it's not nested at all.
      if (!dom_analysis->Dominates(previous_loop->GetLoopHeader(), start_bb))
        break;

      // If this loop is dominated by the merge block of the previous loop it's
      // a nested loop of the parent of the previous loop. Otherwise it's just a
      // nested loop of the parent.
      if (dom_analysis->Dominates(previous_loop->GetMergeBB(), start_bb)) {
        continue;
      } else {
        previous_loop->AddNestedLoop(loops_.back().get());
        loops_.back()->SetParent(previous_loop);
        break;
      }
    }
  }
}

}  // namespace opt
}  // namespace spvtools
