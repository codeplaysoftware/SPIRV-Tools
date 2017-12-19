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
#include <utility>
#include <vector>

#include "cfg.h"
#include "iterator.h"
#include "module.h"
#include "tree_iterator.h"

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

// A class representing a tree of BasicBlocks in a given function, where each
// node is dominated by its parent.
class DominatorTree {
 public:
  // Map OpLabel ids to dominator tree nodes
  using DominatorTreeNodeMap = std::map<uint32_t, DominatorTreeNode>;
  using iterator = TreeDFIterator<DominatorTreeNode>;
  using const_iterator = TreeDFIterator<const DominatorTreeNode>;
  using post_iterator = PostOrderTreeDFIterator<DominatorTreeNode>;
  using const_post_iterator = PostOrderTreeDFIterator<const DominatorTreeNode>;

  // List of DominatorTreeNode to define the list of roots
  using DominatorTreeNodeList = std::vector<DominatorTreeNode*>;
  using roots_iterator = DominatorTreeNodeList::iterator;
  using roots_const_iterator = DominatorTreeNodeList::const_iterator;

  DominatorTree() : postdominator_(false) {}
  explicit DominatorTree(bool post) : postdominator_(post) {}

  iterator get_iterator(const ir::BasicBlock* bb) {
    auto found_itr = nodes_.find(bb->id());
    if (found_itr == nodes_.end()) return end();
    return iterator(&found_itr->second);
  }

  // Depth first iterators.
  // Traverse the dominator tree in a depth first pre-order.
  // The pseudo-block is ignored.
  iterator begin() { return ++iterator(GetRoot()); }
  iterator end() { return iterator(); }
  const_iterator begin() const { return cbegin(); }
  const_iterator end() const { return cend(); }
  const_iterator cbegin() const { return ++const_iterator(GetRoot()); }
  const_iterator cend() const { return const_iterator(); }

  // Traverse the dominator tree in a depth first post-order.
  // The pseudo-block is ignored.
  post_iterator post_begin() { return post_iterator::begin(GetRoot()); }
  post_iterator post_end() { return post_iterator::end(GetRoot()); }
  const_post_iterator post_begin() const { return post_cbegin(); }
  const_post_iterator post_end() const { return post_cend(); }
  const_post_iterator post_cbegin() const {
    return const_post_iterator::begin(GetRoot());
  }
  const_post_iterator post_cend() const {
    return const_post_iterator::end(GetRoot());
  }

  // Ranged iterators.
  inline ir::IteratorRange<iterator> preorder() {
    return ir::make_range(begin(), end());
  }
  inline ir::IteratorRange<const_iterator> preorder() const {
    return ir::make_range(cbegin(), cend());
  }
  inline ir::IteratorRange<const_iterator> cpreorder() const {
    return ir::make_range(cbegin(), cend());
  }

  inline ir::IteratorRange<post_iterator> postorder() {
    return ir::make_range(post_begin(), post_end());
  }
  inline ir::IteratorRange<const_post_iterator> postorder() const {
    return ir::make_range(post_cbegin(), post_cend());
  }
  inline ir::IteratorRange<const_post_iterator> cpostorder() const {
    return ir::make_range(post_cbegin(), post_cend());
  }

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
  // Tree nodes are visited in a depth first pre-order.
  bool Visit(std::function<bool(DominatorTreeNode*)> func) {
    for (auto n : *this) {
      if (!func(&n)) return false;
    }
    return true;
  }

  // Applies the std::function |func| to all nodes in the dominator tree.
  // Tree nodes are visited in a depth first pre-order.
  bool Visit(std::function<bool(const DominatorTreeNode*)> func) const {
    for (auto n : *this) {
      if (!func(&n)) return false;
    }
    return true;
  }

  // Returns the DominatorTreeNode associated with the basic block |bb|.
  // If the |bb| is unknown to the dominator tree, it returns null.
  inline DominatorTreeNode* operator[](ir::BasicBlock* bb) {
    return (*this)[bb->id()];
  }
  // Returns the DominatorTreeNode associated with the basic block |bb|.
  // If the |bb| is unknown to the dominator tree, it returns null.
  inline const DominatorTreeNode* operator[](ir::BasicBlock* bb) const {
    return (*this)[bb->id()];
  }

  // Returns the DominatorTreeNode associated with the basic block id |id|.
  // If the id |id| is unknown to the dominator tree, it returns null.
  inline DominatorTreeNode* operator[](uint32_t id) {
    DominatorTreeNodeMap::iterator node_iter = nodes_.find(id);
    if (node_iter == nodes_.end()) {
      return nullptr;
    }
    return &node_iter->second;
  }
  // Returns the DominatorTreeNode associated with the basic block id |id|.
  // If the id |id| is unknown to the dominator tree, it returns null.
  inline const DominatorTreeNode* operator[](uint32_t id) const {
    DominatorTreeNodeMap::const_iterator node_iter = nodes_.find(id);
    if (node_iter == nodes_.end()) {
      return nullptr;
    }
    return &node_iter->second;
  }

 private:
  // Adds the basic block |bb| to the tree structure if it doesn't already
  // exist.
  DominatorTreeNode* GetOrInsertNode(ir::BasicBlock* bb);

  // Wrapper function which gets the list of pairs of each BasicBlocks to its
  // immediately  dominating BasicBlock and stores the result in the the edges
  // parameter.
  //
  // The |edges| vector will contain the dominator tree as pairs of nodes.
  // The first node in the pair is a node in the graph. The second node in the
  // pair is its immediate dominator.
  // The root of the tree has themself as immediate dominator.
  void GetDominatorEdges(
      const ir::Function* f, const ir::BasicBlock* dummy_start_node,
      std::vector<std::pair<ir::BasicBlock*, ir::BasicBlock*>>* edges);

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
