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

#ifndef LIBSPIRV_OPT_DOMINATOR_ANALYSIS_TREE_H_
#define LIBSPIRV_OPT_DOMINATOR_ANALYSIS_TREE_H_

#include <cstdint>
#include <map>
#include <stack>
#include <type_traits>
#include <utility>
#include <vector>

#include "cfg.h"
#include "module.h"

namespace spvtools {
namespace opt {
// This helper struct forms the nodes in the tree, with each node containing its
// children. It also contains two values, for the pre and post indexes in the
// tree which are used to compare two nodes.
struct DominatorTreeNode {
  explicit DominatorTreeNode(ir::BasicBlock* bb)
      : bb_(bb),
        parent_(nullptr),
        children_({}),
        dfs_num_pre_(-1),
        dfs_num_post_(-1) {}

  using iterator = std::vector<DominatorTreeNode*>::iterator;
  using const_iterator = std::vector<DominatorTreeNode*>::const_iterator;

  iterator begin() { return children_.begin(); }
  iterator end() { return children_.end(); }
  const_iterator begin() const { return cbegin(); }
  const_iterator end() const { return cend(); }
  const_iterator cbegin() const { return children_.begin(); }
  const_iterator cend() const { return children_.end(); }

  inline uint32_t id() const { return bb_->id(); }

  ir::BasicBlock* bb_;
  DominatorTreeNode* parent_;
  std::vector<DominatorTreeNode*> children_;

  // These indexes are used to compare two given nodes. A node is a child or
  // grandchild of another node if its preorder index is greater than the
  // first nodes preorder index AND if its postorder index is less than the
  // first nodes postorder index.
  int dfs_num_pre_;
  int dfs_num_post_;
};

// Helper class to iterate over a tree in a DFS order.
// It work by manipulating the node children begin/end iterators.
template <typename NodeTy>
class TreeDFSIterator {
  // Type alias to keep track of the const qualifier.
  using NodeIterator =
      typename std::conditional<std::is_const<NodeTy>::value,
                                typename NodeTy::const_iterator,
                                typename NodeTy::iterator>::type;

 public:
  // Standard iterator interface.
  using reference = typename NodeIterator::reference;
  using value_type = typename NodeIterator::value_type;

  static inline TreeDFSIterator begin(NodeIterator node_it,
                                      NodeIterator end_it) {
    return TreeDFSIterator(node_it, end_it);
  }

  static inline TreeDFSIterator end(NodeIterator end_it) {
    return TreeDFSIterator(end_it);
  }

  bool operator==(const TreeDFSIterator& x) const {
    return current_.first == x.current_.first;
  }

  bool operator!=(const TreeDFSIterator& x) const { return !(*this == x); }

  reference operator*() const { return *current_.first; }

  TreeDFSIterator& operator++() {
    MoveToNextNode();
    return *this;
  }

  TreeDFSIterator operator++(int) {
    TreeDFSIterator tmp = *this;
    ++*this;
    return tmp;
  }

 private:
  inline TreeDFSIterator(NodeIterator node_it, NodeIterator end_it)
      : current_(node_it, end_it) {
    if (node_it != end_it) {
      // Force the iterator to be identical with end().
      parent_iterators_.push(std::make_pair(end_it, end_it));
    }
  }

  // end iterator.
  explicit inline TreeDFSIterator(NodeIterator end_it)
      : TreeDFSIterator(end_it, end_it) {}

  // Set the first parent in the as the current iterator.
  // Do nothing if the stack is empty.
  inline void pop_parent() {
    if (!parent_iterators_.empty()) {
      current_ = parent_iterators_.top();
      parent_iterators_.pop();
    }
  }

  // Move the iterator to the next node in the tree.
  // if we are at the end, do nothing, otherwise
  // if our current node has children, use the children iterator and push the
  // current into the stack.
  // If we reach the end of the local iterator, pop the parent one.
  inline void MoveToNextNode() {
    if (current_.first == current_.second && parent_iterators_.empty()) return;
    assert(current_.first != current_.second);
    value_type current_node = *current_.first;
    current_.first++;
    // if there is remaining nodes, save them for latter
    if (current_.first != current_.second) parent_iterators_.emplace(current_);
    if (current_node->begin() != current_node->end())
      current_ = {current_node->begin(), current_node->end()};
    else
      pop_parent();
  }

  std::pair<NodeIterator, NodeIterator> current_;
  std::stack<std::pair<NodeIterator, NodeIterator>> parent_iterators_;
};

// A class representing a tree of BasicBlocks in a given function, where each
// node is dominated by its parent.
class DominatorTree {
 public:
  // Map OpLabel ids to dominator tree nodes
  using DominatorTreeNodeMap = std::map<uint32_t, DominatorTreeNode>;
  using iterator = TreeDFSIterator<DominatorTreeNode>;
  using const_iterator = TreeDFSIterator<const DominatorTreeNode>;

  // List of DominatorTreeNode to define the list of roots
  using DominatorTreeNodeList = std::vector<DominatorTreeNode*>;
  using roots_iterator = DominatorTreeNodeList::iterator;
  using roots_const_iterator = DominatorTreeNodeList::const_iterator;

  DominatorTree() : postdominator_(false) {}
  explicit DominatorTree(bool post) : postdominator_(post) {}

  iterator begin() { return iterator::begin(roots_.begin(), roots_.end()); }
  iterator end() { return iterator::end(roots_.end()); }
  const_iterator begin() const { return cbegin(); }
  const_iterator end() const { return cend(); }
  const_iterator cbegin() const {
    return const_iterator::begin(roots_.begin(), roots_.end());
  }
  const_iterator cend() const { return const_iterator::end(roots_.end()); }

  roots_iterator roots_begin() { return roots_.begin(); }
  roots_iterator roots_end() { return roots_.end(); }
  roots_const_iterator roots_begin() const { return roots_cbegin(); }
  roots_const_iterator roots_end() const { return roots_cend(); }
  roots_const_iterator roots_cbegin() const { return roots_.begin(); }
  roots_const_iterator roots_cend() const { return roots_.end(); }

  // Get the unique root of the tree.
  // It is guaranteed to work on a dominator tree.
  // post-dominator might have a list.
  DominatorTreeNode* GetRoot() {
    assert(roots_.size() == 1);
    return *roots_.begin();
  }

  const DominatorTreeNode* GetRoot() const {
    assert(roots_.size() == 1);
    return *roots_.begin();
  }

  const DominatorTreeNodeList& Roots() const { return roots_; }

  // Dumps the tree in the graphvis dot format into the |out_stream|.
  void DumpTreeAsDot(std::ostream& out_stream) const;

  // Build the (post-)dominator tree for the function |f|
  // Any existing data will be overwritten
  void InitializeTree(const ir::Function* f, const ir::CFG& cfg);

  // Check if the basic block |a| dominates the basic block |b|.
  bool Dominates(const ir::BasicBlock* a, const ir::BasicBlock* b) const;

  // Check if the basic block id |a| dominates the basic block id |b|.
  bool Dominates(uint32_t a, uint32_t b) const;

  // Check if the basic block |a| strictly dominates the basic block |b|.
  bool StrictlyDominates(const ir::BasicBlock* a,
                         const ir::BasicBlock* b) const;

  // Check if the basic block id |a| strictly dominates the basic block id |b|.
  bool StrictlyDominates(uint32_t a, uint32_t b) const;

  // Returns the immediate dominator of basic block |a|.
  ir::BasicBlock* ImmediateDominator(const ir::BasicBlock* A) const;

  // Returns the immediate dominator of basic block id |a|.
  ir::BasicBlock* ImmediateDominator(uint32_t a) const;

  // Returns true if the basic block |a| is reachable by this tree. A node would
  // be unreachable if it cannot be reached by traversal from the start node or
  // for a postdominator tree, cannot be reached from the exit nodes.
  inline bool ReachableFromRoots(const ir::BasicBlock* a) const {
    if (!a) return false;
    return ReachableFromRoots(a->id());
  }

  // Returns true if the basic block id |a| is reachable by this tree.
  bool ReachableFromRoots(uint32_t a) const;

  // Returns true if this tree is a post dominator tree.
  bool IsPostDominator() const { return postdominator_; }

  // Clean up the tree.
  void ClearTree() {
    nodes_.clear();
    roots_.clear();
  }

  // Applies the std::function |func| to all nodes in the dominator tree.
  bool Visit(std::function<bool(DominatorTreeNode*)> func) {
    for (auto n : *this) {
      if (!func(n)) return false;
    }
    return true;
  }

  // Applies the std::function |func| to all nodes in the dominator tree.
  bool Visit(std::function<bool(const DominatorTreeNode*)> func) const {
    for (auto n : *this) {
      if (!func(n)) return false;
    }
    return true;
  }

 private:
  // Adds the basic block |bb| to the tree structure if it doesn't already
  // exist.
  DominatorTreeNode* GetOrInsertNode(ir::BasicBlock* bb);

  // Wrapper functio which gets the list of BasicBlock->DominatingBasicBlock
  // from the CFA and stores it in the edges parameter.
  void GetDominatorEdges(
      const ir::Function* F, const ir::BasicBlock* DummyStartNode,
      std::vector<std::pair<ir::BasicBlock*, ir::BasicBlock*>>& edges);

  // The roots of the tree.
  std::vector<DominatorTreeNode*> roots_;

  // Pairs each basic block id to the tree node containing that basic block.
  DominatorTreeNodeMap nodes_;

  // True if this is a post dominator tree.
  bool postdominator_;
};

}  // namespace opt
}  // namespace spvtools

#endif  // LIBSPIRV_OPT_DOMINATOR_ANALYSIS_TREE_H_
