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
  enum SENodeType { Constant, Phi, Add, AccessChain };

  explicit SENode(const ir::Instruction* inst)
      : instruction_(inst), parent_(nullptr) {}
  SENode(const ir::Instruction* inst, SENode* parent)
      : instruction_(inst), parent_(parent) {}

  virtual SENodeType GetType() const = 0;

  virtual ~SENode() {}

  inline void AddChild(SENode* child) { children_.push_back(child); }

  inline void SetParent(SENode* parent) { parent_ = parent; }

  inline void MarkAsNonConstant() {
    is_constant = false;
    if (parent_) parent_->MarkAsNonConstant();
  }


  inline uint32_t UniqueID() const { return instruction_->unique_id(); }
  inline uint32_t ResultID() const { return instruction_->result_id(); }


  std::string AsString() const {
    switch (GetType()) {
      case Constant:
        return "Constant";
      case Phi:
        return "Phi";
      case Add:
        return "Add";
      case AccessChain:
        return "AccessChain";
    }
    return "NULL";
  }

  void DumpDot(std::ostream& out) {
    out << UniqueID() << " [label=\"" << AsString() << " " << ResultID()
        << "\"]\n";
    for (const SENode* child : children_) {
      out << child->UniqueID() << " [label=\"" << child->AsString() << " "
          << child->ResultID() << "\"]\n";
      out << UniqueID() << " -> " << child->UniqueID() << " \n";
    }
  }


  virtual bool FoldToSingleValue(int64_t *) const {
    return false;
  }
 protected:
  const ir::Instruction* instruction_;
  SENode* parent_;
  std::vector<SENode*> children_;

  // Are all child nodes constant. Defualts to true and should be set to false
  // when a child node is added which is not constant.
  bool is_constant;
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
    // Add the value.
    SENode::AddChild(child);
    // Add the block that value came from.
    incoming_blocks_.push_back(index);
  }

 private:
  // Each child node of this node will be the value parameters to the phi. This
  // vector maintains a list of the blocks the values came from.
  std::vector<uint32_t> incoming_blocks_;
};

class SEAccessChainRoot : public SENode {
 public:
  SEAccessChainRoot(const ir::Instruction* inst, SENode* parent)
      : SENode(inst, parent) {}

  explicit SEAccessChainRoot(const ir::Instruction* inst) : SENode(inst) {}
  SENodeType GetType() const final { return AccessChain; }
};

class SEAddNode : public SENode {
 public:
  SEAddNode(const ir::Instruction* inst, SENode* parent)
      : SENode(inst, parent) {}

  explicit SEAddNode(const ir::Instruction* inst) : SENode(inst) {}
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





class ScalarEvolutionAnalysis {
 public:
  ScalarEvolutionAnalysis(ir::IRContext* context) : context_(context) {}
  /*const Evolution& GetEvolution(const ir::Instruction* instruction) {
    // Calculate.
    return &scalar_evolutions_[induction];
  }*/

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

  SENode* AnalyzeInstruction(const ir::Instruction* inst) {
    if (scalar_evolutions_.find(inst) != scalar_evolutions_.end())
      return scalar_evolutions_[inst];

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
      default:
        return nullptr;
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
    scalar_evolutions_[inst] = constant_node;
    return constant_node;
  }

  SENode* AnalyzeAddOp(const ir::Instruction* add) {
    opt::analysis::DefUseManager* def_use = context_->get_def_use_mgr();
    SENode* add_node{new SEAddNode(add)};
    scalar_evolutions_[add] = add_node;

    add_node->AddChild(
        AnalyzeInstruction(def_use->GetDef(add->GetSingleWordInOperand(0))));
    add_node->AddChild(
        AnalyzeInstruction(def_use->GetDef(add->GetSingleWordInOperand(1))));
    return add_node;
  }

  SENode* AnalyzePhiInstruction(const ir::Instruction* phi) {
    SEPhiNode* phi_node{new SEPhiNode(phi)};
    scalar_evolutions_[phi] = phi_node;

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
  std::map<const ir::Instruction*, SENode*> scalar_evolutions_;
};

class LoopDependenceAnalysis {
 public:

  LoopDependenceAnalysis(ir::IRContext* context, const ir::Loop& loop)
      : context_(context), loop_(loop), scalar_evolution_(context){};

  bool GetDependence(const ir::Instruction* source,
                     const ir::Instruction* destination) {
    return ZIVTest(*memory_access_to_indice_[source][0],
                   *memory_access_to_indice_[destination][0]);
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


  std::map<const ir::Instruction*, std::vector<SENode*>> memory_access_to_indice_;

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
