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

#ifndef DOMINATOR_ANALYSIS_PASS_H
#define DOMINATOR_ANALYSIS_PASS_H
#include <cstdint>
#include "dominator_tree.h"
#include "module.h"
#include "pass.h"

namespace spvtools {
namespace opt {

class DominatorAnalysisBase {
 public:
  DominatorAnalysisBase() : Tree(nullptr) {}
  virtual ~DominatorAnalysisBase() {}

  virtual void InitializeTree(const ir::Function* F) = 0;
  bool Dominates(const ir::BasicBlock* A, const ir::BasicBlock* B) const;

  bool Dominates(uint32_t A, uint32_t B) const;

  bool StrictlyDominates(const ir::BasicBlock* A,
                         const ir::BasicBlock* B) const;

  bool StrictlyDominates(uint32_t A, uint32_t B) const;

  void DumpAsDot(std::ostream& Out) const;

  ir::BasicBlock* ImmediateDominator(const ir::BasicBlock*) const;
  ir::BasicBlock* ImmediateDominator(uint32_t) const;

 protected:
  std::unique_ptr<DominatorTree> Tree;
};

class DominatorAnalysis : public DominatorAnalysisBase {
 public:
  DominatorAnalysis() : DominatorAnalysisBase() {}

  ~DominatorAnalysis() {}

  void InitializeTree(const ir::Function* f) override;
};

class PostDominatorAnalysis : public DominatorAnalysisBase {
 public:
  PostDominatorAnalysis() : DominatorAnalysisBase(){};

  ~PostDominatorAnalysis() {}

  void InitializeTree(const ir::Function* f) override;
};

// TODO: Decide if we want this to be a normal pass or not
class DominatorAnalysisPass {
 public:
  DominatorAnalysisPass() {}

  DominatorAnalysis* GetDominatorAnalysis(const ir::Function* f);
  PostDominatorAnalysis* GetPostDominatorAnalysis(const ir::Function* f);

 private:
  // Each function in the module will create its own dominator tree
  std::map<const ir::Function*, DominatorAnalysis> DomTrees;
  std::map<const ir::Function*, PostDominatorAnalysis> PostDomTrees;
};

}  // ir
}  // spvtools

#endif  // DOMINATOR_ANALYSIS_PASS_H
