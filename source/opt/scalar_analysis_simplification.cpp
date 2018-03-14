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
  void GatherAccumulatorsFromChildNodes(SENode* new_node, SENode* child,
                                        bool negation);

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

  // Simplify the whole graph by linking like terms together in a single flat
  // add node. So X*2 + Y -Y + 3 +6 would become X*2 + 9. Where X and Y are a
  // ValueUnknown node (i.e, a load) or a recurrent expression.
  SENode* SimplifyPolynomial();

  // Each recurrent expression is an expression with respect to a specific loop.
  // If we have two different recurrent terms with respect to the same loop in a
  // single expression then we can fold those terms into a single new term.
  // For instance:
  //
  // induction i = 0, i++
  // temp = i*10
  // array[i+temp]
  //
  // We can fold the i + temp into a single expression. Rec(0,1) + Rec(0,10) can
  // become Rec(0,11).
  SENode* FoldRecurrentExpressions(SENode*);
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
  // We only handle graphs with an addition, multiplication, or negation, at the
  // root.
  if (node_->GetType() != SENode::Add && node_->GetType() != SENode::Multiply &&
      node_->GetType() != SENode::Negative)
    return node_;

  SENode* simplified_polynomial = SimplifyPolynomial();

  SERecurrentNode* recurrent_expr = nullptr;
  node_ = simplified_polynomial;

  // Fold recurrent expressions which are with respect to the same loop into a
  // single recurrent expression.
  simplified_polynomial = FoldRecurrentExpressions(simplified_polynomial);


  // Traverse the new DAG to find the recurrent expression. If there is more
  // than one there is nothing further we can do.
  for (SENode* child : simplified_polynomial->GetChildren()) {
    if (child->GetType() == SENode::RecurrentExpr) {
      recurrent_expr = child->AsSERecurrentNode();
    }
  }

  // We need to count the number of unique recurrent expressions in the DAG to
  // ensure there is only one.
  std::set<SENode*> recurrent_expressions_in_dag;

  for (auto child_iterator = simplified_polynomial->graph_begin();
      child_iterator != simplified_polynomial->graph_end();
      ++child_iterator) {
    // If the child is a recurrent expression add it to the set.
    if (child_iterator->GetType() == SENode::RecurrentExpr) {
      recurrent_expressions_in_dag.insert(&*child_iterator);
    }
  }

  if (recurrent_expr && recurrent_expressions_in_dag.size() == 1 ) {
    return SimplifyRecurrentExpression(recurrent_expr);
  }

  return simplified_polynomial;
}

// Traverse the graph to build up the accumulator objects.
void SENodeSimplifyImpl::GatherAccumulatorsFromChildNodes(SENode* new_node,
                                                          SENode* child,
                                                          bool negation) {
  int32_t sign = negation ? -1 : 1;

  if (child->GetType() == SENode::Constant) {
    // Collect all the constants and add them together.
    constant_accumulator_ +=
        child->AsSEConstantNode()->FoldToSingleValue() * sign;

  } else if (child->GetType() == SENode::ValueUnknown ||
             child->GetType() == SENode::RecurrentExpr) {
    // If we've incountered this term before add to the accumulator for it.
    if (accumulators_.find(child) == accumulators_.end())
      accumulators_[child] = sign;
    else
      accumulators_[child] += sign;

  } else if (child->GetType() == SENode::Multiply) {
    if (!AccumulatorsFromMultiply(child, negation)) {
      new_node->AddChild(child);
    }

  } else if (child->GetType() == SENode::Add) {
    for (SENode* next_child : *child) {
      GatherAccumulatorsFromChildNodes(new_node, next_child, negation);
    }

  } else if (child->GetType() == SENode::Negative) {
    SENode* negated_node = child->GetChild(0);
    GatherAccumulatorsFromChildNodes(new_node, negated_node, !negation);
  } else {
    // If we can't work out how to fold the expression just add it back into
    // the graph.
    new_node->AddChild(child);
  }
}

SERecurrentNode* SENodeSimplifyImpl::UpdateCoefficent(
    SERecurrentNode* recurrent, int64_t coefficent_update) const {
  std::unique_ptr<SERecurrentNode> new_recurrent_node{new SERecurrentNode(
      recurrent->GetParentAnalysis(), recurrent->GetLoop())};

  SENode* new_coefficent = analysis_.CreateMultiplyNode(
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
  GatherAccumulatorsFromChildNodes(new_add.get(), node_, false);

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
        assert(term->GetType() == SENode::RecurrentExpr &&
               "We only handle value unknowns or recurrent expressions");

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

SENode* SENodeSimplifyImpl::FoldRecurrentExpressions(SENode* root) {
  std::unique_ptr<SEAddNode> new_node {
      new SEAddNode(&analysis_)};

  // A mapping of loops to the list of recurrent expressions which are with
  // respect to those loops.
  std::map<const ir::Loop*, std::vector<SERecurrentNode*>> loops_to_recurrent{};

  bool has_multiple_same_loop_recurrent_terms = false;

  for (SENode* child : *root) {
    if (child->GetType() == SENode::RecurrentExpr) {
      const ir::Loop* loop = child->AsSERecurrentNode()->GetLoop();


      SERecurrentNode* rec = child->AsSERecurrentNode();
      if (loops_to_recurrent.find(loop) == loops_to_recurrent.end()) {
        loops_to_recurrent[loop] = {rec};
      } else {
        loops_to_recurrent[loop].push_back(rec);
        has_multiple_same_loop_recurrent_terms = true;
      }
    } else {
      new_node->AddChild(child);
    }
  }

  if (!has_multiple_same_loop_recurrent_terms)
    return root;

  for (auto pair : loops_to_recurrent) {
    std::vector<SERecurrentNode*>& recurrent_expressions = pair.second;

    std::unique_ptr<SENode> new_coefficent{new SEAddNode(&analysis_)};
    std::unique_ptr<SENode> new_offset{new SEAddNode(&analysis_)};

    for (SERecurrentNode* node : recurrent_expressions) {
      new_coefficent->AddChild(node->GetCoefficient());
      new_offset->AddChild(node->GetOffset());
    }

    std::unique_ptr<SERecurrentNode> new_recurrent{
        new SERecurrentNode(&analysis_, pair.first)};

    new_recurrent->AddCoefficient(
        analysis_.SimplifyExpression(new_coefficent.get()));

    new_recurrent->AddOffset(analysis_.SimplifyExpression(new_offset.get()));

    new_node->AddChild(analysis_.GetCachedOrAdd(std::move(new_recurrent)));
  }

  // If we only have one child in the add just return that.
  if (new_node->GetChildren().size() == 1) {
    return new_node->GetChild(0);
  }

  return analysis_.GetCachedOrAdd(std::move(new_node));
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
