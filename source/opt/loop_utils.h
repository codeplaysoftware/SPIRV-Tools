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

// LoopUtils is used to encapsulte loop optimizations and from the passes which
// use them. Any pass which needs a loop optimization should do it through this
// or through a pass which is using this.
class LoopUtils {
 public:
  // Store references to |function| and |context| and create the loop descriptor
  // from the |function|.
  LoopUtils(ir::Function* function, ir::IRContext* context)
      : function_(*function),
        ir_context_(context),
        loop_descriptor_(&function_) {}

  // Perfom a partial unroll of |loop| by given |factor|. This will copy the
  // body of the loop |factor| times. So a |factor| of one would give a new loop
  // with the original body plus one unrolled copy body.
  bool PartiallyUnroll(ir::Loop* loop, size_t factor);

  // Fully unroll |loop|.
  bool FullyUnroll(ir::Loop* loop);

  // Get the stored loop descriptor generated from the function passed into the
  // constructor.
  ir::LoopDescriptor& GetLoopDescriptor() { return loop_descriptor_; }

  // This function validates that |loop| meets the assumptions made by the
  // implementation of the loop unroller. As the implementation accommodates
  // more types of loops this function can reduce its checks.
  bool CanPerformUnroll(ir::Loop* loop);

 private:
  ir::Function& function_;
  ir::IRContext* ir_context_;
  ir::LoopDescriptor loop_descriptor_;
};

}  // namespace opt
}  // namespace spvtools

#endif  // SOURCE_OPT_LOOP_UTILS_H_
