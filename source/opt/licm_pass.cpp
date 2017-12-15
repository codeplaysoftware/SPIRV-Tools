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

#include "licm_pass.h"
#include "cfg.h"
#include "module.h"

#include "pass.h"

namespace spvtools {
namespace opt {

LICMPass::LICMPass(){};

Pass::Status LICMPass::Process(ir::IRContext* context) {
  bool modified = false;

  if (context != nullptr) {
    ir_context = context;
    modified = ProcessIRContext();
  }

  return modified ? Status::SuccessWithChange : Status::SuccessWithoutChange;
}

bool LICMPass::ProcessIRContext() {
  bool modified = false;
  ir::Module* module = ir_context->module();

  // Process each function in the module
  for (ir::Function& f : *module) {
    modified |= ProcessFunction(&f);
  }
  return modified;
}

bool LICMPass::ProcessFunction(ir::Function* f) {
  bool modified = false;
  LoopDescriptor loop_descriptor{f};

  // TODO(Alexander: remove this debug dom_analysis stuff)
  ir::CFG cfg(ir_context->module());
  dom_analysis = ir_context->GetDominatorAnalysis(f, cfg);

  dom_analysis->DumpAsDot(std::cout);

  // Process each loop in the function
  for (size_t i = 0; i < loop_descriptor.NumLoops(); ++i) {
    modified |= ProcessLoop(&loop_descriptor.GetLoop(i));
  }
  return modified;
}

bool LICMPass::ProcessLoop(Loop* loop) {
  // Process all nested loops first
  if (loop->HasNestedLoops()) {
    auto nested_loops = loop->GetNestedLoops();
    for (auto it = nested_loops.begin(); it != nested_loops.end(); ++it) {
      ProcessLoop(*it);
    }
  }

  // Search all BB in this loop, and not in a nested loop, for invariants,
  // starting at the first bb after the header and stopping when we reach the
  // header.
  std::vector<ir::Instruction*> invariants = {};
  std::vector<ir::BasicBlock*> valid_blocks = FindValidBasicBlocks(loop);
  for (auto bb_it = valid_blocks.begin(); bb_it != valid_blocks.end();
       ++bb_it) {
    for (auto inst_it = (*bb_it)->begin(); inst_it != (*bb_it)->end();
         ++inst_it) {
      // TODO(Alexander: Add appropriate conditions to only test OpCodes that
      // can be invariant)
      if (inst_it->result_id() == 0) continue;
      if (loop->IsLoopInvariant(&(*inst_it))) {
        invariants.push_back(&(*inst_it));
      }
    }
  }

  // If we found any invariants, process them
  if (invariants.size() > 0) {
    // Create a new BB to hold all found invariants
    uint32_t new_bb_result_id = ir_context->TakeNextUniqueId();
    ir::BasicBlock invariants_bb(std::unique_ptr<ir::Instruction>((
        new ir::Instruction(ir_context, SpvOpLabel, 0, new_bb_result_id, {}))));

    // Add all invariant instructions to the new bb
    for (auto invariant_it = invariants.begin();
         invariant_it != invariants.end(); ++invariant_it) {
      invariants_bb.AddInstruction(
          std::unique_ptr<ir::Instruction>(*invariant_it));
    }

    // Remove all invariant instructions from the existing loop
    for (auto invariant_it = invariants.begin();
         invariant_it != invariants.end(); ++invariant_it) {
      // Remove the instruction once we've added it to our bb
      ir_context->KillInst(*invariant_it);
    }

    ir::BasicBlock* pre_header = loop->GetPreheader();
    // Insert the new BB of invariants between the loop header and the previous
    // BB
    return HoistInstructions(pre_header, &invariants_bb);
  }

  // Didn't find any invariants
  return false;
}

bool LICMPass::HoistInstructions(ir::BasicBlock* pre_header_bb,
                                 ir::BasicBlock* invariants_bb) {
  if (pre_header_bb == nullptr || invariants_bb == nullptr) {
    return false;
  }
  // TODO(Alexander: Change this to insert the new bb between the preheader and
  // header, rather than inserting into the preheader)
  pre_header_bb->AddInstructions(invariants_bb);
  return true;
}

std::vector<ir::BasicBlock*> LICMPass::FindValidBasicBlocks(Loop* loop) {
  std::vector<ir::BasicBlock*> blocks = {};
  std::vector<ir::BasicBlock*> nested_blocks = FindAllNestedBasicBlocks(loop);

  opt::DominatorTree& tree = dom_analysis->GetDomTree();

  // Find every basic block in the loop, excluding the header, merge, and blocks
  // belonging to a nested loop
  auto begin_it = tree.get_iterator(loop->GetLoopHeader());
  for (; begin_it != tree.end(); ++begin_it) {
    ir::BasicBlock* cur_block = begin_it->bb_;
    if (dom_analysis->Dominates(loop->GetMergeBB(), cur_block)) break;

    // Check block is not nested within another loop
    for (auto nested_it = nested_blocks.begin();
         nested_it != nested_blocks.end(); ++nested_it) {
      if (cur_block == *nested_it) break;
    }

    blocks.push_back(cur_block);
  }
  return blocks;
}

std::vector<ir::BasicBlock*> LICMPass::FindAllNestedBasicBlocks(Loop* loop) {
  std::vector<ir::BasicBlock*> blocks = {};

  opt::DominatorTree& tree = dom_analysis->GetDomTree();

  if (loop->HasNestedLoops()) {
    std::vector<Loop*> nested_loops = loop->GetNestedLoops();

    // Go through each nested loop
    for (auto loop_it = nested_loops.begin(); loop_it != nested_loops.end();
         ++loop_it) {
      // Test the blocks of the nested loop against the dominator tree
      auto tree_it = tree.get_iterator((*loop_it)->GetLoopHeader());
      for (; tree_it != tree.end(); ++tree_it) {
        if (dom_analysis->Dominates((*loop_it)->GetMergeBB(), tree_it->bb_))
          break;
        blocks.push_back(tree_it->bb_);
      }

      // Add the header and merge blocks, as they won't be caught in the above
      // loop
      blocks.push_back((*loop_it)->GetLoopHeader());
      blocks.push_back((*loop_it)->GetMergeBB());
    }
  }

  return blocks;
}

}  // namespace opt
}  // namespace spvtools
