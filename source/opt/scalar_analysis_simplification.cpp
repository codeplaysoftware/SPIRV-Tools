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

#include <functional>
#include "opt/scalar_analysis.h"

namespace spvtools {
namespace opt {

// Implementation of the functions which are used to simplify the graph. Graphs
// of unknowns, multiplies, additions, and constants can be turned into a linear
// add node with each term as a child. For instance a large graph built from, X
// + X*2 + Y - Y*3 + 4 - 1, would become a single add expression with the
// children X*3, -Y*2, and the constant 3. Graphs containing a recurrent
// expression will be simplified to represent the entire graph around a single
// recurrent expression. So for an induction variable (i=0, i++) if you add 1 to
// i in an expression we can rewrite the graph of that expression to be a single
// recurrent expression of (i=1,i++).
class SENodeSimplifyImpl {
 public:
  SENodeSimplifyImpl(ScalarEvolutionAnalysis* analysis,
                     SENode* node_to_simplify)
      : analysis_(*analysis),
        node_(node_to_simplify),
        constant_accumulator_(0) {}

  // Return the result of the simplification.
  SENode* Simplify();

 private:
  // A reference the the analysis which requested the simplification.
  ScalarEvolutionAnalysis& analysis_;

  // The node being simplified.
  SENode* node_;

  // An accumulator of the net result of all the constant operations performed
  // in a graph.
  int64_t constant_accumulator_;

  // An accumulator for each of the non constant terms in the graph.
  std::map<SENode*, int64_t> accumulators_;

  // Recursively descend through the graph to build up the accumulator objects
  // which are used to flatten the graph. |child| is the node currenty being
  // traversed and the |negation| flag is used to signify that this operation
  // was preceded by a unary negative operation and as such the result should be
  // negated.
  void GatherAccumulatorsFromChildNodes(SENode* child, bool negation);

  // Given a |multiply| node add to the accumulators for the term type within
  // the |multiply| expression. Will return true if the accumulators could be
  // calculated successfully. If the |multiply| is in any form other than
  // unknown*constant then we return false. |negation| signifies that the
  // operation was preceded by a unary negative.
  bool AccumulatorsFromMultiply(SENode* multiply, bool negation);

  SERecurrentNode* UpdateCoefficent(SERecurrentNode* recurrent,
                                    int64_t coefficent_update) const;

  // If the graph contains a recurrent expression, ie, an expression with the
  // loop iterations as a term in the expression, then the whole expression
  // can be rewritten to be a recurrent expression.
  SENode* SimplifyRecurrentExpression(SERecurrentNode* node);

  // The case of a graph with no recurrent expressions in it.
  SENode* SimplifyPolynomial();
};

// From a |multiply| build up the accumulator objects.
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
  if (operand_1->GetType() == SENode::ValueUnknown ||
      operand_1->GetType() == SENode::RecurrentExpr)
    value_unknown = operand_1;
  else if (operand_2->GetType() == SENode::ValueUnknown ||
           operand_2->GetType() == SENode::RecurrentExpr)
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

  // Add the result of the multiplication to the accumulators.
  if (accumulators_.find(value_unknown) != accumulators_.end()) {
    accumulators_[value_unknown] +=
        constant->AsSEConstantNode()->FoldToSingleValue() * sign;
  } else {
    accumulators_[value_unknown] =
        constant->AsSEConstantNode()->FoldToSingleValue() * sign;
  }

  return true;
}

SENode* SENodeSimplifyImpl::Simplify() {
  // We only handle graphs with an addition at the root.
  if (node_->GetType() != SENode::Add && node_->GetType() != SENode::Multiply)
    return node_;

  SENode* simplified_polynomial = SimplifyPolynomial();

  bool multiple_recurrent_expressions = false;
  SERecurrentNode* recurrent_expr = nullptr;
  node_ = simplified_polynomial;

  // Traverse the new DAG to find the recurrent expression. If there is more
  // than one there is nothing further we can do.
  for (SENode* child : simplified_polynomial->GetChildren()) {
    if (child->GetType() == SENode::RecurrentExpr) {
      // We only handle graphs with a single recurrent expression in them.
      if (recurrent_expr) {
        multiple_recurrent_expressions = true;
      }
      recurrent_expr = child->AsSERecurrentNode();
    }
  }

  if (recurrent_expr && !multiple_recurrent_expressions) {
    return SimplifyRecurrentExpression(recurrent_expr);
  }

  return simplified_polynomial;
}

// Traverse the graph to build up the accumulator objects.
void SENodeSimplifyImpl::GatherAccumulatorsFromChildNodes(SENode* new_child,
                                                          bool negation) {
  int32_t sign = negation ? -1 : 1;

  if (new_child->GetType() == SENode::Constant) {
    // Collect all the constants and add them together.
    constant_accumulator_ +=
        new_child->AsSEConstantNode()->FoldToSingleValue() * sign;

  } else if (new_child->GetType() == SENode::ValueUnknown ||
             new_child->GetType() == SENode::RecurrentExpr) {
    // If we've incountered this term before add to the accumulator for it.
    if (accumulators_.find(new_child) == accumulators_.end())
      accumulators_[new_child] = sign;
    else
      accumulators_[new_child] += sign;

  } else if (new_child->GetType() == SENode::Multiply) {
    if (!AccumulatorsFromMultiply(new_child, negation)) {
      node_->AddChild(new_child);
    }

  } else if (new_child->GetType() == SENode::Add) {
    for (SENode* child : *new_child) {
      GatherAccumulatorsFromChildNodes(child, negation);
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

SERecurrentNode* SENodeSimplifyImpl::UpdateCoefficent(
    SERecurrentNode* recurrent, int64_t coefficent_update) const {
  std::unique_ptr<SERecurrentNode> new_recurrent_node{new SERecurrentNode(
      recurrent->GetParentAnalysis(), recurrent->GetLoop())};

  SENode* new_coefficent = analysis_.CreateAddNode(
      recurrent->GetCoefficient(), analysis_.CreateConstant(coefficent_update));

  // See if the node can be simplified.
  SENode* simplified = analysis_.SimplifyExpression(new_coefficent);
  if (simplified->GetType() != SENode::CanNotCompute)
    new_coefficent = simplified;

  new_recurrent_node->AddOffset(recurrent->GetOffset());
  new_recurrent_node->AddCoefficient(new_coefficent);

  return analysis_.GetCachedOrAdd(std::move(new_recurrent_node))
      ->AsSERecurrentNode();
}

// Simplify all the terms in the polynomial function.
SENode* SENodeSimplifyImpl::SimplifyPolynomial() {
  std::unique_ptr<SENode> new_add{new SEAddNode(node_->GetParentAnalysis())};

  // Traverse the graph and gather the accumulators from it.
  GatherAccumulatorsFromChildNodes(node_, false);

  // Fold all the constants into a single constant node.
  if (constant_accumulator_ != 0) {
    new_add->AddChild(analysis_.CreateConstant(constant_accumulator_));
  }

  for (auto& pair : accumulators_) {
    SENode* term = pair.first;
    int64_t count = pair.second;

    // We can eliminate the term completely.
    if (count == 0) continue;

    if (count == 1) {
      new_add->AddChild(term);
    } else if (count == -1) {
      new_add->AddChild(analysis_.CreateNegation(term));
    } else {
      // Output value unknown terms as count*term and output recurrent
      // expression terms as rec(offset, coefficient + count) offset and
      // coefficient are the same as in the original expression.
      if (term->GetType() == SENode::ValueUnknown) {
        SENode* count_as_constant = analysis_.CreateConstant(count);
        new_add->AddChild(
            analysis_.CreateMultiplyNode(count_as_constant, term));
      } else {
        // Create a new recurrent expression by adding the count to the
        // coefficient of the old one.
        new_add->AddChild(UpdateCoefficent(term->AsSERecurrentNode(), count));
      }
    }
  }

  // If there is only one term in the addition left just return that term.
  if (new_add->GetChildren().size() == 1) {
    return new_add->GetChild(0);
  }

  // If there are no terms left in the addition just return 0.
  if (new_add->GetChildren().size() == 0) {
    return analysis_.CreateConstant(0);
  }

  return analysis_.GetCachedOrAdd(std::move(new_add));
}

SENode* SENodeSimplifyImpl::SimplifyRecurrentExpression(
    SERecurrentNode* recurrent_expr) {
  const std::vector<SENode*>& children = node_->GetChildren();

  std::unique_ptr<SERecurrentNode> recurrent_node{new SERecurrentNode(
      recurrent_expr->GetParentAnalysis(), recurrent_expr->GetLoop())};

  // Create and simplify the new offset node.
  SENode* new_offset = analysis_.CreateAddNode(recurrent_expr->GetOffset(),
                                               analysis_.CreateConstant(0));

  for (SENode* child : children) {
    if (child->GetType() != SENode::RecurrentExpr) {
      new_offset->AddChild(child);
    }
  }

  // Simplify the new offset.
  SENode* simplified_child = analysis_.SimplifyExpression(new_offset);

  // If the new offset cannot be simplified exit out for the main node as well.
  if (simplified_child->GetType() != SENode::CanNotCompute) {
    new_offset = simplified_child;
  }

  recurrent_node->AddOffset(new_offset);
  recurrent_node->AddCoefficient(recurrent_expr->GetCoefficient());

  return analysis_.GetCachedOrAdd(std::move(recurrent_node));
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
