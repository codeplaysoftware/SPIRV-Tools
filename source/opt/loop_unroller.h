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

#ifndef LIBSPIRV_OPT_LOOP_UNROLLER_H_
#define LIBSPIRV_OPT_LOOP_UNROLLER_H_
#include <list>
#include "opt/loop_descriptor.h"
#include "pass.h"

namespace spvtools {
namespace opt {

class LoopUtils {
 public:
  LoopUtils(ir::Function& function, ir::IRContext* context)
      : function_(function),
        ir_context_(context),
        loop_descriptor_(&function_) {}

  void InsertLoopClosedSSA();

  ir::BasicBlock* CopyLoop(ir::Loop& loop, ir::BasicBlock* preheader);

  void CopyBody(ir::Loop& loop, int, bool eliminate_conditions);

  ir::Loop DuplicateLoop(ir::Loop& loop);
  bool PartiallyUnroll(ir::Loop& loop, int factor);

  bool FullyUnroll(ir::Loop& loop);

  ir::LoopDescriptor& GetLoopDescriptor() { return loop_descriptor_; }

  void RemoveLoopFromFunction(ir::Loop& loop, ir::BasicBlock* preheader);

  bool CanEliminateConditionBlocks(ir::Loop& loop) const;

  void FoldConditionBlock(ir::BasicBlock* condtion_block, uint32_t new_target);

  bool CanPerformPartialUnroll(ir::Loop& loop);

 private:
  ir::Function& function_;
  ir::IRContext* ir_context_;
  ir::LoopDescriptor loop_descriptor_;

  ir::Instruction* previous_phi_;
  ir::BasicBlock* previous_continue_block_;
  ir::BasicBlock* previous_condition_block_;

  std::vector<std::unique_ptr<ir::BasicBlock>> blocks_to_add_;

  ir::Instruction* RemapResultIDs(ir::Loop&, ir::BasicBlock* BB,
                                  std::map<uint32_t, uint32_t>& new_inst) const;

  void RemapOperands(ir::BasicBlock* BB,
                     std::map<uint32_t, uint32_t>& new_inst) const;

  uint32_t GetPhiVariableID(const ir::Instruction* phi, uint32_t label) const;

  void PartiallyUnrollImpl(ir::Loop& loop, int factor);
  void PartiallyUnrollImpl(ir::Loop& loop, int factor,
                           ir::Instruction* induction,
                           ir::BasicBlock* initial_continue_block,
                           ir::BasicBlock* initial_condition);

  void AddBlocksToFunction(const ir::BasicBlock* insert_point);
  void CloseUnrolledLoop(ir::Loop& loop);

  bool PartiallyUnrollUnevenFactor(ir::Loop& loop, int factor);
};

class LoopUnroller : public Pass {
 public:
  LoopUnroller() : Pass() {}

  const char* name() const override { return "Loop unroller"; }

  Status Process(ir::IRContext* context) override;

 private:
  ir::IRContext* context_;
};

}  // namespace opt
}  // namespace spvtools
#endif
