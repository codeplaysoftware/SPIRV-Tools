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
  // Searchs the IRContext for functions and processes each, moving invairants
  // outside loops within the function where possible
  // Returns true if a change was made to a function within the IRContext
  bool ProcessIRContext();

  // Checks the function for loops, calling ProcessLoop on each one found.
  // Returns true if a change was made to the function, false otherwise.
  bool ProcessFunction(ir::Function* f);

  // Checks for invariants in the loop and attempts to move them to the loops
  // preheader. Works from inner loop to outer when nested loops are found.
  // Returns true if a change was made to the loop, false otherwise.
  bool ProcessLoop(Loop* loop);

  // Returns the preheader of the loop
  ir::BasicBlock* FindPreheader(Loop* loop);

  // Moves the given instruction out of the loop and into the loops preheader
  bool HoistInstructions(Loop* loop, ir::BasicBlock* pre_header_bb,
                        ir::BasicBlock* invariants_bb);

  ir::IRContext* ir_context;
  opt::DominatorAnalysis* dom_analysis;
};

}  // namespace opt
}  // namespace spvtools

#endif  // LIBSPIRV_OPT_LICM_PASS_H_
