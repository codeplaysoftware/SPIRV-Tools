// Copyright (c) 2018 Google LLC.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASI,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SOURCE_OPT_SCALAR_ANALYSIS_NODES_H_
#define SOURCE_OPT_SCALAR_ANALYSIS_NODES_H_

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "opt/tree_iterator.h"

namespace spvtools {
namespace ir {
class Loop;
}  // namespace ir

namespace opt {

class ScalarEvolutionAnalysis;
class SEConstantNode;
class SERecurrentNode;
class SEAddNode;
class SEMultiplyNode;
class SENegative;
class SEValueUnknown;
class SECantCompute;

// Abstract class representing a node in the scalar evolution DAG. Each node
// contains a vector of pointers to its children and each subclass of SENode
// implements GetType and an As method to allow casting. SENodes can be hashed
// using the SENodeHash functor. The vector of children is sorted when a node is
// added. This is important as it allows the hash of X+Y to be the same as Y+X.
class SENode {
 protected:
  // Context structure for printing SENode expression.
  struct PrintContext {
    PrintContext() : next_symbol_('a') {}
    // Maps a loop iterator to a symbol a, b, c, d etc.
    std::unordered_map<const ir::Loop*, std::string> loop_iterator_;
    char next_symbol_;
  };

 public:
  enum SENodeType {
    Constant,
    RecurrentExpr,
    Add,
    Multiply,
    Negative,
    ValueUnknown,
    CanNotCompute
  };

  explicit SENode(opt::ScalarEvolutionAnalysis* parent_analysis)
      : parent_analysis_(parent_analysis) {}

  virtual SENodeType GetType() const = 0;

  virtual ~SENode() {}

  inline void AddChild(SENode* child) {
    children_.push_back(child);

    // Children are sorted so the hashing
    std::sort(children_.begin(), children_.end());
  }

  // Get the type as an std::string. This is used to represent the node in the
  // dot output and is used to hash the type as well.
  std::string AsString() const;

  // Dump the SENode and its immediate children, if |recurse| is true then it
  // will recurse through all children to print the DAG starting from this node
  // as a root.
  void DumpDot(std::ostream& out, bool recurse = false) const;

  // Dump the SENode as a mathematical expression.
  void Dump() const { Dump(&std::cout); }

  // Dump the SENode as a mathematical expression.
  void Dump(std::ostream* out) const {
    PrintContext context;
    Dump(out, &context);
    *out << "\n";
  }
  // Dump the SENode as a mathematical expression.
  virtual void Dump(std::ostream* out, PrintContext* context) const = 0;

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

  // Collect all the recurrent nodes in this SENode
  std::vector<SERecurrentNode*> CollectRecurrentNodes() {
    std::vector<SERecurrentNode*> recurrent_nodes{};

    if (auto recurrent_node = AsSERecurrentNode()) {
      recurrent_nodes.push_back(recurrent_node);
    }

    for (auto child : GetChildren()) {
      auto child_recurrent_nodes = child->CollectRecurrentNodes();
      recurrent_nodes.insert(recurrent_nodes.end(),
                             child_recurrent_nodes.begin(),
                             child_recurrent_nodes.end());
    }

    return recurrent_nodes;
  }

  // Collect all the value unknown nodes in this SENode
  std::vector<SEValueUnknown*> CollectValueUnknownNodes() {
    std::vector<SEValueUnknown*> value_unknown_nodes{};

    if (auto value_unknown_node = AsSEValueUnknown()) {
      value_unknown_nodes.push_back(value_unknown_node);
    }

    for (auto child : GetChildren()) {
      auto child_value_unknown_nodes = child->CollectValueUnknownNodes();
      value_unknown_nodes.insert(value_unknown_nodes.end(),
                                 child_value_unknown_nodes.begin(),
                                 child_value_unknown_nodes.end());
    }

    return value_unknown_nodes;
  }

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

// Implements a casting method for each type.
#define DeclareCastMethod(target)                  \
  virtual target* As##target() { return nullptr; } \
  virtual const target* As##target() const { return nullptr; }
  DeclareCastMethod(SEConstantNode);
  DeclareCastMethod(SERecurrentNode);
  DeclareCastMethod(SEAddNode);
  DeclareCastMethod(SEMultiplyNode);
  DeclareCastMethod(SENegative);
  DeclareCastMethod(SEValueUnknown);
  DeclareCastMethod(SECantCompute);
#undef DeclareCastMethod

  // Get the analysis which has this node in its cache.
  inline opt::ScalarEvolutionAnalysis* GetParentAnalysis() const {
    return parent_analysis_;
  }

 protected:
  std::vector<SENode*> children_;

  opt::ScalarEvolutionAnalysis* parent_analysis_;
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
  SEConstantNode(opt::ScalarEvolutionAnalysis* parent_analysis, int64_t value)
      : SENode(parent_analysis), literal_value_(value) {}

  SENodeType GetType() const final { return Constant; }

  int64_t FoldToSingleValue() const { return literal_value_; }

  SEConstantNode* AsSEConstantNode() override { return this; }
  const SEConstantNode* AsSEConstantNode() const override { return this; }

  using SENode::Dump;
  // Dump the SENode as a mathematical expression.
  void Dump(std::ostream* out, SENode::PrintContext*) const override {
    *out << literal_value_;
  }

 protected:
  int64_t literal_value_;
};

// A node represeting a recurrent expression in the code. A recurrent expression
// is an expression with a loop variant as one of its terms, such as an
// induction variable.
class SERecurrentNode : public SENode {
 public:
  SERecurrentNode(opt::ScalarEvolutionAnalysis* parent_analysis,
                  const ir::Loop* loop)
      : SENode(parent_analysis), loop_(loop) {}

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

  SERecurrentNode* AsSERecurrentNode() override { return this; }
  const SERecurrentNode* AsSERecurrentNode() const override { return this; }

  using SENode::Dump;
  // Dump the SENode as a mathematical expression.
  void Dump(std::ostream* out, PrintContext* context) const override {
    std::string& var = context->loop_iterator_[loop_];
    if (var.empty()) {
      var = context->next_symbol_++;
    }
    *out << "(";
    coefficient_->Dump(out, context);
    *out << ") " << var << " + (";
    step_operation_->Dump(out, context);
    *out << ")";
  }

 private:
  SENode* coefficient_;
  SENode* step_operation_;
  const ir::Loop* loop_;
};

// A node representing an addition operation between child nodes.
class SEAddNode : public SENode {
 public:
  explicit SEAddNode(opt::ScalarEvolutionAnalysis* parent_analysis)
      : SENode(parent_analysis) {}

  SENodeType GetType() const final { return Add; }

  SEAddNode* AsSEAddNode() override { return this; }
  const SEAddNode* AsSEAddNode() const override { return this; }

  using SENode::Dump;
  // Dump the SENode as a mathematical expression.
  void Dump(std::ostream* out, PrintContext* context) const override {
    *out << "(";
    children_[0]->Dump(out, context);
    *out << ")";
    std::for_each(++children_.begin(), children_.end(),
                  [out, context](SENode* c) {
                    *out << " + (";
                    c->Dump(out, context);
                    *out << ")";
                  });
  }
};

// A node representing a multiply operation between child nodes.
class SEMultiplyNode : public SENode {
 public:
  explicit SEMultiplyNode(opt::ScalarEvolutionAnalysis* parent_analysis)
      : SENode(parent_analysis) {}

  SENodeType GetType() const final { return Multiply; }

  SEMultiplyNode* AsSEMultiplyNode() override { return this; }
  const SEMultiplyNode* AsSEMultiplyNode() const override { return this; }

  using SENode::Dump;
  // Dump the SENode as a mathematical expression.
  void Dump(std::ostream* out, PrintContext* context) const override {
    *out << "(";
    children_[0]->Dump(out, context);
    *out << ")";
    std::for_each(++children_.begin(), children_.end(),
                  [out, context](SENode* c) {
                    *out << " * (";
                    c->Dump(out, context);
                    *out << ")";
                  });
  }
};

// A node representing a unary negative operation.
class SENegative : public SENode {
 public:
  explicit SENegative(opt::ScalarEvolutionAnalysis* parent_analysis)
      : SENode(parent_analysis) {}

  SENodeType GetType() const final { return Negative; }

  SENegative* AsSENegative() override { return this; }
  const SENegative* AsSENegative() const override { return this; }

  using SENode::Dump;
  // Dump the SENode as a mathematical expression.
  void Dump(std::ostream* out, PrintContext* context) const override {
    *out << "- (";
    children_[0]->Dump(out, context);
    *out << ")";
  }
};

// A node representing a value which we do not know the value of, such as a load
// instruction.
class SEValueUnknown : public SENode {
 public:
  explicit SEValueUnknown(opt::ScalarEvolutionAnalysis* parent_analysis,
                          uint32_t value)
      : SENode(parent_analysis), value_(value) {}

  SENodeType GetType() const final { return ValueUnknown; }

  SEValueUnknown* AsSEValueUnknown() override { return this; }
  const SEValueUnknown* AsSEValueUnknown() const override { return this; }

  using SENode::Dump;
  // Dump the SENode as a mathematical expression.
  void Dump(std::ostream* out, PrintContext*) const override {
    *out << "%" << value_;
  }

  uint32_t GetValue() const { return value_; }

 protected:
  uint32_t value_;
};

// A node which we cannot reason about at all.
class SECantCompute : public SENode {
 public:
  explicit SECantCompute(opt::ScalarEvolutionAnalysis* parent_analysis)
      : SENode(parent_analysis) {}

  SENodeType GetType() const final { return CanNotCompute; }

  SECantCompute* AsSECantCompute() override { return this; }
  const SECantCompute* AsSECantCompute() const override { return this; }

  using SENode::Dump;
  // Dump the SENode as a mathematical expression.
  void Dump(std::ostream* out, PrintContext*) const override {
    *out << "Cannot compute";
  }
};

}  // namespace opt
}  // namespace spvtools
#endif  // SOURCE_OPT_SCALAR_ANALYSIS_NODES_H_
