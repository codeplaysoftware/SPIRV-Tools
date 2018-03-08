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
  SENode* negation_node{new SENegative(context_->TakeNextUniqueId())};
  negation_node->AddChild(operand);
  return GetCachedOrAdd(negation_node);
}

SENode* ScalarEvolutionAnalysis::AnalyzeMultiplyOp(
    const ir::Instruction* multiply) {
  opt::analysis::DefUseManager* def_use = context_->get_def_use_mgr();
  SENode* multiply_node{new SEMultiplyNode(context_->TakeNextUniqueId())};

  if (scalar_evolutions_.find(multiply_node->UniqueID()) ==
      scalar_evolutions_.end())
    scalar_evolutions_[multiply_node->UniqueID()] = multiply_node;

  multiply_node->AddChild(
      AnalyzeInstruction(def_use->GetDef(multiply->GetSingleWordInOperand(0))));
  multiply_node->AddChild(
      AnalyzeInstruction(def_use->GetDef(multiply->GetSingleWordInOperand(1))));

  multiply_node = GetCachedOrAdd(multiply_node);
  return multiply_node;
}

SENode* ScalarEvolutionAnalysis::CreateMultiplyNode(SENode* operand_1,
                                                    SENode* operand_2) {
  SENode* add_node{new SEMultiplyNode(context_->TakeNextUniqueId())};

  if (scalar_evolutions_.find(add_node->UniqueID()) == scalar_evolutions_.end())
    scalar_evolutions_[add_node->UniqueID()] = add_node;

  add_node->AddChild(operand_1);
  add_node->AddChild(operand_2);

  add_node = GetCachedOrAdd(add_node);
  return add_node;
}

SENode* ScalarEvolutionAnalysis::CreateAddNode(SENode* operand_1,
                                               SENode* operand_2) {
  SENode* add_node{new SEAddNode(context_->TakeNextUniqueId())};
  scalar_evolutions_[add_node->UniqueID()] = add_node;

  add_node->AddChild(operand_1);
  add_node->AddChild(operand_2);

  add_node = GetCachedOrAdd(add_node);
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
      output = AnalyzeAddOp(inst, false);
      break;
    }
    case SpvOp::SpvOpISub: {
      output = AnalyzeAddOp(inst, true);
      break;
    }
    case SpvOp::SpvOpLoad: {
      output = AnalyzeLoadOp(inst);
      break;
    }
    case SpvOp::SpvOpIMul: {
      output = AnalyzeMultiplyOp(inst);
      break;
    }
    default: {
      output = new SECantCompute(inst);
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

  constant_node = GetCachedOrAdd(constant_node);

  return constant_node;
}

SENode* ScalarEvolutionAnalysis::AnalyzeAddOp(const ir::Instruction* add,
                                              bool sub) {
  opt::analysis::DefUseManager* def_use = context_->get_def_use_mgr();
  SENode* add_node{new SEAddNode(add)};

  add_node->AddChild(
      AnalyzeInstruction(def_use->GetDef(add->GetSingleWordInOperand(0))));

  SENode* op2 =
      AnalyzeInstruction(def_use->GetDef(add->GetSingleWordInOperand(1)));
  if (sub) {
    op2 = CreateNegation(op2);
  }
  add_node->AddChild(op2);

  add_node = GetCachedOrAdd(add_node);

  return add_node;
}

SENode* ScalarEvolutionAnalysis::AnalyzePhiInstruction(
    const ir::Instruction* phi) {
  SERecurrentNode* phi_node{new SERecurrentNode(phi)};

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
      phi_node->AddInitalizer(value_node);
    } else if (incoming_label_id == loop->GetLatchBlock()->id()) {
      SENode* just_child = value_node->GetChild(1);
      phi_node->AddTripCount(just_child);
    }
  }

  return GetCachedOrAdd(phi_node);
}

void ScalarEvolutionAnalysis::FlattenAddExpressions(
    SENode* child_node, std::vector<SENode*>* nodes_to_add) const {
  for (SENode* child : *child_node) {
    if (child->GetType() == SENode::Add) {
      FlattenAddExpressions(child, nodes_to_add);
    } else {
      // It is easier to just remove and re add the non addition node children
      // with the rest of the nodes. As the vector is sorted when we add a
      // child it is problematic to keep track of the indexes.
      nodes_to_add->push_back(child);
    }
  }
}

static bool AccumulatorsFromMultiply(SENode* multiply,
                                     std::map<SENode*, int64_t>* accumulators,
                                     bool negation) {
  if (multiply->GetChildren().size() != 2 ||
      multiply->GetType() != SENode::Multiply)
    return false;

  SENode* operand_1 = multiply->GetChild(0);
  SENode* operand_2 = multiply->GetChild(1);

  SENode* value_unknown = nullptr;
  SENode* constant = nullptr;

  // Work out which operand is the unknown value.
  if (operand_1->GetType() == SENode::ValueUnknown)
    value_unknown = operand_1;
  else if (operand_2->GetType() == SENode::ValueUnknown)
    value_unknown = operand_2;

  // Work out which operand is the constant coefficient.
  if (operand_1->GetType() == SENode::Constant)
    constant = operand_1;
  else if (operand_2->GetType() == SENode::Constant)
    constant = operand_2;

  // If the expression is not a variable multiplied by a constant coefficient,
  // exit out.
  if (!(value_unknown && constant)) {
    return false;
  }

  int64_t sign = negation ? -1 : 1;
  if (accumulators->find(value_unknown) != accumulators->end()) {
    (*accumulators)[value_unknown] += constant->FoldToSingleValue() * sign;
  } else {
    (*accumulators)[value_unknown] = constant->FoldToSingleValue() * sign;
  }

  return true;
}

static void HandleChild(SENode* global_add, SENode* new_child,
                        int64_t* constant_accumulator,
                        std::map<SENode*, int64_t>* accumulators,
                        bool negation) {
  // Collect all the constants and add them together.
  if (new_child->GetType() == SENode::Constant) {
    if (!negation) {
      *constant_accumulator += new_child->FoldToSingleValue();
    } else {
      *constant_accumulator -= new_child->FoldToSingleValue();
    }
  } else if (new_child->GetType() == SENode::ValueUnknown) {
    // If we've incountered this term before add to the accumilator for it.
    if (accumulators->find(new_child) == accumulators->end())
      (*accumulators)[new_child] = negation ? -1 : 1;
    else
      (*accumulators)[new_child] += negation ? -1 : 1;

  } else if (new_child->GetType() == SENode::Multiply) {
    if (!AccumulatorsFromMultiply(new_child, accumulators, negation)) {
      global_add->AddChild(new_child);
    }
  } else if (new_child->GetType() == SENode::Negative) {
    SENode* negated_node = new_child->GetChild(0);
    HandleChild(global_add, negated_node, constant_accumulator, accumulators,
                !negation);
  } else {
    // If we can't work out how to fold the expression just add it back into
    // the graph.
    global_add->AddChild(new_child);
  }
}

SENode* ScalarEvolutionAnalysis::SimplifyExpression(SENode* node) {
  if (node->GetType() == SENode::Add) {
    std::vector<SENode*> nodes_to_add;

    FlattenAddExpressions(node, &nodes_to_add);
    node->GetChildren().clear();
    // Accumulator for constant terms in the expression.
    int64_t constant_accumulator = 0;

    // Accumulators for each of the non-constant terms in the expression.
    std::map<SENode*, int64_t> accumulators;

    for (SENode* new_child : nodes_to_add) {
      HandleChild(node, new_child, &constant_accumulator, &accumulators, false);
    }

    // Fold all the constants into a single constant node.
    if (constant_accumulator != 0) {
      node->AddChild(GetCachedOrAdd(new SEConstantNode(
          context_->TakeNextUniqueId(), constant_accumulator)));
    }

    for (auto& pair : accumulators) {
      SENode* term = pair.first;
      int64_t count = pair.second;

      // We can eliminate the term completely.
      if (count == 0) continue;

      if (count == 1) {
        node->AddChild(term);
      } else {
        SENode* count_as_constant = GetCachedOrAdd(
            new SEConstantNode(context_->TakeNextUniqueId(), count));

        node->AddChild(CreateMultiplyNode(count_as_constant, term));
      }
    }
  }

  return node;
}

SENode* ScalarEvolutionAnalysis::GetRecurrentExpression(SENode* node) {
  //  SERecurrentNode* recurrent_node{new SERecurrentNode(node)};

  if (node->GetType() != SENode::Add) return node;

  SERecurrentNode* recurrent_expr = nullptr;
  for (auto child = node->graph_begin(); child != node->graph_end(); ++child) {
    if (child->GetType() == SENode::RecurrentExpr) {
      recurrent_expr = static_cast<SERecurrentNode*>(&*child);
    }
  }

  if (!recurrent_expr) return nullptr;

  SERecurrentNode* recurrent_node{
      new SERecurrentNode(context_->TakeNextUniqueId())};

  recurrent_node->AddInitalizer(node->GetChild(1));
  recurrent_node->AddTripCount(recurrent_expr->GetTripCount());

  recurrent_node =
      static_cast<SERecurrentNode*>(GetCachedOrAdd(recurrent_node));
  return recurrent_node;
}

SENode* ScalarEvolutionAnalysis::AnalyzeLoadOp(const ir::Instruction* load) {
  SEValueUnknown* load_node{new SEValueUnknown(load)};
  return GetCachedOrAdd(load_node);
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

SENode* ScalarEvolutionAnalysis::GetCachedOrAdd(SENode* perspective_node) {
  auto itr = node_cache_.find(perspective_node);
  if (itr != node_cache_.end()) {
    delete perspective_node;
    return *itr;
  }

  node_cache_.insert(perspective_node);
  scalar_evolutions_[perspective_node->UniqueID()] = perspective_node;
  return perspective_node;
}

bool SENode::operator==(const SENode& other) const {
  return SENodeHash{}(this) == SENodeHash{}(&other);
}

bool SENode::operator!=(const SENode& other) const { return !(*this == other); }

}  // namespace opt
}  // namespace spvtools
