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

  // Flatten a graph with an add at its root to be a broader graph with a single
  // add with multiple children, folding constants where possible.
  // E.G X+ X*2 + Y - Y*3 + 4 - 1 =  X*3 - Y*2 + 3
  void FlattenAddExpressions(SENode* child_node,
                             std::vector<SENode*>* nodes_to_add) const;

  // If the graph contains a recurrent expression, ie, an expression with the
  // loop iterations as a term in the expression, then the whole expression
  // can be rewritten to be a recurrent expression.
  SENode* SimplifyRecurrentExpression(SERecurrentNode* node);

  // The case of a graph with no recurrent expressions in it.
  SENode* SimplifyNonRecurrent();
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

  // Add the result of the multiplication to the accumulators.
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
  // We only handle graphs with an addition at the root.
  if (node_->GetType() != SENode::Add) return analysis_.CreateCantComputeNode();

  bool multiple_recurrent_expressions = false;
  SERecurrentNode* recurrent_expr = nullptr;

  // Traverse the DAG to find the recurrent expression.
  for (auto child = node_->graph_begin(); child != node_->graph_end();
       ++child) {
    if (child->GetType() == SENode::RecurrentExpr) {
      // We only handle graphs with a single recurrent expression in them.
      if (recurrent_expr) {
        multiple_recurrent_expressions = true;
      }
      recurrent_expr = static_cast<SERecurrentNode*>(&*child);
    }
  }

  if (recurrent_expr) {
    // We only handle single recurrent expressions at the moment.
    if (multiple_recurrent_expressions)
      return analysis_.CreateCantComputeNode();

    return SimplifyRecurrentExpression(recurrent_expr);
  }


  return SimplifyNonRecurrent();
}

// Traverse the graph to build up the accumulator objects.
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

SENode* SENodeSimplifyImpl::SimplifyNonRecurrent() {
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
  return node_;
}


SENode* SENodeSimplifyImpl::SimplifyRecurrentExpression(
    SERecurrentNode* recurrent_expr) {
  // Work out if the recurrent expression we found is an immediate child of the
  // root being simplified. Else exit out.
  bool is_immediate_child = false;

  size_t index_of_non_recurrent_node = 0;

  const std::vector<SENode*>& children = node_->GetChildren();

  if (children.size() != 2) return analysis_.CreateCantComputeNode();

  // Check that the recurrent expression is an immediate child of the node being
  // simplified.
  if (children[0] == recurrent_expr) {
    is_immediate_child = true;
    index_of_non_recurrent_node = 1;
  }

  if (children[1] == recurrent_expr) {
    is_immediate_child = true;
    index_of_non_recurrent_node = 0;
  }

  // If the recurrent expression is not a child of the node being simplified.
  if (!is_immediate_child) return analysis_.CreateCantComputeNode();

  std::unique_ptr<SERecurrentNode> recurrent_node{
      new SERecurrentNode(recurrent_expr->GetLoop())};

  // Create and simplify the new offset node.
  SENode* new_offset = analysis_.CreateAddNode(
      recurrent_expr->GetOffset(), children[index_of_non_recurrent_node]);

  // Simplify the new offset.
  SENode* simplified_child = analysis_.SimplifyExpression(new_offset);

  // If the new offset cannot be simplified exit out for the main node as well.
  if (simplified_child->GetType() == SENode::CanNotCompute) {
    return simplified_child;
  }

  recurrent_node->AddOffset(simplified_child);
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
