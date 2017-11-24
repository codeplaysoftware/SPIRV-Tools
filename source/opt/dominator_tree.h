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

#ifndef DOMINATOR_ANALYSIS_TREE_H
#define DOMINATOR_ANALYSIS_TREE_H

#include <cstdint>
#include <map>
#include "module.h"

namespace spvtools {
namespace opt {

// A class representing a tree of BasicBlocks in a given function, where each
// node is dominated by its parent.
class DominatorTree {
 public:
  DominatorTree() : Root(nullptr), PostDominator(false) {}
  DominatorTree(bool post) : Root(nullptr), PostDominator(post) {}

  bool Validate() const;

  // Dumps the tree in the graphvis dot format into the stream.
  void DumpTreeAsDot(std::ostream& OutStream) const;

  void InitializeTree(const ir::Function* M);

  // Check if BasicBlock B is a dominator of BasicBlock A.
  bool Dominates(const ir::BasicBlock* A, const ir::BasicBlock* B) const;

  // Check if BasicBlock B is a dominator of BasicBlock A. This function uses
  // the IDs of A and B.
  bool Dominates(uint32_t A, uint32_t B) const;

  bool StrictlyDominates(const ir::BasicBlock* A,
                         const ir::BasicBlock* B) const;

  bool StrictlyDominates(uint32_t A, uint32_t B) const;

  // Returns the immediate dominator of basicblock A.
  ir::BasicBlock* ImmediateDominator(const ir::BasicBlock* A) const;

  // Returns the immediate dominator of basicblock A.
  ir::BasicBlock* ImmediateDominator(uint32_t A) const;

  bool Reachable(const ir::BasicBlock* A) const;
  bool Reachable(uint32_t A) const;

 private:
  struct DominatorTreeNode {
    DominatorTreeNode(ir::BasicBlock* bb)
        : BB(bb),
          Parent(nullptr),
          Children({}),
          DepthFirstInCount(-1),
          DepthFirstOutCount(-1) {}
    DominatorTreeNode() : DominatorTreeNode(nullptr) {}

    ir::BasicBlock* BB;
    DominatorTreeNode* Parent;
    std::vector<DominatorTreeNode*> Children;
    int DepthFirstInCount;
    int DepthFirstOutCount;

    uint32_t id() const {
      if (BB) {
        return BB->id();
      } else {
        return 0;
      }
    }
  };

  // The root of the tree.
  DominatorTreeNode* Root;

  // Adds the BasicBlock to the tree structure if it doesn't already exsist.
  DominatorTreeNode* GetOrInsertNode(ir::BasicBlock* BB);

  // Applies the std::function 'func' to 'node' then applies it to nodes
  // children.
  void Visit(const DominatorTreeNode* node,
             std::function<void(const DominatorTreeNode*)> func) const;

  // Wrapper functio which gets the list of BasicBlock->DominatingBasicBlock
  // from the CFA and stores it in the edges parameter.
  void GetDominatorEdges(
      const ir::Function* F, ir::BasicBlock* DummyStartNode,
      std::vector<std::pair<ir::BasicBlock*, ir::BasicBlock*>>& edges);

  // Pairs each basic block id to the tree node containing that basic block.
  std::map<uint32_t, DominatorTreeNode> Nodes;

  // The depth first implementation in cfa requires us to have access to a
  // vector of each successor node from any give node.
  std::map<const DominatorTreeNode*, std::vector<DominatorTreeNode*>>
      Successors;

  // True if this is a post dominator tree.
  bool PostDominator;
};

}  // ir
}  // spvtools

#endif
