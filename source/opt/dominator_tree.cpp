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
#include "cfa.h"

#include <iostream>
#include <set>

using namespace spvtools;
using namespace spvtools::opt;

namespace {

template <typename SuccessorLambda, typename PreLambda, typename PostLambda>
static void depthFirstSearch(const ir::BasicBlock* BB,
                             SuccessorLambda successors, PreLambda pre,
                             PostLambda post) {
  // Ignore backedge operation.
  auto nop_backedge = [](const ir::BasicBlock*, const ir::BasicBlock*) {};

  CFA<ir::BasicBlock>::DepthFirstTraversal(BB, successors, pre, post,
                                           nop_backedge);
}

template <typename SuccessorLambda, typename PostLambda>
static void depthFirstSearchPostOrder(const ir::BasicBlock* BB,
                                      SuccessorLambda successors,
                                      PostLambda post) {
  // Ignore preorder operation.
  auto nop_preorder = [](const ir::BasicBlock*) {};
  depthFirstSearch(BB, successors, nop_preorder, post);
}

// Small type trait to get the function class type
template<typename BBType>
struct GetFunctionClass {
  using FunctionType = ir::Function;
};

// This helper class is basically a massive workaround for the current way that
// depth first is implemented.
template<typename BBType>
class BasicBlockSuccessorHelper {
  // This should eventually become const ir::BasicBlock
  using BasicBlock = BBType;
  using Function = typename GetFunctionClass<BBType>::FunctionType;

  using BasicBlockListTy = std::vector<BasicBlock*>;
  using BasicBlockMapTy = std::map<const BasicBlock*, BasicBlockListTy>;

 public:
  BasicBlockSuccessorHelper(Function& func, BasicBlock* dummyStartNode,
                            bool post);

  // CFA::CalculateDominators requires std::vector<ir::BasicBlock*>
  using GetBlocksFunction =
      std::function<const std::vector<BasicBlock*>*(const BasicBlock*)>;

  // Returns the list of predecessor functions.
  GetBlocksFunction GetPredFunctor() {
    return [&](const BasicBlock* BB) {
      BasicBlockListTy* v = &Predecessors[BB];
      return v;
    };
  }

  // Returns a vector of the list of successor nodes from a given node.
  GetBlocksFunction GetSuccessorFunctor() {
    return [&](const BasicBlock* BB) {
      BasicBlockListTy* v = &Successors[BB];
      return v;
    };
  }

 private:
  bool InvertGraph;
  BasicBlockMapTy Successors;
  BasicBlockMapTy Predecessors;

  // Build a bi-directional graph from the CFG of F.
  // If InvertGraph is true, all edge are reverted (successors becomes
  // predecessors and vise versa).
  // For convenience, the start of the graph is dummyStartNode. The dominator
  // tree construction requires a unique entry node, which cannot be guarantied
  // for the postdominator graph. The dummyStartNode BB is here to gather all
  // entry nodes.
  void CreateSuccessorMap(Function& F, BasicBlock* dummyStartNode);
};

template <typename BBType>
BasicBlockSuccessorHelper<BBType>::BasicBlockSuccessorHelper(
    Function& func, BasicBlock* dummyStartNode, bool Invert)
    : InvertGraph(Invert) {
  CreateSuccessorMap(func, dummyStartNode);
}

template <typename BBType>
void BasicBlockSuccessorHelper<BBType>::CreateSuccessorMap(
    Function& F, BasicBlock* dummyStartNode) {
  std::map<uint32_t, BasicBlock*> IDtoBBMap;
  auto GetSuccessorBasicBlock = [&](uint32_t successorID) {
    BasicBlock*& Succ = IDtoBBMap[successorID];
    if (!Succ) {
      for (BasicBlock& BBIt : F) {
        if (successorID == BBIt.id()) {
          Succ = &BBIt;
          break;
        }
      }
    }
    return Succ;
  };

  if (InvertGraph) {
    // for the post, we see the inverted graph
    // so successors in the inverted graph are the predecessor in the CFG.
    // The tree construction requires 1 entry point, so we add a dummy node
    // that is connected to all exiting basic block.
    // An exiting basic block is a block with an OpKill, OpUnreachable,
    // OpReturn or OpReturnValue as terminator instruction.
    for (BasicBlock& BB : F) {
      const auto& br = *BB.ctail();
      switch (br.opcode()) {
        case SpvOpKill:
        case SpvOpUnreachable:
        case SpvOpReturn:
        case SpvOpReturnValue: {
          // create a unique entry node
          Successors[dummyStartNode].push_back(&BB);
          Predecessors[&BB].push_back(dummyStartNode);
        } break;
        default: {
          BasicBlockListTy& PredList = Predecessors[&BB];
          BB.ForEachSuccessorLabel([&](const uint32_t successorID) {
            BasicBlock* Succ = GetSuccessorBasicBlock(successorID);
            // inverted graph, so our successor in the CFG
            // is our predecessor in the inverted graph
            Successors[Succ].push_back(&BB);
            PredList.push_back(Succ);
          });
        } break;
      }
    }
  } else {
    // technically, this is not needed, but it unifies
    // the handling of dominator and postdom tree later on
    Successors[dummyStartNode].push_back(F.entry().get());
    Predecessors[F.entry().get()].push_back(dummyStartNode);
    for (BasicBlock& BB : F) {
      BasicBlockListTy& SuccList = Successors[&BB];

      BB.ForEachSuccessorLabel([&](const uint32_t successorID) {
        BasicBlock* Succ = GetSuccessorBasicBlock(successorID);
        SuccList.push_back(Succ);
        Predecessors[Succ].push_back(&BB);
      });
    }
  }
}

} // namespace

namespace spvtools {
namespace opt {

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
    const ir::Function* F, ir::BasicBlock* DummyStartNode,
    std::vector<std::pair<ir::BasicBlock*, ir::BasicBlock*>>& edges) {
  // Each time the depth first traversal calls the postorder callback
  // std::function we push that node into the postorder vector to create our
  // postorder list
  std::vector<const ir::BasicBlock*> postorder;
  auto postorder_function = [&](const ir::BasicBlock* b) {
    postorder.push_back(b);
  };

  // CFA::CalculateDominators requires std::vector<ir::BasicBlock*>
  // BB are derived from F, so we need to const cast it at some point
  // no modification is made on F.
  BasicBlockSuccessorHelper<ir::BasicBlock> helper{
      *const_cast<ir::Function*>(F), DummyStartNode, PostDominator};

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
  depthFirstSearchPostOrder(DummyStartNode, successorFunctor,
                            postorder_function);
  edges =
    CFA<ir::BasicBlock>::CalculateDominators(postorder, predecessorFunctor);
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

  // Ignore backedge operation
  auto nop_backedge = [](const DominatorTreeNode*, const DominatorTreeNode*) {};

  CFA<DominatorTreeNode>::DepthFirstTraversal(Root, getSucc, preFunc, postFunc,
                                              nop_backedge);
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
