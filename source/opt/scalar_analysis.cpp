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
#include <functional>

namespace spvtools {
namespace opt {

SENode* ScalarEvolutionAnalysis::CreateNegation(SENode* operand) {
  std::unique_ptr<SENode> negation_node{new SENegative()};
  negation_node->AddChild(operand);
  return GetCachedOrAdd(std::move(negation_node));
}

SENode* ScalarEvolutionAnalysis::CreateConstant(int64_t integer) {
  return GetCachedOrAdd(std::unique_ptr<SENode>(new SEConstantNode(integer)));
}

SENode* ScalarEvolutionAnalysis::AnalyzeMultiplyOp(
    const ir::Instruction* multiply) {
  opt::analysis::DefUseManager* def_use = context_->get_def_use_mgr();

  SENode* op1 =
      AnalyzeInstruction(def_use->GetDef(multiply->GetSingleWordInOperand(0)));
  SENode* op2 =
      AnalyzeInstruction(def_use->GetDef(multiply->GetSingleWordInOperand(1)));

  return CreateMultiplyNode(op1, op2);
}

SENode* ScalarEvolutionAnalysis::CreateMultiplyNode(SENode* operand_1,
                                                    SENode* operand_2) {
  if (operand_1->GetType() == SENode::Constant &&
      operand_2->GetType() == SENode::Constant) {
    return CreateConstant(operand_1->FoldToSingleValue() *
                          operand_2->FoldToSingleValue());
  }

  std::unique_ptr<SENode> add_node{new SEMultiplyNode()};

  add_node->AddChild(operand_1);
  add_node->AddChild(operand_2);

  return GetCachedOrAdd(std::move(add_node));
}

SENode* ScalarEvolutionAnalysis::CreateAddNode(SENode* operand_1,
                                               SENode* operand_2) {
  std::unique_ptr<SENode> add_node{new SEAddNode()};

  add_node->AddChild(operand_1);
  add_node->AddChild(operand_2);

  return GetCachedOrAdd(std::move(add_node));
}

SENode* ScalarEvolutionAnalysis::AnalyzeInstruction(
    const ir::Instruction* inst) {
  if (instruction_map_.find(inst) != instruction_map_.end())
    return instruction_map_[inst];

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
      output = AnalyzeAddOp(inst, false);
      break;
    }
    case SpvOp::SpvOpISub: {
      output = AnalyzeAddOp(inst, true);
      break;
    }
    case SpvOp::SpvOpLoad: {
      output = CreateValueUnknownNode();
      break;
    }
    case SpvOp::SpvOpIMul: {
      output = AnalyzeMultiplyOp(inst);
      break;
    }
    default: {
      output = new SECantCompute();
      instruction_map_[inst] = output;
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

  return CreateConstant(value);
}

SENode* ScalarEvolutionAnalysis::AnalyzeAddOp(const ir::Instruction* add,
                                              bool sub) {
  opt::analysis::DefUseManager* def_use = context_->get_def_use_mgr();
  std::unique_ptr<SENode> add_node{new SEAddNode()};

  add_node->AddChild(
      AnalyzeInstruction(def_use->GetDef(add->GetSingleWordInOperand(0))));

  SENode* op2 =
      AnalyzeInstruction(def_use->GetDef(add->GetSingleWordInOperand(1)));
  if (sub) {
    op2 = CreateNegation(op2);
  }
  add_node->AddChild(op2);

  return GetCachedOrAdd(std::move(add_node));
}

SENode* ScalarEvolutionAnalysis::AnalyzePhiInstruction(
    const ir::Instruction* phi) {
  opt::analysis::DefUseManager* def_use = context_->get_def_use_mgr();

  ir::BasicBlock* basic_block =
      context_->get_instr_block(const_cast<ir::Instruction*>(phi));
  ir::Function* function = basic_block->GetParent();

  ir::Loop* loop = (*context_->GetLoopDescriptor(function))[basic_block->id()];
  if (!loop) return CreateCantComputeNode();

  std::unique_ptr<SERecurrentNode> phi_node{new SERecurrentNode(loop)};
  instruction_map_[phi] = phi_node.get();

  for (uint32_t i = 0; i < phi->NumInOperands(); i += 2) {
    uint32_t value_id = phi->GetSingleWordInOperand(i);
    uint32_t incoming_label_id = phi->GetSingleWordInOperand(i + 1);

    ir::Instruction* value_inst = def_use->GetDef(value_id);
    SENode* value_node = AnalyzeInstruction(value_inst);

    if (incoming_label_id == loop->GetPreHeaderBlock()->id()) {
      phi_node->AddOffset(value_node);
    } else if (incoming_label_id == loop->GetLatchBlock()->id()) {
      SENode* just_child = value_node->GetChild(1);
      phi_node->AddCoefficient(just_child);
    }
  }

  return GetCachedOrAdd(std::move(phi_node));
}

SENode* ScalarEvolutionAnalysis::CreateValueUnknownNode() {
  std::unique_ptr<SEValueUnknown> load_node{new SEValueUnknown()};
  return GetCachedOrAdd(std::move(load_node));
}

SENode* ScalarEvolutionAnalysis::CreateCantComputeNode() {
  return GetCachedOrAdd(std::unique_ptr<SECantCompute>(new SECantCompute));
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

int64_t SEAddNode::FoldToSingleValue() const {
  int64_t val = 0;
  for (SENode* child : children_) {
    val += child->FoldToSingleValue();
  }
  return val;
}

// Add the created node into the cache of nodes. If it already exists return it.
SENode* ScalarEvolutionAnalysis::GetCachedOrAdd(
    std::unique_ptr<SENode> perspective_node) {
  auto itr = node_cache_.find(perspective_node);
  if (itr != node_cache_.end()) {
    return (*itr).get();
  }

  SENode* raw_ptr_to_node = perspective_node.get();
  node_cache_.insert(std::move(perspective_node));
  return raw_ptr_to_node;
}

std::string SENode::AsString() const {
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

bool SENode::operator==(const SENode& other) const {
  return SENodeHash{}(this) == SENodeHash{}(&other);
}

bool SENode::operator!=(const SENode& other) const { return !(*this == other); }

// Implements the hashing of SENodes.
size_t SENodeHash::operator()(const SENode* node) const {
  // Hashing the type as a string is safer than hashing the enum as the enum is
  // very likely to collide with constants.
  std::string type = node->AsString();

  // We just ignore the literal value unless it is a constant.
  int64_t literal_value = 0;
  if (node->GetType() == SENode::Constant)
    literal_value = node->FoldToSingleValue();

  // Hash the type string and the constant value if any.
  size_t resulting_hash =
      std::hash<std::string>{}(type) ^ std::hash<int64_t>{}(literal_value);

  // Hash the pointers of the child nodes, each SENode has a unique pointer
  // associated with it.
  const std::vector<SENode*>& children = node->GetChildren();
  for (const SENode* child : children) {
    resulting_hash ^= std::hash<const SENode*>{}(child);
  }

  return resulting_hash;
}

// This overload is the actual overload used by the node_cache_ set.
size_t SENodeHash::operator()(const std::unique_ptr<SENode>& node) const {
  return this->operator()(node.get());
}

void SENode::DumpDot(std::ostream& out, bool recurse) const {
  size_t unique_id = std::hash<const SENode*>{}(this);
  out << unique_id << " [label=\"" << AsString() << " ";
  if (GetType() == SENode::Constant) {
    out << "\nwith value: " << FoldToSingleValue();
  }
  out << "\"]\n";
  for (const SENode* child : children_) {
    size_t child_unique_id = std::hash<const SENode*>{}(child);
    out << unique_id << " -> " << child_unique_id << " \n";
    if (recurse) child->DumpDot(out, true);
  }
}

}  // namespace opt
}  // namespace spvtools
