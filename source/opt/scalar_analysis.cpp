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
#include <algorithm>
#include <functional>
#include <string>
#include <utility>

#include "opt/ir_context.h"

namespace spvtools {
namespace opt {

SENode* ScalarEvolutionAnalysis::CreateNegation(SENode* operand) {
  if (operand->GetType() == SENode::Constant) {
    return CreateConstant(-operand->AsSEConstantNode()->FoldToSingleValue());
  }
  std::unique_ptr<SENode> negation_node{new SENegative(this)};
  negation_node->AddChild(operand);
  return GetCachedOrAdd(std::move(negation_node));
}

SENode* ScalarEvolutionAnalysis::CreateConstant(int64_t integer) {
  return GetCachedOrAdd(
      std::unique_ptr<SENode>(new SEConstantNode(this, integer)));
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
    return CreateConstant(operand_1->AsSEConstantNode()->FoldToSingleValue() *
                          operand_2->AsSEConstantNode()->FoldToSingleValue());
  }

  std::unique_ptr<SENode> add_node{new SEMultiplyNode(this)};

  add_node->AddChild(operand_1);
  add_node->AddChild(operand_2);

  return GetCachedOrAdd(std::move(add_node));
}

SENode* ScalarEvolutionAnalysis::CreateSubtraction(SENode* operand_1,
                                                   SENode* operand_2) {
  return CreateAddNode(operand_1, CreateNegation(operand_2));
}

SENode* ScalarEvolutionAnalysis::CreateAddNode(SENode* operand_1,
                                               SENode* operand_2) {
  std::unique_ptr<SENode> add_node{new SEAddNode(this)};

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
    case SpvOp::SpvOpIMul: {
      output = AnalyzeMultiplyOp(inst);
      break;
    }
    default: {
      output = CreateValueUnknownNode(inst);
      instruction_map_[inst] = output;
      break;
    }
  }
  return output;
}

SENode* ScalarEvolutionAnalysis::AnalyzeConstant(const ir::Instruction* inst) {
  if (inst->NumInOperands() != 1) {
    assert(false);
  }

  int64_t value = 0;

  // Look up the instruction in the constant manager.
  const opt::analysis::Constant* constant =
      context_->get_constant_mgr()->FindDeclaredConstant(inst->result_id());

  if (constant->AsIntConstant()->type()->AsInteger()->IsSigned()) {
    value = constant->AsIntConstant()->GetS32BitValue();
  } else {
    value = constant->AsIntConstant()->GetU32BitValue();
  }

  return CreateConstant(value);
}

// Handles both addition and subtraction. If the |sub| flag is set then the
// addition will be op1+(-op2) otherwise op1+op2.
SENode* ScalarEvolutionAnalysis::AnalyzeAddOp(const ir::Instruction* add,
                                              bool sub) {
  opt::analysis::DefUseManager* def_use = context_->get_def_use_mgr();
  std::unique_ptr<SENode> add_node{new SEAddNode(this)};

  add_node->AddChild(
      AnalyzeInstruction(def_use->GetDef(add->GetSingleWordInOperand(0))));

  SENode* op2 =
      AnalyzeInstruction(def_use->GetDef(add->GetSingleWordInOperand(1)));

  // To handle subtraction we wrap the second operand in a unary negation node.
  if (sub) {
    op2 = CreateNegation(op2);
  }
  add_node->AddChild(op2);

  return GetCachedOrAdd(std::move(add_node));
}

SENode* ScalarEvolutionAnalysis::AnalyzePhiInstruction(
    const ir::Instruction* phi) {
  opt::analysis::DefUseManager* def_use = context_->get_def_use_mgr();

  // Get the basic block this instruction belongs to.
  ir::BasicBlock* basic_block =
      context_->get_instr_block(const_cast<ir::Instruction*>(phi));

  // And then the function that the basic blocks belongs to.
  ir::Function* function = basic_block->GetParent();

  // Use the function to get the loop descriptor.
  ir::LoopDescriptor* loop_descriptor = context_->GetLoopDescriptor(function);

  // We only handle phis in loops at the moment.
  if (!loop_descriptor) return CreateCantComputeNode();

  // Get the innermost loop which this block belongs to.
  ir::Loop* loop = (*loop_descriptor)[basic_block->id()];
  if (!loop) return CreateCantComputeNode();

  std::unique_ptr<SERecurrentNode> phi_node{new SERecurrentNode(this, loop)};
  instruction_map_[phi] = phi_node.get();

  for (uint32_t i = 0; i < phi->NumInOperands(); i += 2) {
    uint32_t value_id = phi->GetSingleWordInOperand(i);
    uint32_t incoming_label_id = phi->GetSingleWordInOperand(i + 1);

    ir::Instruction* value_inst = def_use->GetDef(value_id);
    SENode* value_node = AnalyzeInstruction(value_inst);

    if (incoming_label_id == loop->GetPreHeaderBlock()->id()) {
      phi_node->AddOffset(value_node);
    } else if (incoming_label_id == loop->GetLatchBlock()->id()) {
      // Assumed to be in the form of step + phi.
      SENode* constant_node = nullptr;
      SENode* phi_operand = nullptr;
      SENode* operand_1 = value_node->GetChild(0);
      SENode* operand_2 = value_node->GetChild(1);

      // Find which node is the step term.
      if (!operand_1->AsSERecurrentNode())
        constant_node = operand_1;
      else if (!operand_2->AsSERecurrentNode())
        constant_node = operand_2;

      // Find which node is the recurrent expression.
      if (operand_1->AsSERecurrentNode())
        phi_operand = operand_1;
      else if (operand_2->AsSERecurrentNode())
        phi_operand = operand_2;

      // If it is not in the form step + phi exit out.
      if (!(constant_node && phi_operand)) return CreateCantComputeNode();

      // If the phi operand is not the same phi node exit out.
      if (phi_operand != phi_node.get()) return CreateCantComputeNode();

      phi_node->AddCoefficient(constant_node);
    }
  }

  instruction_map_[phi] = GetCachedOrAdd(std::move(phi_node));

  return instruction_map_[phi];
}

SENode* ScalarEvolutionAnalysis::CreateValueUnknownNode(
    const ir::Instruction* inst) {
  std::unique_ptr<SEValueUnknown> load_node{
      new SEValueUnknown(this, inst->unique_id())};
  return GetCachedOrAdd(std::move(load_node));
}

SENode* ScalarEvolutionAnalysis::CreateCantComputeNode() {
  return GetCachedOrAdd(
      std::unique_ptr<SECantCompute>(new SECantCompute(this)));
}

// Add the created node into the cache of nodes. If it already exists return it.
SENode* ScalarEvolutionAnalysis::GetCachedOrAdd(
    std::unique_ptr<SENode> prospective_node) {
  auto itr = node_cache_.find(prospective_node);
  if (itr != node_cache_.end()) {
    return (*itr).get();
  }

  SENode* raw_ptr_to_node = prospective_node.get();
  node_cache_.insert(std::move(prospective_node));
  return raw_ptr_to_node;
}

bool ScalarEvolutionAnalysis::IsLoopInvariant(const ir::Loop* loop,
                                              const SENode* node) const {
  return std::none_of(
      node->graph_cbegin(), node->graph_cend(), [loop](const SENode& expr) {
        if (const SERecurrentNode* rec = expr.AsSERecurrentNode()) {
          return loop->IsInsideLoop(rec->GetLoop()->GetHeaderBlock());
        }
        if (const SEValueUnknown* unknown = expr.AsSEValueUnknown()) {
          return loop->IsInsideLoop(unknown->UniqueId());
        }
        return false;
      });
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
  if (GetType() != other.GetType()) return false;

  if (other.GetChildren().size() != children_.size()) return false;

  for (size_t index = 0; index < children_.size(); ++index) {
    if (other.GetChildren()[index] != children_[index]) return false;
  }

  // If we're dealing with a value unknown node check both nodes were created by
  // the same instruction.
  if (GetType() == SENode::ValueUnknown) {
    if (AsSEValueUnknown()->UniqueId() !=
        other.AsSEValueUnknown()->UniqueId()) {
      return false;
    }
  }

  if (AsSEConstantNode()) {
    if (AsSEConstantNode()->FoldToSingleValue() !=
        other.AsSEConstantNode()->FoldToSingleValue())
      return false;
  }

  return true;
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
    literal_value = node->AsSEConstantNode()->FoldToSingleValue();

  // Hash the type string and the constant value if any.
  size_t resulting_hash =
      std::hash<std::string>{}(type) ^ std::hash<int64_t>{}(literal_value);

  // If we're dealing with a recurrent expression hash the loop as well so that
  // nested inductions like i=0,i++ and j=0,j++ correspond to different nodes.
  if (node->GetType() == SENode::RecurrentExpr) {
    resulting_hash ^=
        std::hash<const ir::Loop*>{}(node->AsSERecurrentNode()->GetLoop());
  }

  // Hash the unique id of the creator instruction if this is a value unknown
  // node.
  if (node->GetType() == SENode::ValueUnknown) {
    resulting_hash ^=
        std::hash<uint32_t>{}(node->AsSEValueUnknown()->UniqueId());
  }
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
    out << "\nwith value: " << this->AsSEConstantNode()->FoldToSingleValue();
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
