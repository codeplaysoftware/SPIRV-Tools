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

#include "licm_pass.h"

#include "pass.h"

namespace spvtools {
namespace opt {

Pass::Status LICMPass::Process(ir::IRContext* irContext) {
  bool modified = ProcessIRContext(irContext);

  return modified ? Status::SuccessWithChange : Status::SuccessWithoutChange;
}

bool LICMPass::ProcessIRContext(ir::IRContext* irContext) {
  bool modified = false;
  ir::Module* module = irContext->module();
  for (ir::Function& f : *module) {
    modified |= ProcessFunction(&f);
  }
  return modified;
}

bool LICMPass::ProcessFunction(ir::Function* f) {
  bool modified = false;
  LoopDescriptor loopDescriptor{f};
  for (size_t i = 0; i < loopDescriptor.NumLoops(); ++i) {
    modified |= ProcessLoop(&loopDescriptor.GetLoop(i));
  }
  return modified;
}

bool LICMPass::ProcessLoop(Loop* loop) {
  // Process all nested loops first
  if (loop->HasNestedLoops()) {
    auto nested_loops = loop->GetNestedLoops();
    for (auto it = nested_loops.begin(); it != nested_loops.end(); ++it) {
      ProcessLoop(*it);
    }
  }

  std::vector<ir::Instruction*> invariants = {};
  ir::BasicBlock* bb = loop->GetContinueBB();
  for (auto it = bb->begin(); it != bb->end(); ++it) {
    if (loop->IsLoopInvariant(&(*it))) {
      invariants.push_back(&(*it));
    }
  }

  return false;
}

}
}
