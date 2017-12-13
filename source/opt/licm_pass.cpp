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
#include "cfg.h"
#include "module.h"

#include "pass.h"

namespace spvtools {
namespace opt {

LICMPass::LICMPass(){};

Pass::Status LICMPass::Process(ir::IRContext* context) {
  bool modified = false;

  if (context != nullptr) {
    ir_context = context;
    modified = ProcessIRContext();
  }

  return modified ? Status::SuccessWithChange : Status::SuccessWithoutChange;
}

bool LICMPass::ProcessIRContext() {
  bool modified = false;
  ir::Module* module = ir_context->module();
  for (ir::Function& f : *module) {
    modified |= ProcessFunction(&f);
  }
  return modified;
}

bool LICMPass::ProcessFunction(ir::Function* f) {
  bool modified = false;
  LoopDescriptor loop_descriptor{f};

  ir::CFG cfg(ir_context->module());
  dom_analysis = ir_context->GetDominatorAnalysis(f, cfg);

  for (size_t i = 0; i < loop_descriptor.NumLoops(); ++i) {
    modified |= ProcessLoop(&loop_descriptor.GetLoop(i));
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
    if (it->result_id() == 0) continue;
    if (loop->IsLoopInvariant(&(*it))) {
      invariants.push_back(&(*it));
    }
  }

  ir::BasicBlock invariants_bb(
      std::unique_ptr<ir::Instruction>((new ir::Instruction(
          ir_context, SpvOpLabel, 0, ir_context->TakeNextUniqueId(), {}))));
  ir::BasicBlock* pre_header = FindPreheader(loop);

  if (invariants.size() > 0) {
    for (auto invariant_it = invariants.begin();
         invariant_it != invariants.end(); ++invariant_it) {
      invariants_bb.AddInstruction(
          std::unique_ptr<ir::Instruction>(*invariant_it));
    }
  }

  return HoistInstructions(loop, pre_header, &invariants_bb);
}

ir::BasicBlock* LICMPass::FindPreheader(Loop* loop) {
  ir::BasicBlock* bb = dom_analysis->ImmediateDominator(loop->GetStartBB());
  return bb;
}

// TODO(Alexander: Remove instructions from loop when hoisting)
bool LICMPass::HoistInstructions(Loop* loop, ir::BasicBlock* pre_header_bb,
                                 ir::BasicBlock* invariants_bb) {
  if (loop == nullptr || pre_header_bb == nullptr || invariants_bb == nullptr) {
    return false;
  }
  pre_header_bb->AddInstructions(invariants_bb);
  return true;
}

}  // namespace opt
}  // namespace spvtools
