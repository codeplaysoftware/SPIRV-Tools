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
    Divide,
    Negative,
    ValueUnknown,
    Unknown
  };

  explicit SENode(const ir::Instruction* inst)
      : result_id_(inst->result_id()),
        unique_id_(inst->unique_id()),
        can_fold_to_constant_(true),
        is_unknown_(false) {}

  explicit SENode(uint32_t unique_id)
      : result_id_(0),
        unique_id_(unique_id),
        can_fold_to_constant_(true),
        is_unknown_(false) {}

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
  inline uint32_t ResultID() const { return result_id_; }

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
      case Divide:
        return "Division";
      case Unknown:
        return "Unknown";
    }
    return "NULL";
  }

  void DumpDot(std::ostream& out) const {
    out << UniqueID() << " [label=\"" << AsString() << " " << ResultID()
        << "\"]\n";
    for (const SENode* child : children_) {
      out << UniqueID() << " -> " << child->UniqueID() << " \n";
    }
  }

  virtual int64_t FoldToSingleValue() const {
    assert(can_fold_to_constant_);
    return 0;
  }

  virtual bool IsUnknown() const { return false; }

  bool ContainsUnknown() const { return is_unknown_; }

  bool CanFoldToConstant() const { return can_fold_to_constant_; }

  inline void MarkAsUnknown() { is_unknown_ = true; }

  bool operator==(const SENode& other) const;

  bool operator!=(const SENode& other) const;

  inline SENode* GetChild(size_t index) { return children_[index]; }

  using iterator = std::vector<SENode*>::iterator;
  using const_iterator = std::vector<SENode*>::const_iterator;

  using graph_iterator = TreeDFIterator<SENode>;
  using const_graph_iterator = TreeDFIterator<const SENode>;

  iterator begin() { return children_.begin(); }
  iterator end() { return children_.end(); }

  const_iterator begin() const { return children_.cbegin(); }
  const_iterator end() const { return children_.cend(); }
  const_iterator cbegin() { return children_.cbegin(); }
  const_iterator cend() { return children_.cend(); }

  graph_iterator graph_begin() { return graph_iterator(this); }
  graph_iterator graph_end() { return graph_iterator(); }
  const_graph_iterator graph_begin() const { return graph_cbegin(); }
  const_graph_iterator graph_end() const { return graph_cend(); }
  const_graph_iterator graph_cbegin() const {
    return const_graph_iterator(this);
  }
  const_graph_iterator graph_cend() const { return const_graph_iterator(); }

  virtual SENode* Clone(uint32_t) const = 0;

  const std::vector<SENode*>& GetChildren() const { return children_; }

 protected:
  uint32_t result_id_;
  uint32_t unique_id_;

  std::vector<SENode*> children_;
  // Are all child nodes constant. Defualts to true and should be set to false
  // when a child node is added which is not constant.
  bool can_fold_to_constant_;
  bool is_unknown_;
};

class SEConstantNode : public SENode {
 public:
  explicit SEConstantNode(const ir::Instruction* inst, int64_t value)
      : SENode(inst), literal_value_(value) {}

  SEConstantNode(uint32_t unique_id, int64_t value)
      : SENode(unique_id), literal_value_(value) {}

  SENodeType GetType() const final { return Constant; }

  int64_t FoldToSingleValue() const override { return literal_value_; }

  SENode* Clone(uint32_t id) const final {
    return new SEConstantNode(id, literal_value_);
  }

 protected:
  int64_t literal_value_;
};

struct SENodeHash {
  size_t operator()(const SENode* node) const {
    SENode::SENodeType type = node->GetType();
    int64_t literal_value = 0;
    if (node->GetType() == SENode::Constant)
      literal_value = node->FoldToSingleValue();

    const std::vector<SENode*>& children = node->GetChildren();

    int new_type = static_cast<int>(type) << 16;
    int64_t new_literal = (literal_value + 1) << 2;

    size_t resulting_hash =
        std::hash<int>{}(new_type) ^ std::hash<int64_t>{}(new_literal);

    for (const SENode* child : children) {
      resulting_hash ^= std::hash<const SENode*>{}(child);
    }

    return resulting_hash;
  }
};

class SERecurrentNode : public SENode {
 public:
  explicit SERecurrentNode(const ir::Instruction* inst) : SENode(inst) {}
  explicit SERecurrentNode(uint32_t unique_id) : SENode(unique_id) {}

  SENodeType GetType() const final { return RecurrentExpr; }

  inline void AddChild(SENode* child, uint32_t index) {
    // Add the block that value came from.
    incoming_blocks_[index] = children_.size();
    // Add the value.
    SENode::AddChild(child);
  }

  inline SENode* GetValueFromEdge(uint32_t edge) const {
    auto itr = incoming_blocks_.find(edge);
    if (itr == incoming_blocks_.end()) return nullptr;

    return children_[itr->second];
  }

  inline void AddInitalizer(SENode* child) {
    initalizer_ = child;
    SENode::AddChild(child);
  }

  inline void AddTripCount(SENode* child) {
    step_operation_ = child;
    SENode::AddChild(child);
  }

  inline const SENode* GetInitalizer() const { return initalizer_; }
  inline SENode* GetInitalizer() { return initalizer_; }

  inline const SENode* GetTripCount() const { return step_operation_; }
  inline SENode* GetTripCount() { return step_operation_; }

  SENode* Clone(uint32_t id) const final { return new SERecurrentNode(id); }

 private:
  // Each child node of this node will be the value parameters to the phi. This
  // map maintains a list of the blocks the values came from to their child
  // index.
  std::map<uint32_t, size_t> incoming_blocks_;

  SENode* initalizer_;
  SENode* step_operation_;
};

class SEAddNode : public SENode {
 public:
  explicit SEAddNode(const ir::Instruction* inst) : SENode(inst) {}
  explicit SEAddNode(uint32_t unique_id) : SENode(unique_id) {}
  SENodeType GetType() const final { return Add; }

  int64_t FoldToSingleValue() const override;

  SENode* Clone(uint32_t id) const final { return new SEAddNode(id); }
};

class SEMultiplyNode : public SENode {
 public:
  explicit SEMultiplyNode(const ir::Instruction* inst) : SENode(inst) {}
  explicit SEMultiplyNode(uint32_t unique_id) : SENode(unique_id) {}
  SENodeType GetType() const final { return Add; }

  int64_t FoldToSingleValue() const override;

  SENode* Clone(uint32_t id) const final { return new SEMultiplyNode(id); }
};

class SEDivideNode : public SENode {
 public:
  explicit SEDivideNode(const ir::Instruction* inst) : SENode(inst) {}
  explicit SEDivideNode(uint32_t unique_id) : SENode(unique_id) {}
  SENodeType GetType() const final { return Add; }

  int64_t FoldToSingleValue() const override;
  SENode* Clone(uint32_t id) const final { return new SEDivideNode(id); }
};

class SENegative : public SENode {
 public:
  explicit SENegative(uint32_t unique_id) : SENode(unique_id) {}

  int64_t FoldToSingleValue() const override {
    return -children_[0]->FoldToSingleValue();
  }
  SENode* Clone(uint32_t id) const final { return new SENegative(id); }

  SENodeType GetType() const final { return Negative; }
};

class SEValueUnknown : public SENode {
 public:
  explicit SEValueUnknown(const ir::Instruction* inst) : SENode(inst) {
    can_fold_to_constant_ = false;
  }

  explicit SEValueUnknown(uint32_t unique_id) : SENode(unique_id) {
    can_fold_to_constant_ = false;
  }

  SENode* Clone(uint32_t id) const final { return new SEValueUnknown(id); }

  SENodeType GetType() const final { return ValueUnknown; }
};

class SECantCompute : public SENode {
 public:
  explicit SECantCompute(const ir::Instruction* inst) : SENode(inst) {
    can_fold_to_constant_ = false;
  }

  explicit SECantCompute(uint32_t unique_id) : SENode(unique_id) {
    can_fold_to_constant_ = false;
  }

  SENode* Clone(uint32_t id) const final { return new SECantCompute(id); }

  bool IsUnknown() const final { return true; }
  SENodeType GetType() const final { return Unknown; }
};

}  // namespace opt
}  // namespace spvtools
#endif
