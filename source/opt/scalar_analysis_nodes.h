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


namespace spvtools {
namespace opt {

// ScalarEvolution
class SENode {
 public:
  enum SENodeType {
    Constant,
    Phi,
    Add,
    Multiply,
    Divide,
    Negative,
    Load,
    Unknown
  };

  explicit SENode(const ir::Instruction* inst)
      : result_id_(inst->result_id()),
        unique_id_(inst->unique_id()),
        parent_(nullptr),
        can_fold_to_constant_(true),
        is_unknown_(false) {}

  explicit SENode(uint32_t unique_id)
      : result_id_(0),
        unique_id_(unique_id),
        parent_(nullptr),
        can_fold_to_constant_(true),
        is_unknown_(false) {}

  virtual SENodeType GetType() const = 0;

  virtual ~SENode() {}

  inline void AddChild(SENode* child) {
    children_.push_back(child);
    child->SetParent(this);
    if (!child->can_fold_to_constant_) {
      this->MarkAsNonConstant();
    }
  }

  inline virtual void SetParent(SENode* parent) { parent_ = parent; }

  inline void MarkAsNonConstant() {
    can_fold_to_constant_ = false;
    if (parent_) parent_->MarkAsNonConstant();
  }

  inline uint32_t UniqueID() const { return unique_id_; }
  inline uint32_t ResultID() const { return result_id_; }

  std::string AsString() const {
    switch (GetType()) {
      case Constant:
        return "Constant";
      case Phi:
        return "Phi";
      case Add:
        return "Add";
      case Negative:
        return "Negative";
      case Multiply:
        return "Multiply";
      case Load:
        return "Load";
      case Divide:
        return "Division";
      case Unknown:
        return "Unknown";
    }
    return "NULL";
  }

  void DumpDot(std::ostream& out) {
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

  inline void MarkAsUnknown() {
    is_unknown_ = true;
    if (parent_) parent_->MarkAsUnknown();
  }

  bool operator==(const SENode& other) const {
    if (!other.CanFoldToConstant() || !this->CanFoldToConstant()) return false;
    return this->FoldToSingleValue() == other.FoldToSingleValue();
  }

  bool operator!=(const SENode& other) const {
    if (!other.CanFoldToConstant() || !this->CanFoldToConstant()) return false;

    return this->FoldToSingleValue() != other.FoldToSingleValue();
  }


 protected:
  uint32_t result_id_;
  uint32_t unique_id_;

  SENode* parent_;
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

  SENodeType GetType() const final { return Constant; }

  int64_t FoldToSingleValue() const override {
       return literal_value_;
  }

 protected:
  int64_t literal_value_;
};

class SEPhiNode : public SENode {
 public:
  explicit SEPhiNode(const ir::Instruction* inst) : SENode(inst) {}

  SENodeType GetType() const final { return Phi; }

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

  inline const SENode* GetTripCount() const { return step_operation_; }

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
};

class SEMultiplyNode : public SENode {
 public:
  explicit SEMultiplyNode(const ir::Instruction* inst) : SENode(inst) {}
  explicit SEMultiplyNode(uint32_t unique_id) : SENode(unique_id) {}
  SENodeType GetType() const final { return Add; }

  int64_t FoldToSingleValue() const override;
};

class SEDivideNode : public SENode {
 public:
  explicit SEDivideNode(const ir::Instruction* inst) : SENode(inst) {}
  explicit SEDivideNode(uint32_t unique_id) : SENode(unique_id) {}
  SENodeType GetType() const final { return Add; }

  int64_t FoldToSingleValue() const override;
};

class SENegative : public SENode {
 public:
  explicit SENegative(uint32_t unique_id) : SENode(unique_id) {}

  int64_t FoldToSingleValue() const override {

   return -children_[0]->FoldToSingleValue();
  }

  SENodeType GetType() const final { return Negative; }
};

class SELoad : public SENode {
 public:
  explicit SELoad(const ir::Instruction* inst) : SENode(inst) {
    can_fold_to_constant_ = false;
  }

  SENodeType GetType() const final { return Load; }
};

class SEUnknown : public SENode {
 public:
  explicit SEUnknown(const ir::Instruction* inst) : SENode(inst) {
    can_fold_to_constant_ = false;
  }

  void SetParent(SENode* parent) final {
    SENode::SetParent(parent);
    parent_->MarkAsUnknown();
  }

  bool IsUnknown() const final { return true; }
  SENodeType GetType() const final { return Unknown; }
};


// This class represents an expression with respect to the loop bounds.
class SELoopRecurrence: public SENode {
 public:
  bool IsAffine() const {
    if (children_.size() != 2) return false;

    SENode* lhs = children_[0];
    SENode* rhs = children_[1];

    // Expected to be in the form of a*x + c
    if ((rhs->CanFoldToConstant() || lhs->GetType() == Multiply) &&
        (lhs->CanFoldToConstant() || rhs->GetType() == Multiply)) {
      return true;
    }

    return false;
  }
};


}  // namespace opt
}  // namespace spvtools
#endif
