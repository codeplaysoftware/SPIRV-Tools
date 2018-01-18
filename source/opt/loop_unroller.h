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
  using BasicBlockListTy = std::vector<std::unique_ptr<ir::BasicBlock>>;

  LoopUtils(ir::Function& function, ir::IRContext* context)
      : function_(function),
        ir_context_(context),
        loop_descriptor_(&function_) {}

  ir::BasicBlock* CopyLoop(ir::Loop& loop, ir::BasicBlock* preheader);

  ir::Loop DuplicateLoop(ir::Loop& loop);
  bool PartiallyUnroll(ir::Loop& loop, int factor);

  bool FullyUnroll(ir::Loop& loop);

  ir::LoopDescriptor& GetLoopDescriptor() { return loop_descriptor_; }

  bool CanEliminateConditionBlocks(ir::Loop& loop) const;

  bool CanPerformPartialUnroll(ir::Loop& loop);

 private:
  ir::Function& function_;
  ir::IRContext* ir_context_;
  ir::LoopDescriptor loop_descriptor_;

  ir::Instruction* previous_phi_;
  ir::BasicBlock* previous_continue_block_;
  ir::BasicBlock* previous_condition_block_;

  void PartiallyUnrollImpl(ir::Loop& loop, int factor);
  void PartiallyUnrollImpl(ir::Loop& loop, int factor,
                           ir::Instruction* induction,
                           ir::BasicBlock* initial_continue_block,
                           ir::BasicBlock* initial_condition);

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
