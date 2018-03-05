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

#include "opt/scalar_analysis.h"
namespace spvtools {
namespace opt {

SENode* ScalarEvolutionAnalysis::CreateNegation(SENode* operand) {
  SENode* negation_node{new SENegative(context_->TakeNextUniqueId())};

  scalar_evolutions_[negation_node->UniqueID()] = negation_node;
  negation_node->AddChild(operand);
  return negation_node;
}

SENode* ScalarEvolutionAnalysis::CreateAddNode(SENode* operand_1,
                                               SENode* operand_2) {
  SENode* add_node{new SEAddNode(context_->TakeNextUniqueId())};
  scalar_evolutions_[add_node->UniqueID()] = add_node;

  add_node->AddChild(operand_1);
  add_node->AddChild(operand_2);

  return add_node;
}

SENode* ScalarEvolutionAnalysis::AnalyzeInstruction(
    const ir::Instruction* inst) {
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
    case SpvOp::SpvOpLoad: {
      output = AnalyzeLoadOp(inst);
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

SENode* ScalarEvolutionAnalysis::AnalyzeConstant(const ir::Instruction* inst) {
  if (inst->NumInOperands() != 1) {
    assert(false);
  }

  int64_t value = 0;

  const opt::analysis::Constant* constant =
      context_->get_constant_mgr()->FindDeclaredConstant(inst->result_id());

  if (constant->AsIntConstant()->type()->AsInteger()->IsSigned()) {
    value = constant->AsIntConstant()->GetS32BitValue();
  } else {
    value = constant->AsIntConstant()->GetU32BitValue();
  }

  SENode* constant_node{new SEConstantNode(inst, value)};
  scalar_evolutions_[inst->unique_id()] = constant_node;
  return constant_node;
}

SENode* ScalarEvolutionAnalysis::AnalyzeAddOp(const ir::Instruction* add) {
  opt::analysis::DefUseManager* def_use = context_->get_def_use_mgr();
  SENode* add_node{new SEAddNode(add)};
  scalar_evolutions_[add->unique_id()] = add_node;

  add_node->AddChild(
      AnalyzeInstruction(def_use->GetDef(add->GetSingleWordInOperand(0))));
  add_node->AddChild(
      AnalyzeInstruction(def_use->GetDef(add->GetSingleWordInOperand(1))));
  return add_node;
}

SENode* ScalarEvolutionAnalysis::AnalyzePhiInstruction(
    const ir::Instruction* phi) {
  SEPhiNode* phi_node{new SEPhiNode(phi)};

  scalar_evolutions_[phi->unique_id()] = phi_node;

  opt::analysis::DefUseManager* def_use = context_->get_def_use_mgr();

  ir::BasicBlock* basic_block =
      context_->get_instr_block(const_cast<ir::Instruction*>(phi));

  ir::Function* function = basic_block->GetParent();

  ir::Loop* loop = (*context_->GetLoopDescriptor(function))[basic_block->id()];

  for (uint32_t i = 0; i < phi->NumInOperands(); i += 2) {
    uint32_t value_id = phi->GetSingleWordInOperand(i);
    uint32_t incoming_label_id = phi->GetSingleWordInOperand(i + 1);

    ir::Instruction* value_inst = def_use->GetDef(value_id);
    SENode* value_node = AnalyzeInstruction(value_inst);

    if (incoming_label_id == loop->GetPreHeaderBlock()->id()) {
      phi_node->AddTripCount(value_node);
    } else if (incoming_label_id == loop->GetLatchBlock()->id()) {
      phi_node->AddInitalizer(value_node);
    }
  }

  return phi_node;
}

SENode* ScalarEvolutionAnalysis::AnalyzeLoadOp(const ir::Instruction* load) {
 SELoad* load_node{new SELoad(load)};
 scalar_evolutions_[load->unique_id()] = load_node;
 return load_node;
}

bool ScalarEvolutionAnalysis::CanProveEqual(const SENode& source,
                                            const SENode& destination) {
  return source == destination;
}

bool ScalarEvolutionAnalysis::CanProveNotEqual(const SENode& source,
                                               const SENode& destination) {
  return source != destination;  
}

int64_t SEMultiplyNode::FoldToSingleValue() const {
  int64_t val = 0;
  for (SENode* child : children_) {
    val *= child->FoldToSingleValue();
  }
  return val;
}


int64_t SEDivideNode::FoldToSingleValue() const {
  int64_t val = 0;
  for (SENode* child : children_) {
    val /= child->FoldToSingleValue();
  }
  return val;
}

int64_t SEAddNode::FoldToSingleValue() const {
  int64_t val = 0;
  for (SENode* child : children_) {
    val += child->FoldToSingleValue();
  }
  return val;
}

}  // namespace opt
}  // namespace spvtools
