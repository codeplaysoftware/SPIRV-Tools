// Copyright (c) 2018 Google LLC.
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

#ifndef LIBSPIRV_OPT_SCALAR_ANALYSIS_NODES_H_
#define LIBSPIRV_OPT_SCALAR_ANALYSIS_NODES_H_

#include "tree_iterator.h"

namespace spvtools {
namespace opt {

// ScalarEvolution
class SENode {
 public:
  enum SENodeType {
    Constant = 1,
    RecurrentExpr,
    Add,
    Multiply,
    Negative,
    ValueUnknown,
    CanNotCompute
  };

  SENode() : unique_id_(NodeCount++), can_fold_to_constant_(true) {}

  virtual SENodeType GetType() const = 0;

  virtual ~SENode() {}

  inline void AddChild(SENode* child) {
    children_.push_back(child);

    std::sort(children_.begin(), children_.end());
    if (!child->can_fold_to_constant_) {
      this->MarkAsNonConstant();
    }
  }

  inline void MarkAsNonConstant() { can_fold_to_constant_ = false; }

  inline uint32_t UniqueID() const { return unique_id_; }

  std::string AsString() const {
    switch (GetType()) {
      case Constant:
        return "Constant";
      case RecurrentExpr:
        return "RecurrentExpr";
      case Add:
        return "Add";
      case Negative:
        return "Negative";
      case Multiply:
        return "Multiply";
      case ValueUnknown:
        return "Value Unknown";
      case CanNotCompute:
        return "Can not compute";
    }
    return "NULL";
  }

  void DumpDot(std::ostream& out, bool recurse = false) const {
    out << UniqueID() << " [label=\"" << AsString() << " ";
    if (GetType() == SENode::Constant) {
      out << "\nwith value: " << FoldToSingleValue();
    }
    out << "\"]\n";
    for (const SENode* child : children_) {
      out << UniqueID() << " -> " << child->UniqueID() << " \n";
      if (recurse) child->DumpDot(out, true);
    }
  }

  virtual int64_t FoldToSingleValue() const {
    assert(can_fold_to_constant_);
    return 0;
  }

  bool CanFoldToConstant() const { return can_fold_to_constant_; }

  // Checks if two nodes are the same by hashing them.
  bool operator==(const SENode& other) const;

  // Checks if two nodes are not the same by comparing the hashes.
  bool operator!=(const SENode& other) const;

  // Return the child node at |index|.
  inline SENode* GetChild(size_t index) { return children_[index]; }

  // Iterator to iterate over the child nodes.
  using iterator = std::vector<SENode*>::iterator;
  using const_iterator = std::vector<SENode*>::const_iterator;

  // Iterator to iterate over the entire DAG. Even though we are using the tree
  // iterator it should still be safe to iterate over. However, nodes with
  // multiple parents will be visited multiple times, unlike in a tree.
  using graph_iterator = TreeDFIterator<SENode>;
  using const_graph_iterator = TreeDFIterator<const SENode>;

  // Iterate over immediate child nodes.
  iterator begin() { return children_.begin(); }
  iterator end() { return children_.end(); }

  // Constant overloads for iterating over immediate child nodes.
  const_iterator begin() const { return children_.cbegin(); }
  const_iterator end() const { return children_.cend(); }
  const_iterator cbegin() { return children_.cbegin(); }
  const_iterator cend() { return children_.cend(); }

  // Iterate over all child nodes in the graph.
  graph_iterator graph_begin() { return graph_iterator(this); }
  graph_iterator graph_end() { return graph_iterator(); }
  const_graph_iterator graph_begin() const { return graph_cbegin(); }
  const_graph_iterator graph_end() const { return graph_cend(); }
  const_graph_iterator graph_cbegin() const {
    return const_graph_iterator(this);
  }
  const_graph_iterator graph_cend() const { return const_graph_iterator(); }

  // Return the vector of immediate children.
  const std::vector<SENode*>& GetChildren() const { return children_; }
  std::vector<SENode*>& GetChildren() { return children_; }

 protected:
  // Each node is assigned a unique id.
  uint32_t unique_id_;

  // Generate a unique id for each node in the graph by maintaining a count of
  // nodes currently in the graph.
  static uint32_t NodeCount;

  std::vector<SENode*> children_;

  // Are all child nodes constant. Defualts to true and should be set to false
  // when a child node is added which is not constant.
  bool can_fold_to_constant_;
};

class SEConstantNode : public SENode {
 public:
  explicit SEConstantNode(int64_t value) : literal_value_(value) {}

  SENodeType GetType() const final { return Constant; }

  int64_t FoldToSingleValue() const override { return literal_value_; }

 protected:
  int64_t literal_value_;
};

struct SENodeHash {
  size_t operator()(const std::unique_ptr<SENode>& node) const;
  size_t operator()(const SENode* node) const;
};

class SERecurrentNode : public SENode {
 public:
  SENodeType GetType() const final { return RecurrentExpr; }

  inline void AddCoefficient(SENode* child) {
    coefficient_ = child;
    SENode::AddChild(child);
  }

  inline void AddOffset(SENode* child) {
    step_operation_ = child;
    SENode::AddChild(child);
  }

  inline const SENode* GetCoefficient() const { return coefficient_; }
  inline SENode* GetCoefficient() { return coefficient_; }

  inline const SENode* GetOffset() const { return step_operation_; }
  inline SENode* GetOffset() { return step_operation_; }

 private:
  SENode* coefficient_;
  SENode* step_operation_;
};

class SEAddNode : public SENode {
 public:
  SENodeType GetType() const final { return Add; }

  int64_t FoldToSingleValue() const override;
};

class SEMultiplyNode : public SENode {
 public:
  SENodeType GetType() const final { return Multiply; }

  int64_t FoldToSingleValue() const override;
};

class SENegative : public SENode {
 public:
  int64_t FoldToSingleValue() const override {
    return -children_[0]->FoldToSingleValue();
  }

  SENodeType GetType() const final { return Negative; }
};

class SEValueUnknown : public SENode {
 public:
  SEValueUnknown() : SENode() { can_fold_to_constant_ = false; }

  SENodeType GetType() const final { return ValueUnknown; }
};

class SECantCompute : public SENode {
 public:
  SECantCompute() : SENode() { can_fold_to_constant_ = false; }
  SENodeType GetType() const final { return CanNotCompute; }
};

}  // namespace opt
}  // namespace spvtools
#endif
