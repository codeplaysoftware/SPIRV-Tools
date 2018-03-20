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

    // Both operands must be loop invariant.
    if (!IsLoopInvariant(loop, value_node)) {
      return CreateCantComputeNode();
    }

    if (!loop->IsInsideLoop(incoming_label_id)) {
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
  for (auto itr = node->graph_cbegin(); itr != node->graph_cend(); ++itr) {
    if (const SERecurrentNode* rec = itr->AsSERecurrentNode()) {
      const ir::BasicBlock* header = rec->GetLoop()->GetHeaderBlock();

      // If the header block is not the same as |loop| header block or is not
      // inside the loop then assume invariance.
      if (header != loop->GetHeaderBlock() && !loop->IsInsideLoop(header))
        return false;
    }
    if (const SEValueUnknown* unknown = itr->AsSEValueUnknown()) {
      // If the instruction is inside the loop we conservatively assume it is
      // loop variant.
      if (loop->IsInsideLoop(unknown->UniqueId())) return false;
    }
  }

  return true;
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

namespace {
class IsGreaterThanZero {
 public:
  explicit IsGreaterThanZero(ir::IRContext* context) : context_(context) {}

  bool Eval(const SENode* node, bool or_equal_zero, bool* result) {
    switch (Visit(node)) {
      case Signedness::kPositiveOrNegative: {
        return false;
      }
      case Signedness::kStrictlyNegative: {
        *result = false;
        break;
      }
      case Signedness::kNegative: {
        if (!or_equal_zero) {
          return false;
        }
        *result = false;
        break;
      }
      case Signedness::kStrictlyPositive: {
        *result = true;
        break;
      }
      case Signedness::kPositive: {
        if (!or_equal_zero) {
          return false;
        }
        *result = true;
        break;
      }
    }
    return true;
  }

 private:
  enum class Signedness {
    kPositiveOrNegative,  // Yield a value positive or negative.
    kStrictlyNegative,    // Yield a value strictly less than 0.
    kNegative,            // Yield a value less or equal to 0.
    kStrictlyPositive,    // Yield a value strictly greater than 0.
    kPositive             // Yield a value greater or equal to 0.
  };

  // Combine the signedness according to arithmetic rules of a given operator.
  using Combiner = std::function<Signedness(Signedness, Signedness)>;

  Combiner GetAddCombiner() const {
    return [](Signedness lhs, Signedness rhs) {
      switch (lhs) {
        case Signedness::kPositiveOrNegative:
          break;
        case Signedness::kStrictlyNegative:
          if (rhs == Signedness::kStrictlyNegative ||
              rhs == Signedness::kNegative)
            return lhs;
          break;
        case Signedness::kNegative: {
          if (rhs == Signedness::kStrictlyNegative)
            return Signedness::kStrictlyNegative;
          if (rhs == Signedness::kNegative) return Signedness::kNegative;
          break;
        }
        case Signedness::kStrictlyPositive: {
          if (rhs == Signedness::kStrictlyPositive ||
              rhs == Signedness::kPositive) {
            return Signedness::kStrictlyPositive;
          }
          break;
        }
        case Signedness::kPositive: {
          if (rhs == Signedness::kStrictlyPositive)
            return Signedness::kStrictlyPositive;
          if (rhs == Signedness::kPositive) return Signedness::kPositive;
          break;
        }
      }
      return Signedness::kPositiveOrNegative;
    };
  }

  Combiner GetMulCombiner() const {
    return [](Signedness lhs, Signedness rhs) {
      switch (lhs) {
        case Signedness::kPositiveOrNegative:
          break;
        case Signedness::kStrictlyNegative: {
          switch (rhs) {
            case Signedness::kPositiveOrNegative: {
              break;
            }
            case Signedness::kStrictlyNegative: {
              return Signedness::kStrictlyPositive;
            }
            case Signedness::kNegative: {
              return Signedness::kPositive;
            }
            case Signedness::kStrictlyPositive: {
              return Signedness::kStrictlyNegative;
            }
            case Signedness::kPositive: {
              return Signedness::kNegative;
            }
          }
          break;
        }
        case Signedness::kNegative: {
          switch (rhs) {
            case Signedness::kPositiveOrNegative: {
              break;
            }
            case Signedness::kStrictlyNegative:
            case Signedness::kNegative: {
              return Signedness::kPositive;
            }
            case Signedness::kStrictlyPositive:
            case Signedness::kPositive: {
              return Signedness::kNegative;
            }
          }
          break;
        }
        case Signedness::kStrictlyPositive: {
          return rhs;
        }
        case Signedness::kPositive: {
          switch (rhs) {
            case Signedness::kPositiveOrNegative: {
              break;
            }
            case Signedness::kStrictlyNegative:
            case Signedness::kNegative: {
              return Signedness::kNegative;
            }
            case Signedness::kStrictlyPositive:
            case Signedness::kPositive: {
              return Signedness::kPositive;
            }
          }
          break;
        }
      }
      return Signedness::kPositiveOrNegative;
    };
  }

  Signedness Visit(const SENode* node) {
    switch (node->GetType()) {
      case SENode::Constant:
        return Visit(node->AsSEConstantNode());
        break;
      case SENode::RecurrentExpr:
        return Visit(node->AsSERecurrentNode());
        break;
      case SENode::Negative:
        return Visit(node->AsSENegative());
        break;
      case SENode::CanNotCompute:
        return Visit(node->AsSECantCompute());
        break;
      case SENode::ValueUnknown:
        return Visit(node->AsSEValueUnknown());
        break;
      case SENode::Add:
        return VisitExpr(node, GetAddCombiner());
        break;
      case SENode::Multiply:
        return VisitExpr(node, GetMulCombiner());
        break;
    }
    return Signedness::kPositiveOrNegative;
  }

  Signedness Visit(const SEConstantNode* node) {
    if (0 == node->FoldToSingleValue()) return Signedness::kPositive;
    if (0 < node->FoldToSingleValue()) return Signedness::kStrictlyPositive;
    if (0 > node->FoldToSingleValue()) return Signedness::kStrictlyNegative;
    return Signedness::kPositiveOrNegative;
  }

  Signedness Visit(const SEValueUnknown* node) {
    ir::Instruction* insn =
        context_->get_def_use_mgr()->GetDef(node->UniqueId());
    analysis::Type* type = context_->get_type_mgr()->GetType(insn->type_id());
    assert(type && "Can't retrieve a type for the instruction");
    analysis::Integer* int_type = type->AsInteger();
    assert(type && "Can't retrieve an integer type for the instruction");
    return int_type->IsSigned() ? Signedness::kPositiveOrNegative
                                : Signedness::kPositive;
  }

  Signedness Visit(const SERecurrentNode* node) {
    Signedness coeff_sign = Visit(node->GetCoefficient());
    // SERecurrentNode represent an affine expression in the range [0,
    // loop_bound], so the result cannot be strictly positive or negative.
    switch (coeff_sign) {
      default:
        break;
      case Signedness::kStrictlyNegative:
        coeff_sign = Signedness::kNegative;
        break;
      case Signedness::kStrictlyPositive:
        coeff_sign = Signedness::kPositive;
        break;
    }
    return GetAddCombiner()(coeff_sign, Visit(node->GetOffset()));
  }

  Signedness Visit(const SENegative* node) {
    switch (Visit(*node->begin())) {
      case Signedness::kPositiveOrNegative: {
        return Signedness::kPositiveOrNegative;
      }
      case Signedness::kStrictlyNegative: {
        return Signedness::kStrictlyPositive;
      }
      case Signedness::kNegative: {
        return Signedness::kPositive;
      }
      case Signedness::kStrictlyPositive: {
        return Signedness::kStrictlyNegative;
      }
      case Signedness::kPositive: {
        return Signedness::kNegative;
      }
    }
    return Signedness::kPositiveOrNegative;
  }

  Signedness Visit(const SECantCompute*) {
    return Signedness::kPositiveOrNegative;
  }

  Signedness VisitExpr(
      const SENode* node,
      std::function<Signedness(Signedness, Signedness)> reduce) {
    Signedness result = Visit(*node->begin());
    for (const SENode* operand : ir::make_range(++node->begin(), node->end())) {
      if (result == Signedness::kPositiveOrNegative) {
        return Signedness::kPositiveOrNegative;
      }
      result = reduce(result, Visit(operand));
    }
    return result;
  }

  ir::IRContext* context_;
};
}  // namespace

bool ScalarEvolutionAnalysis::IsAlwaysGreaterThanZero(SENode* node,
                                                      bool* is_gt_zero) const {
  return IsGreaterThanZero(context_).Eval(node, false, is_gt_zero);
}

bool ScalarEvolutionAnalysis::IsAlwaysGreaterOrEqualToZero(
    SENode* node, bool* is_ge_zero) const {
  return IsGreaterThanZero(context_).Eval(node, true, is_ge_zero);
}

namespace {

// Remove N from chains like A * ... * N * ... * Z, if N is not in the chain,
// returns the original chain.
static SENode* RemoveOneNodeFromMultiplyChain(SEMultiplyNode* mul,
                                              const SENode* node) {
  SENode* lhs = mul->GetChildren()[0];
  SENode* rhs = mul->GetChildren()[1];
  if (lhs == node) {
    return rhs;
  }
  if (rhs == node) {
    return lhs;
  }
  if (lhs->AsSEMultiplyNode()) {
    SENode* res = RemoveOneNodeFromMultiplyChain(lhs->AsSEMultiplyNode(), node);
    if (res != lhs)
      return mul->GetParentAnalysis()->CreateMultiplyNode(res, rhs);
  }
  if (rhs->AsSEMultiplyNode()) {
    SENode* res = RemoveOneNodeFromMultiplyChain(rhs->AsSEMultiplyNode(), node);
    if (res != rhs)
      return mul->GetParentAnalysis()->CreateMultiplyNode(res, rhs);
  }

  return mul;
}
}  // namespace

std::pair<SExpression, int64_t> SExpression::operator/(
    SExpression rhs_wrapper) const {
  SENode* lhs = node_;
  SENode* rhs = rhs_wrapper.node_;
  // Check for division by 0.
  if (rhs->AsSEConstantNode() &&
      !rhs->AsSEConstantNode()->FoldToSingleValue()) {
    return {scev_->CreateCantComputeNode(), 0};
  }

  // Trivial case.
  if (lhs->AsSEConstantNode() && rhs->AsSEConstantNode()) {
    int64_t lhs_value = lhs->AsSEConstantNode()->FoldToSingleValue();
    int64_t rhs_value = rhs->AsSEConstantNode()->FoldToSingleValue();
    return {scev_->CreateConstant(lhs_value / rhs_value),
            lhs_value % rhs_value};
  }

  // look for a "c U / U" pattern.
  if (lhs->AsSEMultiplyNode()) {
    assert(lhs->GetChildren().size() == 2 &&
           "More than 2 operand for a multiply node.");
    SENode* res = RemoveOneNodeFromMultiplyChain(lhs->AsSEMultiplyNode(), rhs);
    if (res != lhs) {
      return {res, 0};
    }
  }

  return {scev_->CreateCantComputeNode(), 0};
}

}  // namespace opt
}  // namespace spvtools
