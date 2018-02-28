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

#ifndef LIBSPIRV_OPT_LOOP_DEPENDENCE_H_
#define LIBSPIRV_OPT_LOOP_DEPENDENCE_H_

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "opt/basic_block.h"
#include "opt/loop_descriptor.h"
#include "opt/tree_iterator.h"

namespace spvtools {
namespace opt {

// ScalarEvolution
class SENode {
 public:
  enum SENodeType { Constant, Phi, Add, Negative, Unknown };

  explicit SENode(const ir::Instruction* inst)
      : result_id_(inst->result_id()), unique_id_(inst->unique_id()), parent_(nullptr), is_unknown_(false) {}

  explicit SENode(uint32_t unique_id)
    : result_id_(0), unique_id_(unique_id), parent_(nullptr), is_unknown_(false) {}

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
  explicit SEConstantNode(const ir::Instruction* inst, int64_t value) : SENode(inst), literal_value_(value) {}

  SENodeType GetType() const final { return Constant; }

  bool FoldToSingleValue(int64_t *value_to_return) const override {
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

  SENode* GetValueFromEdge(uint32_t edge) const {
    auto itr = incoming_blocks_.find(edge);
    if (itr == incoming_blocks_.end()) return nullptr;

    return children_[itr->second];
  }

  bool FoldToSingleValue(int64_t* val) const final { *val = 0; return true; }

 private:
  // Each child node of this node will be the value parameters to the phi. This
  // map maintains a list of the blocks the values came from to their child index.
  std::map<uint32_t, size_t> incoming_blocks_;
};

class SEAddNode : public SENode {
 public:
  explicit SEAddNode(const ir::Instruction* inst) : SENode(inst) {}
  explicit SEAddNode(uint32_t unique_id): SENode(unique_id) {}
  SENodeType GetType() const final { return Add; }

  bool FoldToSingleValue(int64_t *value_to_return) const override {
    int64_t val = 0;

    for (SENode* child : children_) {
      int64_t child_value = 0;
      if(!child->FoldToSingleValue(&child_value)) {
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

  explicit SENegative(uint32_t unique_id):
      SENode(unique_id) {}

  bool FoldToSingleValue(int64_t *value_to_return) const override {
    int64_t val = 0;

    SENode* child = children_[0];

    if(!child->FoldToSingleValue(&val)) {
        return false;
    }
    *value_to_return = -val;
    return true;
  }

  SENodeType GetType() const final { return Negative; }

};


class SEUnknown : public SENode {
  public:

  explicit SEUnknown(const ir::Instruction* inst)
      : SENode(inst) {}

  void SetParent(SENode* parent_) final {
    SENode::SetParent(parent_);
    parent_->MarkAsUnknown();
  }

  bool IsUnknown() const final { return true; }
  SENodeType GetType() const final { return Unknown; }
};




class ScalarEvolutionAnalysis {
 public:
  ScalarEvolutionAnalysis(ir::IRContext* context) : context_(context) {}

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

  SENode* CreateNegation(SENode* operand) {
    SENode* negation_node { new SENegative(context_->TakeNextUniqueId())};

    scalar_evolutions_[negation_node->UniqueID()] = negation_node;
    negation_node->AddChild(operand);
    return negation_node;
  }

  SENode* CreateAddNode(SENode* operand_1, SENode* operand_2) {
    SENode* add_node{new SEAddNode(context_->TakeNextUniqueId())};
    scalar_evolutions_[add_node->UniqueID()] = add_node;

    add_node->AddChild(operand_1);
    add_node->AddChild(operand_2);

    return add_node;
  }

  SENode* AnalyzeInstruction(const ir::Instruction* inst) {
    if (scalar_evolutions_.find(inst->unique_id()) != scalar_evolutions_.end())
      return scalar_evolutions_[inst->unique_id()];

    SENode* output = nullptr;
    switch (inst->opcode()) {
      case SpvOp::SpvOpPhi: {
        output = AnalyzePhiInstruction(inst);
        break;
      }
      case SpvOp::SpvOpConstant: {
        output = AnalyzeConstant(inst);
        break;
      }
      case SpvOp::SpvOpIAdd: {
        output = AnalyzeAddOp(inst);
        break;
      }
      default: {
        output = new SEUnknown(inst);
        scalar_evolutions_[inst->unique_id()] = output;
        break;
      }
    };
    return output;
  }

  SENode* AnalyzeConstant(const ir::Instruction* inst) {
    if (inst->NumInOperands() != 1) {
      assert(false);
    }

    int64_t value = 0;

    const opt::analysis::Constant* constant =
        context_->get_constant_mgr()->FindDeclaredConstant(
            inst->result_id());

    if (constant->AsIntConstant()->type()->AsInteger()->IsSigned()) {
      value = constant->AsIntConstant()->GetS32BitValue();
    } else {
      value = constant->AsIntConstant()->GetU32BitValue();
    }

    SENode* constant_node{new SEConstantNode(inst, value)};
    scalar_evolutions_[inst->unique_id()] = constant_node;
    return constant_node;
  }

  SENode* AnalyzeAddOp(const ir::Instruction* add) {
    opt::analysis::DefUseManager* def_use = context_->get_def_use_mgr();
    SENode* add_node{new SEAddNode(add)};
    scalar_evolutions_[add->unique_id()] = add_node;

    add_node->AddChild(
        AnalyzeInstruction(def_use->GetDef(add->GetSingleWordInOperand(0))));
    add_node->AddChild(
        AnalyzeInstruction(def_use->GetDef(add->GetSingleWordInOperand(1))));
    return add_node;
  }

  SENode* AnalyzePhiInstruction(const ir::Instruction* phi) {
    SEPhiNode* phi_node{new SEPhiNode(phi)};
    scalar_evolutions_[phi->unique_id()] = phi_node;

    opt::analysis::DefUseManager* def_use = context_->get_def_use_mgr();

    for (uint32_t i = 0; i < phi->NumInOperands(); i += 2) {
      uint32_t value_id = phi->GetSingleWordInOperand(i);
      uint32_t incoming_label_id = phi->GetSingleWordInOperand(i + 1);

      ir::Instruction* value_inst = def_use->GetDef(value_id);

      phi_node->AddChild(
          AnalyzeInstruction(value_inst), incoming_label_id);
    }

    return phi_node;
  }

  bool CanProveEqual(const SENode& source, const SENode& destination) {
    int64_t source_value = 0;
    if (!source.FoldToSingleValue(&source_value)) {
      return false;
    }

    int64_t destination_value = 0;
    if (!destination.FoldToSingleValue(&destination_value)) {
      return false;
    }

    return source_value == destination_value;
  }

  bool CanProveNotEqual(const SENode& source, const SENode& destination) {
    int64_t source_value = 0;
    if (!source.FoldToSingleValue(&source_value)) {
      return false;
    }

    int64_t destination_value = 0;
    if (!destination.FoldToSingleValue(&destination_value)) {
      return false;
    }

    return source_value != destination_value;
  }

 private:
  ir::IRContext* context_;
  std::map<uint32_t, SENode*> scalar_evolutions_;
};

class LoopDependenceAnalysis {
 public:

  LoopDependenceAnalysis(ir::IRContext* context, const ir::Loop& loop)
      : context_(context), loop_(loop), scalar_evolution_(context){};

  bool GetDependence(const ir::Instruction* source,
                     const ir::Instruction* destination) {


    SENode* source_node = memory_access_to_indice_[source][0];
    SENode* destination_node = memory_access_to_indice_[destination][0];
/*    return ZIVTest(*source_node,
                   *destination_node);*/

    return SIVTest(source_node, destination_node);
  }

  void DumpIterationSpaceAsDot(std::ostream& out_stream) {
    out_stream << "digraph {\n";

    for (uint32_t id : loop_.GetBlocks()) {
      ir::BasicBlock* block = context_->cfg()->block(id);
      for (ir::Instruction& inst : *block) {
        if (inst.opcode() == SpvOp::SpvOpStore ||
            inst.opcode() == SpvOp::SpvOpLoad) {
          memory_access_to_indice_[&inst] = {};

          const ir::Instruction* access_chain =
              context_->get_def_use_mgr()->GetDef(
                  inst.GetSingleWordInOperand(0));

          for (uint32_t i = 1u; i < access_chain->NumInOperands(); ++i) {
            const ir::Instruction* index = context_->get_def_use_mgr()->GetDef(
                access_chain->GetSingleWordInOperand(i));
            memory_access_to_indice_[&inst].push_back(
                scalar_evolution_.AnalyzeInstruction(index));
          }
        }
      }
    }

    scalar_evolution_.DumpAsDot(out_stream);
    out_stream << "}\n";
  }

 private:
  ir::IRContext* context_;

  // The loop we are analysing the dependence of.
  const ir::Loop& loop_;

  ScalarEvolutionAnalysis scalar_evolution_;

  std::map<const ir::Instruction*, std::vector<SENode*>>
      memory_access_to_indice_;

  bool ZIVTest(const SENode& source, const SENode& destination) {
    // If source can be proven to equal destination then we have proved
    // dependence.
    if (scalar_evolution_.CanProveEqual(source, destination)) {
      return true;
    }

    // If we can prove not equal then we have prove independence.
    if (scalar_evolution_.CanProveNotEqual(source, destination)) {
      return false;
    }

    // Otherwise, we must assume they are dependent.
    return true;
  }

  bool SIVTest(SENode* source, SENode* destination) {
    return StrongSIVTest(source, destination);
  }

  bool StrongSIVTest(SENode* source, SENode* destination) {
    SENode* new_negation = scalar_evolution_.CreateNegation(destination);
    //SENode* new_add = 
    SENode* distance = scalar_evolution_.CreateAddNode(source, new_negation);

    int64_t value = 0;
    distance->FoldToSingleValue(&value);

    std::cout << value << std::endl;

    return true;
  }


  /*  bool WeakSIVTest(const Evolution& source, const Evolution& destination,
    Dependence* out) const;
    bool StrongSIVTest(const Evolution& source, const Evolution& destination,
    Dependence* out) const;
    bool MIVTest(const Evolution& source, const Evolution& destination,
    Dependence* out) const;

    // Maybe not needed.
    bool DeltaTest(const Evolution& source, const Evolution& destination,
    Dependence* out) const;*/
};

}  // namespace ir
}  // namespace spvtools

#endif  // LIBSPIRV_OPT_LOOP_DEPENDENCE_H__
