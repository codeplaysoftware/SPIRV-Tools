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

#ifndef SOURCE_OPT_LOOP_UTILS_H_
#define SOURCE_OPT_LOOP_UTILS_H_
#include <list>
#include <memory>
#include <vector>
#include "opt/loop_descriptor.h"

namespace spvtools {
namespace opt {

class LoopUtils {
 public:
  using BasicBlockListTy = std::vector<std::unique_ptr<ir::BasicBlock>>;

  LoopUtils(ir::Function* function, ir::IRContext* context)
      : function_(*function),
        ir_context_(context),
        loop_descriptor_(&function_) {}

  bool PartiallyUnroll(ir::Loop* loop, size_t factor);

  bool FullyUnroll(ir::Loop* loop);

  ir::LoopDescriptor& GetLoopDescriptor() { return loop_descriptor_; }

  bool CanPerformUnroll(ir::Loop* loop);

 private:
  ir::Function& function_;
  ir::IRContext* ir_context_;
  ir::LoopDescriptor loop_descriptor_;
};

}  // namespace opt
}  // namespace spvtools

#endif  // SOURCE_OPT_LOOP_UTILS_H_
