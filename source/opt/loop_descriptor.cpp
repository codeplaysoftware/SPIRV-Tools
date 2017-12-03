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
    loops_.push_back({is_nested, start_bb, continue_bb, merge_bb});

    // If it's nested add a reference to it to the parent loop.
    if (is_nested) {
      assert(parent_loop_index != -1);
      loops_[parent_loop_index].AddNestedLoop(&loops_.back());
    }
  }
}

}  // opt
}  // spvtools
