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

// Transforms a given scalar operation instruction into a DAG representation.
//
// 1. Take an instruction and traverse the its operands until we reach a
// constant node or any instruction which we do not know how to compute the
// value of, such as a load.
//
// 2. Create a new node for each instruction traversed and build the nodes for
// the in operands of that instruction as well.
//
// 3. Add the operand nodes as children of the first and hash the node. Use the
// hash to see if the node is already in the cache. We ensure the children are
// always in sorted order so that two nodes with the same children but inserted
// in a different order have the same hash and so that the overloaded operator==
// will return true. If the node is already in the cache return the cached
// version instead.
//
// 4. The created DAG can then be simplified by
// ScalarAnalysis::SimplifyExpression, implemented in
// scalar_analysis_simplification.cpp. See that file for further information on
// the simplification process.
//

namespace spvtools {
namespace opt {

uint32_t SENode::NumberOfNodes = 0;

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
  assert(multiply->opcode() == SpvOp::SpvOpIMul &&
         "Multiply node did not come from a multiply instruction");
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

  std::unique_ptr<SENode> multiply_node{new SEMultiplyNode(this)};

  multiply_node->AddChild(operand_1);
  multiply_node->AddChild(operand_2);

  return GetCachedOrAdd(std::move(multiply_node));
}

SENode* ScalarEvolutionAnalysis::CreateSubtraction(SENode* operand_1,
                                                   SENode* operand_2) {
  // Fold if both operands are constant.
  if (operand_1->GetType() == SENode::Constant &&
      operand_2->GetType() == SENode::Constant) {
    return CreateConstant(operand_1->AsSEConstantNode()->FoldToSingleValue() -
                          operand_2->AsSEConstantNode()->FoldToSingleValue());
  }

  return CreateAddNode(operand_1, CreateNegation(operand_2));
}

SENode* ScalarEvolutionAnalysis::CreateAddNode(SENode* operand_1,
                                               SENode* operand_2,
                                               bool simplify) {
  // Fold if both operands are constant and the |simplify| flag is true.
  if (operand_1->GetType() == SENode::Constant &&
      operand_2->GetType() == SENode::Constant && simplify) {
    return CreateConstant(operand_1->AsSEConstantNode()->FoldToSingleValue() +
                          operand_2->AsSEConstantNode()->FoldToSingleValue());
  }

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
    case SpvOp::SpvOpConstant:
    case SpvOp::SpvOpConstantNull: {
      output = AnalyzeConstant(inst);
      break;
    }
    case SpvOp::SpvOpISub:
    case SpvOp::SpvOpIAdd: {
      output = AnalyzeAddOp(inst);
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
  assert(inst->NumInOperands() == 1);

  if (inst->opcode() == SpvOp::SpvOpConstantNull) return CreateConstant(0);

  int64_t value = 0;

  // Look up the instruction in the constant manager.
  const opt::analysis::Constant* constant =
      context_->get_constant_mgr()->FindDeclaredConstant(inst->result_id());

  if (!constant) return CreateCantComputeNode();

  const opt::analysis::IntConstant* int_constant = constant->AsIntConstant();

  // Exit out if it is a 64 bit integer.
  if (!int_constant || int_constant->words().size() != 1)
    return CreateCantComputeNode();

  if (int_constant->type()->AsInteger()->IsSigned()) {
    value = int_constant->GetS32BitValue();
  } else {
    value = int_constant->GetU32BitValue();
  }

  return CreateConstant(value);
}

// Handles both addition and subtraction. If the |sub| flag is set then the
// addition will be op1+(-op2) otherwise op1+op2.
SENode* ScalarEvolutionAnalysis::AnalyzeAddOp(const ir::Instruction* inst) {
  assert((inst->opcode() == SpvOp::SpvOpIAdd or
          inst->opcode() == SpvOp::SpvOpISub) &&
         "Add node must be created from a OpIAdd or OpISub instruction");

  opt::analysis::DefUseManager* def_use = context_->get_def_use_mgr();
  std::unique_ptr<SENode> add_node{new SEAddNode(this)};

  SENode* op1 =
      AnalyzeInstruction(def_use->GetDef(inst->GetSingleWordInOperand(0)));

  SENode* op2 =
      AnalyzeInstruction(def_use->GetDef(inst->GetSingleWordInOperand(1)));

  // To handle subtraction we wrap the second operand in a unary negation node.
  if (inst->opcode() == SpvOp::SpvOpISub) {
    op2 = CreateNegation(op2);
  }

  return CreateAddNode(op1, op2);
}

SENode* ScalarEvolutionAnalysis::AnalyzePhiInstruction(
    const ir::Instruction* phi) {
  // The phi should only have two incoming value pairs.
  if (phi->NumInOperands() != 4) {
    return CreateCantComputeNode();
  }

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

  // If the loop doesn't exist or doesn't have a preheader or latch block, exit
  // out.
  if (!loop || !loop->GetLatchBlock() || !loop->GetPreHeaderBlock())
    return CreateCantComputeNode();

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
      if (value_node->GetType() != SENode::Add) return CreateCantComputeNode();

      SENode* step_node = nullptr;
      SENode* phi_operand = nullptr;
      SENode* operand_1 = value_node->GetChild(0);
      SENode* operand_2 = value_node->GetChild(1);

      // Find which node is the step term.
      if (!operand_1->AsSERecurrentNode())
        step_node = operand_1;
      else if (!operand_2->AsSERecurrentNode())
        step_node = operand_2;

      // Find which node is the recurrent expression.
      if (operand_1->AsSERecurrentNode())
        phi_operand = operand_1;
      else if (operand_2->AsSERecurrentNode())
        phi_operand = operand_2;

      // If it is not in the form step + phi exit out.
      if (!(step_node && phi_operand)) return CreateCantComputeNode();

      // If the phi operand is not the same phi node exit out.
      if (phi_operand != phi_node.get()) return CreateCantComputeNode();

      if (!IsLoopInvariant(loop, step_node)) {
        return CreateCantComputeNode();
      }

      phi_node->AddCoefficient(step_node);
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
  for (auto itr = node->graph_cbegin(); itr != node->graph_cend(); ++itr) {
    if (const SERecurrentNode* rec = itr->AsSERecurrentNode()) {
      const ir::BasicBlock* header = rec->GetLoop()->GetHeaderBlock();

      // If the loop which the recurrent expression belongs to is either |loop|
      // or a nested loop inside |loop| then we assume it is variant.
      if (loop->IsInsideLoop(header)) {
        return false;
      }
    } else if (const SEValueUnknown* unknown = itr->AsSEValueUnknown()) {
      // If the instruction is inside the loop we conservatively assume it is
      // loop variant.
      if (loop->IsInsideLoop(unknown->ResultId())) return false;
    }
  }

  return true;
}

std::string SENode::AsString() const {
  switch (GetType()) {
    case Constant:
      return "Constant";
    case RecurrentAddExpr:
      return "RecurrentAddExpr";
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
    if (AsSEValueUnknown()->ResultId() !=
        other.AsSEValueUnknown()->ResultId()) {
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

namespace {
// Helper functions to insert 32/64 bit values into the 32 bit hash string. This
// allows us to add pointers to the string by reinterpreting thie pointers as
// uintptr_t which will result in them going to either the uint32_t or uint64_t
// overload depending on the platform.

static void PushToString(uint32_t id, std::u32string* str) {
  str->push_back(id);
}

static void PushToString(uint64_t id, std::u32string* str) {
  str->push_back(static_cast<uint32_t>(id << 32));
  str->push_back(static_cast<uint32_t>(id));
}

static void PushToString(int64_t id, std::u32string* str) {
  str->push_back(static_cast<uint32_t>(id << 32));
  str->push_back(static_cast<uint32_t>(id));
}

}  // namespace

// Implements the hashing of SENodes.
size_t SENodeHash::operator()(const SENode* node) const {
  // Concatinate the terms into a string which we can hash.
  std::u32string hash_string{};

  // Hashing the type as a string is safer than hashing the enum as the enum is
  // very likely to collide with constants.
  for (char ch : node->AsString()) {
    hash_string.push_back(static_cast<char32_t>(ch));
  }

  // We just ignore the literal value unless it is a constant.
  if (node->GetType() == SENode::Constant)
    PushToString(node->AsSEConstantNode()->FoldToSingleValue(), &hash_string);

  // If we're dealing with a recurrent expression hash the loop as well so that
  // nested inductions like i=0,i++ and j=0,j++ correspond to different nodes.
  if (node->GetType() == SENode::RecurrentAddExpr) {
    PushToString(
        reinterpret_cast<uintptr_t>(node->AsSERecurrentNode()->GetLoop()),
        &hash_string);
  }

  // Hash the result id of the original instruction which created this node if
  // it is a value unknown
  // node.
  if (node->GetType() == SENode::ValueUnknown) {
    PushToString(node->AsSEValueUnknown()->ResultId(), &hash_string);
  }

  // Hash the pointers of the child nodes, each SENode has a unique pointer
  // associated with it.
  const std::vector<SENode*>& children = node->GetChildren();
  for (const SENode* child : children) {
    PushToString(reinterpret_cast<uintptr_t>(child), &hash_string);
  }

  return std::hash<std::u32string>{}(hash_string);
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
