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

#ifndef LIBSPIRV_OPT_LICM_PASS_H_
#define LIBSPIRV_OPT_LICM_PASS_H_

#include "loop_descriptor.h"
#include "opt/basic_block.h"
#include "opt/instruction.h"
#include "pass.h"

namespace spvtools {
namespace opt {

class LICMPass : public Pass {
 public:
  LICMPass();

  const char* name() const override { return "licm"; }
  Status Process(ir::IRContext*) override;

 private:
  // Searches the IRContext for functions and processes each, moving invairants
  // outside loops within the function where possible
  // Returns true if a change was made to a function within the IRContext
  bool ProcessIRContext();

  // Checks the function for loops, calling ProcessLoop on each one found.
  // Returns true if a change was made to the function, false otherwise.
  bool ProcessFunction(ir::Function* f, ir::CFG& cfg);

  // Checks for invariants in the loop and attempts to move them to the loops
  // preheader. Works from inner loop to outer when nested loops are found.
  // Returns true if a change was made to the loop, false otherwise.
  bool ProcessLoop(ir::Loop& loop, ir::Function* f);

  // Finds all basic blocks in the between the header and merge blocks of the
  // loop, not contained in a nested loop
  std::vector<ir::BasicBlock*> FindValidBasicBlocks(ir::Loop& loop);

  // Finds all basic blocks in the loop, including the header and blocks inside
  // nested loops
  std::vector<ir::BasicBlock*> FindAllLoopBlocks(ir::Loop* loop);

  // Finds and returns a vector of all basic blocks in nested loops
  std::vector<ir::BasicBlock*> FindAllNestedBasicBlocks(ir::Loop& loop);

  // Moves the given basic block out of the loop and into the loops
  // preheader
  bool HoistInstructions(ir::BasicBlock* pre_header_bb,
                         ir::InstructionList* invariants_list);

  // Finds all invariants in the loop and pushes them to invariants_list
  // Returns true if any invariants were found
  bool FindLoopInvariants(ir::Loop& loop, ir::InstructionList* invariants_list);

  // Tests if an individual instruction is invariant
  bool IsInvariant(ir::Loop& loop,
                   std::unordered_map<ir::Instruction*, bool>* invariants_map,
                   ir::Instruction* inst, std::vector<ir::Instruction*>*,
                   const uint32_t ignore_id);

  ir::IRContext* ir_context;
  opt::DominatorAnalysis* dom_analysis;
};

}  // namespace opt
}  // namespace spvtools

#endif  // LIBSPIRV_OPT_LICM_PASS_H_
