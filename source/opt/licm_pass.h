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

#include "pass.h"

#include "loop_descriptor.h"

namespace spvtools {
namespace opt {

class LICMPass : public Pass {
 public:
  const char* name() const override { return "licm"; }
  Status Process(ir::IRContext *) override;

 private:
  bool ProcessIRContext(ir::IRContext* irContext);
  bool ProcessFunction(ir::Function* f);
  bool ProcessLoop(Loop* loop);

};

}
}

#endif  // LIBSPIRV_OPT_LICM_PASS_H_
