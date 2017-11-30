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
    Loop(const ir::BasicBlock *bb);
};

class LoopDescriptor {
 public:
  // Creates a loop object for all loops found in |f|.
  LoopDescriptor(const ir::Function* f);
};

}  // namespace opt
}  // namespace spvtools

#endif // LIBSPIRV_OPT_LOOP_DESCRIPTORS_H_
