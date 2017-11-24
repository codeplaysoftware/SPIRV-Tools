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

#include "dominator_tree.h"
#include <iostream>
#include <set>
#include "cfa.h"

namespace spvtools {
namespace opt {

template <typename BBType, typename SuccessorLambda, typename PreLambda,
          typename PostLambda>
static void depthFirstSearch(const BBType* BB, SuccessorLambda successors,
                             PreLambda pre, PostLambda post) {
  // Ignore backedge operation.
  auto nop_backedge = [](const BBType*, const BBType*) {};

  CFA<BBType>::DepthFirstTraversal(BB, successors, pre, post, nop_backedge);
}

template <typename BBType, typename SuccessorLambda, typename PostLambda>
static void depthFirstSearchPostOrder(const BBType* BB,
                                      SuccessorLambda successors,
                                      PostLambda post) {
  // Ignore preorder operation.
  auto nop_preorder = [](const BBType*) {};
  depthFirstSearch(BB, successors, nop_preorder, post);
}

// This helper class is basically a massive workaround for the current way that
// depth first is implemented.
// TODO: Either clean this up with a nicer way of doing it all or reimplememt
// parts of DFS to avoid needing these functions.
class BasicBlockSuccessorHelper {
 public:
  BasicBlockSuccessorHelper(ir::Function* func,
                            ir::BasicBlock* dummyStartNode, bool post);

  using GetBlocksFunction =
      std::function<const std::vector<ir::BasicBlock*>*(const ir::BasicBlock*)>;

  // Returns the list of predecessor functions.
  // TODO: Just a hack to get this working, doesn't even check if pred is in the
  // list
  GetBlocksFunction GetPredFunctor() {
    return [&](const ir::BasicBlock* BB) {
      auto v = &Pred[BB];
      return v;
    };
  }

  // Returns a vector of the list of successor nodes from a given node.
  // TODO: As above
  GetBlocksFunction GetSuccessorFunctor() {
    return [&](const ir::BasicBlock* BB) {
      auto v = &Successors[BB];
      return v;
    };
  }

  const ir::BasicBlock* GetEntryNode() const { return DummyStartNode.get(); }

 private:
  ir::Function* F;
  std::map<const ir::BasicBlock*, std::vector<ir::BasicBlock*>> Successors;
  std::map<const ir::BasicBlock*, std::vector<ir::BasicBlock*>> Pred;
  std::set<const ir::BasicBlock*> ReachableNodes;

  // By maintaining a dummy node which acts as the first entry node we can
  // maintain track of multiple roots while still having a singular point for
  // depth first search.
  std::unique_ptr<ir::BasicBlock> DummyStartNode;

  void FindDummyNodeChildren(ir::BasicBlock*);

  void GenerateList();
};

BasicBlockSuccessorHelper::BasicBlockSuccessorHelper(
    ir::Function* func, ir::BasicBlock* dummyStartNode, bool post)
    : F(func) {
  // Generate the internal representation of the CFG.
  GenerateList();

  auto postorder_function = [&](const ir::BasicBlock* b) {
    ReachableNodes.insert(b);
  };

  depthFirstSearchPostOrder(func->entry().get(), GetSuccessorFunctor(),
                            postorder_function);

  // If we're moving through the tree in post order we can have multiple
  // children for the dummy entry node otherwise we just make it a predecessor of
  // the entry node.
  if (post) {
    FindDummyNodeChildren(dummyStartNode);
  } else {
    Pred[dummyStartNode] = {};
    Successors[dummyStartNode] = {func->entry().get()};
  }
  Pred[func->entry().get()].push_back(dummyStartNode);
}

// Locate the roots of the tree and make them the children of the dummy start
// node and make the dummy start node their parents.
void BasicBlockSuccessorHelper::FindDummyNodeChildren(
    ir::BasicBlock* dummyStartNode) {
  Pred[dummyStartNode] = {};
  for (auto& pair : Successors) {
    if (ReachableNodes.count(pair.first) == 0) {
      for (const ir::BasicBlock* BBptr : pair.second) {
        auto& p = Pred[BBptr];

        auto itr = std::find(p.begin(), p.end(), pair.first);
        if (itr == p.end()) continue;
        p.erase(itr);
      }
      continue;
    }
    // If the BasicBlock has nodes preceding it in the traversal of the tree
    // then it is a root node.
    if (pair.second.size() == 0) {
      pair.second.push_back(dummyStartNode);
      Pred[dummyStartNode].push_back(const_cast<ir::BasicBlock*>(pair.first));
    }
  }
  // The dummy root node should have no nodes behind it in the tree.
  Successors[dummyStartNode] = {};
}

void BasicBlockSuccessorHelper::GenerateList() {
  for (ir::BasicBlock& BB : *F) {
    ir::BasicBlock* ptrToBB = &BB;
    if (Pred.find(ptrToBB) == Pred.end()) {
      Pred[ptrToBB] = {};
    }

    if (Successors.find(ptrToBB) == Successors.end()) {
      Successors[ptrToBB] = {};
    }

    ptrToBB->ForEachSuccessorLabel([&](const uint32_t successorID) {
      // TODO: If we keep somthing like this, avoid going over the full N
      // functions each time.
      for (auto itr = F->begin(); itr < F->end(); ++itr) {
        ir::BasicBlock* bb = &*itr;
        if (successorID == bb->id()) {
          Successors[ptrToBB].push_back(bb);

          if (Pred.find(bb) == Pred.end()) {
            Pred[bb] = {};
          }

          if (std::find(Pred[bb].begin(), Pred[bb].end(), ptrToBB) ==
              Pred[bb].end())
            Pred[bb].push_back(const_cast<ir::BasicBlock*>(ptrToBB));

          // Found the successor node, exit out.
          break;
        }
      }
    });
  }
}

bool DominatorTree::StrictlyDominates(uint32_t A, uint32_t B) const {
  if (A == B) return false;
  return Dominates(A, B);
}

bool DominatorTree::StrictlyDominates(const ir::BasicBlock* A,
                                      const ir::BasicBlock* B) const {
  return DominatorTree::StrictlyDominates(A->id(), B->id());
}

bool DominatorTree::Dominates(uint32_t A, uint32_t B) const {
  // Check that both of the inputs are actual nodes
  auto aItr = Nodes.find(A);
  auto bItr = Nodes.find(B);
  if (aItr == Nodes.end() || bItr == Nodes.end()) return false;

  // Node A dominates node B if they are the same.
  if (A == B) return true;
  const DominatorTreeNode* nodeA = &aItr->second;
  const DominatorTreeNode* nodeB = &bItr->second;

  if (nodeA->DepthFirstInCount < nodeB->DepthFirstInCount &&
      nodeA->DepthFirstOutCount > nodeB->DepthFirstOutCount) {
    return true;
  }

  return false;
}

bool DominatorTree::Dominates(const ir::BasicBlock* A,
                              const ir::BasicBlock* B) const {
  return Dominates(A->id(), B->id());
}

ir::BasicBlock* DominatorTree::ImmediateDominator(
    const ir::BasicBlock* A) const {
  return ImmediateDominator(A->id());
}

ir::BasicBlock* DominatorTree::ImmediateDominator(uint32_t A) const {
  // Check that A is a valid node in the tree
  auto aItr = Nodes.find(A);
  if (aItr == Nodes.end()) return nullptr;

  const DominatorTreeNode* nodeA = &aItr->second;

  if (nodeA->Parent == nullptr || nodeA->Parent == Root) {
    return nullptr;
  }

  return nodeA->Parent->BB;
}

DominatorTree::DominatorTreeNode* DominatorTree::GetOrInsertNode(
    ir::BasicBlock* BB) {
  uint32_t id = BB->id();
  if (Nodes.find(id) == Nodes.end()) {
    Nodes[id] = {BB};
  }

  return &Nodes[id];
}

void DominatorTree::GetDominatorEdges(
    const ir::Function* F, const ir::BasicBlock* DummyStartNode,
    std::vector<std::pair<ir::BasicBlock*, ir::BasicBlock*>>& edges) {
  // Each time the depth first traversal calls the postorder callback
  // std::function we push that node into the postorder vector to create our
  // postorder list
  std::vector<const ir::BasicBlock*> postorder;
  auto postorder_function = [&](const ir::BasicBlock* b) {
    postorder.push_back(b);
  };

  // TODO: Refactor helper class and get rid of this const cast
  BasicBlockSuccessorHelper helper{const_cast<ir::Function*>(F),
      const_cast<ir::BasicBlock*>(DummyStartNode), PostDominator};

  // The successor function tells DepthFirstTraversal how to move to successive
  // nodes by providing an interface to get a list of successor nodes from any
  // given node
  auto successorFunctor = helper.GetSuccessorFunctor();

  // predecessorFunctor does the same as the successor functor but for all nodes
  // preceding a given node
  auto predecessorFunctor = helper.GetPredFunctor();

  // If we're building a post dominator tree we traverse the tree in reverse
  // using the predecessor function in place of the successor function and vice
  // versa
  if (PostDominator) {
    depthFirstSearchPostOrder(DummyStartNode, predecessorFunctor,
                              postorder_function);
    edges =
        CFA<ir::BasicBlock>::CalculateDominators(postorder, successorFunctor);
  } else {
    depthFirstSearchPostOrder(DummyStartNode, successorFunctor,
                              postorder_function);
    edges =
        CFA<ir::BasicBlock>::CalculateDominators(postorder, predecessorFunctor);
  }
}

void DominatorTree::InitializeTree(const ir::Function* F) {
  // Skip over empty functions
  if (F->cbegin() == F->cend()) {
    return;
  }

  std::unique_ptr<ir::Instruction> DummyLabel{new ir::Instruction(
      F->GetParent()->context(), SpvOp::SpvOpLabel, 0, -1, {})};
  // Create a dummy start node which will point to all of the roots of the tree
  // to allow us to work with a singular root.
  ir::BasicBlock DummyStartNode(std::move(DummyLabel));

  // Get the immedate dominator for each node
  std::vector<std::pair<ir::BasicBlock*, ir::BasicBlock*>> edges;
  GetDominatorEdges(F, &DummyStartNode, edges);

  // Transform the vector<pair> into the tree structure which we can use to
  // efficiently query dominace
  for (auto edge : edges) {
    uint32_t nodeID = edge.first->id();
    uint32_t dominatorID = edge.second->id();

    DominatorTreeNode* first = GetOrInsertNode(edge.first);

    if (nodeID == dominatorID) continue;

    DominatorTreeNode* second = GetOrInsertNode(edge.second);

    first->Parent = second;
    second->Children.push_back(first);
  }

  Root = GetOrInsertNode(&DummyStartNode);
  Root->BB = nullptr;

  int index = 0;
  auto preFunc = [&](const DominatorTreeNode* node) {
    if (node != Root)
      const_cast<DominatorTreeNode*>(node)->DepthFirstInCount = ++index;
  };

  auto postFunc = [&](const DominatorTreeNode* node) {
    if (node != Root)
      const_cast<DominatorTreeNode*>(node)->DepthFirstOutCount = ++index;
  };

  auto getSucc = [&](const DominatorTreeNode* node) { return &node->Children; };

  depthFirstSearch(Root, getSucc, preFunc, postFunc);
}

void DominatorTree::DumpTreeAsDot(std::ostream& OutStream) const {
  if (!Root) return;

  OutStream << "digraph {\n";
  Visit(Root, [&](const DominatorTreeNode* node) {

    // Print the node, note special case for the dummy entry node
    if (node->BB) {
      OutStream << node->BB->id() << "[label=\"" << node->BB->id() << "\"];\n";
    } else {
      OutStream << node->BB->id() << "[label=\"DummyEntryNode\"];\n";
    }

    // Print the arrow from the parent to this node
    if (node->Parent) {
      OutStream << node->Parent->BB->id() << " -> " << node->BB->id();
      if (!node->Parent->BB)
        OutStream << "[style=dotted]";
      OutStream << ";\n";
    }
  });
  OutStream << "}\n";
}

void DominatorTree::Visit(
    const DominatorTreeNode* Node,
    std::function<void(const DominatorTreeNode*)> func) const {
  // Apply the function to the node
  func(Node);

  // Apply the function to every child node
  for (const DominatorTreeNode* child : Node->Children) {
    Visit(child, func);
  }
}

}  // opt
}  // spvtools
