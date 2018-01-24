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
  bool ProcessFunction(ir::Function* f);

  // Checks for invariants in the loop and attempts to move them to the loops
  // preheader. Works from inner loop to outer when nested loops are found.
  // Returns true if a change was made to the loop, false otherwise.
  bool ProcessLoop(ir::Loop& loop, ir::Function* f);

  // Gathers all instructions in the loop whos opcodes do not have side effects
  void GatherAllLoopInstructions(ir::Loop& loop,
                                 std::vector<ir::Instruction*>* instructions);

  // Move the instruction to the given BasicBlock
  // This method will update the instruction to block mapping for the context
  void HoistInstruction(ir::BasicBlock* pre_header_bb, ir::Instruction* inst);

  // Returns true if the given opcode is known to have side effects.
  bool DoesOpcodeHaveSideEffects(SpvOp opcode);

  // Returns true if all operands of inst are in basic blocks not contained in
  // loop
  bool AllOperandsOutsideLoop(ir::Loop& loop, ir::Instruction* inst);

  // Iterates over the instructions list checking each instruction to see if all
  // of its operands are outside the loop. If so, moves the instruction to a
  // queue. Once the list has been iterated over, hoists each inst in the queue
  // outside of the loop.
  // This cycle continues until a full iteration over the instructions list
  // results in no instructions being queued.
  bool ProcessInstructionList(ir::Loop& loop,
                              std::vector<ir::Instruction*>* instructions);

 private:
  ir::IRContext* ir_context;
};

}  // namespace opt
}  // namespace spvtools

#endif  // LIBSPIRV_OPT_LICM_PASS_H_
