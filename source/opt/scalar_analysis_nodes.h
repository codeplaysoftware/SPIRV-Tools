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

  virtual SENodeType GetType() const = 0;

  virtual ~SENode() {}

  inline void AddChild(SENode* child) {
    children_.push_back(child);

    std::sort(children_.begin(), children_.end());
  }

  // Get the type as an std::string. This is used to represent the node in the
  // dot output`
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

  void DumpDot(std::ostream& out, bool recurse = false) const;

  virtual int64_t FoldToSingleValue() const { return 0; }

  // Checks if two nodes are the same by hashing them.
  bool operator==(const SENode& other) const;

  // Checks if two nodes are not the same by comparing the hashes.
  bool operator!=(const SENode& other) const;

  // Return the child node at |index|.
  inline SENode* GetChild(size_t index) { return children_[index]; }
  inline const SENode* GetChild(size_t index) const { return children_[index]; }

  // Iterator to iterate over the child nodes.
  using iterator = std::vector<SENode*>::iterator;
  using const_iterator = std::vector<SENode*>::const_iterator;

  // Iterate over immediate child nodes.
  iterator begin() { return children_.begin(); }
  iterator end() { return children_.end(); }

  // Constant overloads for iterating over immediate child nodes.
  const_iterator begin() const { return children_.cbegin(); }
  const_iterator end() const { return children_.cend(); }
  const_iterator cbegin() { return children_.cbegin(); }
  const_iterator cend() { return children_.cend(); }

  // Iterator to iterate over the entire DAG. Even though we are using the tree
  // iterator it should still be safe to iterate over. However, nodes with
  // multiple parents will be visited multiple times, unlike in a tree.
  using dag_iterator = TreeDFIterator<SENode>;
  using const_dag_iterator = TreeDFIterator<const SENode>;

  // Iterate over all child nodes in the graph.
  dag_iterator graph_begin() { return dag_iterator(this); }
  dag_iterator graph_end() { return dag_iterator(); }
  const_dag_iterator graph_begin() const { return graph_cbegin(); }
  const_dag_iterator graph_end() const { return graph_cend(); }
  const_dag_iterator graph_cbegin() const { return const_dag_iterator(this); }
  const_dag_iterator graph_cend() const { return const_dag_iterator(); }

  // Return the vector of immediate children.
  const std::vector<SENode*>& GetChildren() const { return children_; }
  std::vector<SENode*>& GetChildren() { return children_; }

 protected:
  std::vector<SENode*> children_;
};

// Function object to handle the hashing of SENodes. Hashing algorithm hashes
// the type (as a string), the literal value of any constants, and the child
// pointers which are assumed to be unique.
struct SENodeHash {
  size_t operator()(const std::unique_ptr<SENode>& node) const;
  size_t operator()(const SENode* node) const;
};

// A node representing a constant integer.
class SEConstantNode : public SENode {
 public:
  explicit SEConstantNode(int64_t value) : literal_value_(value) {}

  SENodeType GetType() const final { return Constant; }

  int64_t FoldToSingleValue() const override { return literal_value_; }

 protected:
  int64_t literal_value_;
};

// A node represeting a recurrent expression in the code. A recurrent expression
// is an expression with a loop variant as one of its terms, such as an
// induction variable.
class SERecurrentNode : public SENode {
 public:
  SERecurrentNode(const ir::Loop* loop) : SENode(), loop_(loop) {}

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

  // Return the loop which this recurrent expression is recurring within.
  const ir::Loop* GetLoop() const { return loop_; }

 private:
  SENode* coefficient_;
  SENode* step_operation_;
  const ir::Loop* loop_;
};

// A node representing an addition operation between child nodes.
class SEAddNode : public SENode {
 public:
  SENodeType GetType() const final { return Add; }

  int64_t FoldToSingleValue() const override;
};

// A node representing a multiply operation between child nodes.
class SEMultiplyNode : public SENode {
 public:
  SENodeType GetType() const final { return Multiply; }

  int64_t FoldToSingleValue() const override;
};

// A node representing a unary negative operation.
class SENegative : public SENode {
 public:
  int64_t FoldToSingleValue() const override {
    return -children_[0]->FoldToSingleValue();
  }

  SENodeType GetType() const final { return Negative; }
};

// A node representing a value which we do not know the value of, such as a load
// instruction.
class SEValueUnknown : public SENode {
 public:
  SENodeType GetType() const final { return ValueUnknown; }
};

// A node which we cannot reason about at all.
class SECantCompute : public SENode {
 public:
  SENodeType GetType() const final { return CanNotCompute; }
};

}  // namespace opt
}  // namespace spvtools
#endif
