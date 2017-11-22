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

#include "dominator_analysis_pass.h"
#include <iostream>
#include "cfa.h"

namespace spvtools {
namespace opt {

void DominatorAnalysis::InitializeTree(ir::Module& module) {
  DominatorAnalysisBase::InitializeTree(module);
}

void PostDominatorAnalysis::InitializeTree(ir::Module& module) {
  DominatorAnalysisBase::InitializeTree(module);
}

void DominatorAnalysisBase::InitializeTree(ir::Module& module) {
  for (const ir::Function& func : module) {
    InitializeTree(&func);
  }
}

void PostDominatorAnalysis::InitializeTree(const ir::Function* F) {
  Trees[F] = {true};

  Trees[F].InitializeTree(F);
  Trees[F].DumpTreeAsDot(std::cout);
}

void DominatorAnalysis::InitializeTree(const ir::Function* F) {
  Trees[F] = {false};

  Trees[F].InitializeTree(F);
  Trees[F].DumpTreeAsDot(std::cout);
}

bool DominatorAnalysisBase::Dominates(const ir::BasicBlock* A,
                                      const ir::BasicBlock* B,
                                      const ir::Function* F) const {
  return Dominates(A->id(), B->id(), F);
}
bool DominatorAnalysisBase::Dominates(uint32_t A, uint32_t B,
                                      const ir::Function* F) const {
  auto itr = Trees.find(F);
  return itr->second.Dominates(A, B);
}

bool DominatorAnalysisBase::StrictlyDominates(const ir::BasicBlock* A,
                                              const ir::BasicBlock* B,
                                              const ir::Function* F) const {
  return StrictlyDominates(A->id(), B->id(), F);
}

bool DominatorAnalysisBase::StrictlyDominates(uint32_t A, uint32_t B,
                                              const ir::Function* F) const {
  auto itr = Trees.find(F);
  return itr->second.StrictlyDominates(A, B);
}

}  // opt
}  // spvtools
