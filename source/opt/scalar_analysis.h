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

#ifndef LIBSPIRV_OPT_SCALAR_ANALYSIS_H_
#define LIBSPIRV_OPT_SCALAR_ANALYSIS_H_

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "opt/basic_block.h"
#include "opt/instruction.h"
#include "opt/ir_context.h"
namespace spvtools {
namespace opt {

// ScalarEvolution
class SENode {
 public:
  enum SENodeType { Constant, Phi, Add, Negative, Unknown };

  explicit SENode(const ir::Instruction* inst)
      : result_id_(inst->result_id()),
        unique_id_(inst->unique_id()),
        parent_(nullptr),
        is_unknown_(false) {}

  explicit SENode(uint32_t unique_id)
      : result_id_(0),
        unique_id_(unique_id),
        parent_(nullptr),
        is_unknown_(false) {}

  virtual SENodeType GetType() const = 0;

  virtual ~SENode() {}

  inline void AddChild(SENode* child) {
    children_.push_back(child);
    child->SetParent(this);
  }

  inline virtual void SetParent(SENode* parent) { parent_ = parent; }

  inline void MarkAsNonConstant() {
    is_constant = false;
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

  virtual bool FoldToSingleValue(int64_t*) const { return false; }

  virtual bool IsUnknown() const { return false; }

  bool ContainsUnknown() const { return is_unknown_; }

  inline void MarkAsUnknown() {
    is_unknown_ = true;
    if (parent_) parent_->MarkAsUnknown();
  }

 protected:
  uint32_t result_id_;
  uint32_t unique_id_;

  SENode* parent_;
  std::vector<SENode*> children_;
  // Are all child nodes constant. Defualts to true and should be set to false
  // when a child node is added which is not constant.
  bool is_constant;
  bool is_unknown_;
};

class SEConstantNode : public SENode {
 public:
  explicit SEConstantNode(const ir::Instruction* inst, int64_t value)
      : SENode(inst), literal_value_(value) {}

  SENodeType GetType() const final { return Constant; }

  bool FoldToSingleValue(int64_t* value_to_return) const override {
    *value_to_return = literal_value_;
    return true;
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

  bool FoldToSingleValue(int64_t* val) const final {
    *val = 0;
    return true;
  }

 private:
  // Each child node of this node will be the value parameters to the phi. This
  // map maintains a list of the blocks the values came from to their child
  // index.
  std::map<uint32_t, size_t> incoming_blocks_;
};

class SEAddNode : public SENode {
 public:
  explicit SEAddNode(const ir::Instruction* inst) : SENode(inst) {}
  explicit SEAddNode(uint32_t unique_id) : SENode(unique_id) {}
  SENodeType GetType() const final { return Add; }

  bool FoldToSingleValue(int64_t* value_to_return) const override {
    int64_t val = 0;

    for (SENode* child : children_) {
      int64_t child_value = 0;
      if (!child->FoldToSingleValue(&child_value)) {
        return false;
      }
      val += child_value;
    }

    *value_to_return = val;
    return true;
  }
};

class SENegative : public SENode {
 public:
  explicit SENegative(uint32_t unique_id) : SENode(unique_id) {}

  bool FoldToSingleValue(int64_t* value_to_return) const override {
    int64_t val = 0;

    SENode* child = children_[0];

    if (!child->FoldToSingleValue(&val)) {
      return false;
    }
    *value_to_return = -val;
    return true;
  }

  SENodeType GetType() const final { return Negative; }
};

class SEUnknown : public SENode {
 public:
  explicit SEUnknown(const ir::Instruction* inst) : SENode(inst) {}

  void SetParent(SENode* parent) final {
    SENode::SetParent(parent);
    parent_->MarkAsUnknown();
  }

  bool IsUnknown() const final { return true; }
  SENodeType GetType() const final { return Unknown; }
};

class ScalarEvolutionAnalysis {
 public:
  explicit ScalarEvolutionAnalysis(ir::IRContext* context)
      : context_(context) {}

  ~ScalarEvolutionAnalysis() {
    for (auto& pair : scalar_evolutions_) {
      delete pair.second;
    }
  }

  void DumpAsDot(std::ostream& out_stream) {
    for (auto& pair : scalar_evolutions_) {
      pair.second->DumpDot(out_stream);
    }
  }

  SENode* CreateNegation(SENode* operand);

  SENode* CreateAddNode(SENode* operand_1, SENode* operand_2);

  SENode* CreateSubtraction(SENode* operand_1, SENode* operand_2);

  SENode* AnalyzeInstruction(const ir::Instruction* inst);

  SENode* AnalyzeConstant(const ir::Instruction* inst);
  SENode* AnalyzeAddOp(const ir::Instruction* add);

  SENode* AnalyzePhiInstruction(const ir::Instruction* phi);

  bool CanProveEqual(const SENode& source, const SENode& destination);
  bool CanProveNotEqual(const SENode& source, const SENode& destination);

 private:
  ir::IRContext* context_;
  std::map<uint32_t, SENode*> scalar_evolutions_;
};

}  // namespace ir
}  // namespace spvtools

#endif  // LIBSPIRV_OPT_SCALAR_ANALYSIS_H__
