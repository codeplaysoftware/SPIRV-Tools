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

class SENodeSimplifyImpl {
 public:
  SENodeSimplifyImpl(ScalarEvolutionAnalysis* analysis,
                     SENode* node_to_simplify)
      : analysis_(*analysis),
        node_(node_to_simplify),
        constant_accumulator_(0) {}

  SENode* Simplify();

  void FlattenAddExpressions(SENode* child_node,
                             std::vector<SENode*>* nodes_to_add) const;

 private:
  ScalarEvolutionAnalysis& analysis_;
  SENode* node_;

  int64_t constant_accumulator_;

  std::map<SENode*, int64_t> accumulators_;

  void GatherAccumulatorsFromChildNodes(SENode* child, bool negation);

  bool AccumulatorsFromMultiply(SENode* multiply, bool negation);
};

bool SENodeSimplifyImpl::AccumulatorsFromMultiply(SENode* multiply,
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
  if (accumulators_.find(value_unknown) != accumulators_.end()) {
    accumulators_[value_unknown] += constant->FoldToSingleValue() * sign;
  } else {
    accumulators_[value_unknown] = constant->FoldToSingleValue() * sign;
  }

  return true;
}

void SENodeSimplifyImpl::FlattenAddExpressions(
    SENode* child_node, std::vector<SENode*>* nodes_to_add) const {
  for (SENode* child : *child_node) {
    if (child->GetType() == SENode::Add) {
      FlattenAddExpressions(child, nodes_to_add);
    } else {
      // It is easier to just remove and re add the non addition node
      // children with the rest of the nodes. As the vector is sorted when we
      // add a child it is problematic to keep track of the indexes.
      nodes_to_add->push_back(child);
    }
  }
}

SENode* SENodeSimplifyImpl::Simplify() {
  if (node_->GetType() == SENode::Add) {
    std::vector<SENode*> nodes_to_add;

    FlattenAddExpressions(node_, &nodes_to_add);
    node_->GetChildren().clear();

    for (SENode* new_child : nodes_to_add) {
      GatherAccumulatorsFromChildNodes(new_child, false);
    }

    // Fold all the constants into a single constant node.
    if (constant_accumulator_ != 0) {
      node_->AddChild(analysis_.CreateConstant(constant_accumulator_));
    }

    for (auto& pair : accumulators_) {
      SENode* term = pair.first;
      int64_t count = pair.second;

      // We can eliminate the term completely.
      if (count == 0) continue;

      if (count == 1) {
        node_->AddChild(term);
      } else {
        SENode* count_as_constant = analysis_.CreateConstant(count);
        node_->AddChild(analysis_.CreateMultiplyNode(count_as_constant, term));
      }
    }
  }
  return node_;
}

void SENodeSimplifyImpl::GatherAccumulatorsFromChildNodes(SENode* new_child,
                                                          bool negation) {
  int32_t sign = negation ? -1 : 1;

  if (new_child->GetType() == SENode::Constant) {
    // Collect all the constants and add them together.
    constant_accumulator_ += new_child->FoldToSingleValue() * sign;
  } else if (new_child->GetType() == SENode::ValueUnknown) {
    // If we've incountered this term before add to the accumulator for it.
    if (accumulators_.find(new_child) == accumulators_.end())
      accumulators_[new_child] = sign;
    else
      accumulators_[new_child] += sign;

  } else if (new_child->GetType() == SENode::Multiply) {
    if (!AccumulatorsFromMultiply(new_child, negation)) {
      node_->AddChild(new_child);
    }
  } else if (new_child->GetType() == SENode::Negative) {
    SENode* negated_node = new_child->GetChild(0);
    GatherAccumulatorsFromChildNodes(negated_node, !negation);
  } else {
    // If we can't work out how to fold the expression just add it back into
    // the graph.
    node_->AddChild(new_child);
  }
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

  std::unique_ptr<SERecurrentNode> recurrent_node{new SERecurrentNode()};

  recurrent_node->AddOffset(node->GetChild(1));
  recurrent_node->AddCoefficient(recurrent_expr->GetCoefficient());

  return GetCachedOrAdd(std::move(recurrent_node));
}

/*
 * Scalar Analysis simplification public methods.
 */

SENode* ScalarEvolutionAnalysis::SimplifyExpression(SENode* node) {
  SENodeSimplifyImpl impl{this, node};

  return impl.Simplify();
}

}  // namespace opt
}  // namespace spvtools
