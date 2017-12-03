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

#ifndef LIBSPIRV_OPT_LOOP_DESCRIPTORS_H_
#define LIBSPIRV_OPT_LOOP_DESCRIPTORS_H_

#include <cstdint>
#include <map>

#include "module.h"
#include "pass.h"

namespace spvtools {
namespace opt {

// A class to represent a loop.
class Loop {
 public:
  Loop(const ir::BasicBlock* begin, const ir::BasicBlock* continue_target,
       const ir::BasicBlock* merge_target)
      : loop_start_(begin),
        loop_continue_(continue_target),
        loop_merge_(merge_target){};

  inline const ir::BasicBlock* GetStartBB() const { return loop_start_; }

  inline const ir::BasicBlock* GetContinueBB() const { return loop_continue_; }

  inline const ir::BasicBlock* GetMergeBB() const { return loop_merge_; }

  inline bool HasNestedLoops() const { return nested_loops_.size() != 0; };

  inline size_t GetNumNestedLoops() const { return nested_loops_.size(); };

 private:
  // The block which marks the start of the loop.
  const ir::BasicBlock* loop_start_;

  // The block which begins the body of the loop.
  const ir::BasicBlock* loop_continue_;

  // The block which marks the end of the loop.
  const ir::BasicBlock* loop_merge_;

  // Nested child loops of this loop.
  std::vector<Loop*> nested_loops_;
};

class LoopDescriptor {
 public:
  // Creates a loop object for all loops found in |f|.
  LoopDescriptor(const ir::Function* f);

  // Return the number of loops found in the function.
  size_t NumLoops() const { return loops_.size(); }

  // Return the loop at a particular |index|. The |index| must be in bounds,
  // check with NumLoops before calling.
  inline const Loop& GetLoop(size_t index) const {
    assert(loops_.size() > index &&
           "Index out of range (larger than loop count)");
    return loops_[index];
  }

 private:
  std::vector<Loop> loops_;
};

}  // namespace opt
}  // namespace spvtools

#endif  // LIBSPIRV_OPT_LOOP_DESCRIPTORS_H_
