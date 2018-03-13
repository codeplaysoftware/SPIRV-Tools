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

#include "opt/loop_dependence.h"
namespace spvtools {
namespace opt {

bool LoopDependenceAnalysis::GetDependence(const ir::Instruction* source,
                                           const ir::Instruction* destination,
                                           DistanceVector* distance_vector) {
  ir::Instruction* source_access_chain =
      context_->get_def_use_mgr()->GetDef(source->GetSingleWordInOperand(0));
  ir::Instruction* destination_access_chain =
      context_->get_def_use_mgr()->GetDef(
          destination->GetSingleWordInOperand(0));

  // If the access chains aren't collecting from the same structure there is no
  // dependence.
  ir::Instruction* source_array = context_->get_def_use_mgr()->GetDef(
      source_access_chain->GetSingleWordInOperand(0));
  ir::Instruction* destination_array = context_->get_def_use_mgr()->GetDef(
      destination_access_chain->GetSingleWordInOperand(0));
  if (source_array != destination_array) {
    distance_vector->direction = DistanceVector::Directions::NONE;
    return true;
  }

  // If the access chains somehow have a different number of operands they store
  // and load must be independent.
  if (source_access_chain->NumOperands() !=
      destination_access_chain->NumOperands()) {
    distance_vector->direction = DistanceVector::Directions::NONE;
    return true;
  }

  // To handle multiple subscripts we must get every operand in the access
  // chains past the first.
  std::vector<ir::Instruction*> source_subscripts{};
  std::vector<ir::Instruction*> destination_subscripts{};
  std::vector<DistanceVector> distance_vector_entries{};
  for (int i = 1;
       i < static_cast<int>(source_access_chain->NumInOperandWords()); ++i) {
    source_subscripts.push_back(context_->get_def_use_mgr()->GetDef(
        source_access_chain->GetSingleWordInOperand(i)));
    destination_subscripts.push_back(context_->get_def_use_mgr()->GetDef(
        destination_access_chain->GetSingleWordInOperand(i)));
    distance_vector_entries.push_back(DistanceVector{});
  }

  // Go through each subscript testing for independence.
  // If any subscript results in independence, we prove independence between the
  // load and store.
  // If we can't prove independence we store what information we can gather in
  // a DistanceVector.
  for (size_t subscript = 0; subscript < source_subscripts.size();
       ++subscript) {
    SENode* source_node = scalar_evolution_.SimplifyExpression(
        scalar_evolution_.AnalyzeInstruction(source_subscripts[subscript]));
    SENode* destination_node = scalar_evolution_.SimplifyExpression(
        scalar_evolution_.AnalyzeInstruction(
            destination_subscripts[subscript]));

    // If either node is simplified to a CanNotCompute we can't perform any
    // analysis so must assume <=> dependence and return.
    if (source_node->GetType() == SENode::CanNotCompute ||
        destination_node->GetType() == SENode::CanNotCompute) {
      distance_vector->direction = DistanceVector::Directions::ALL;
      break;
    }

    // Neither node is a recurrent expr so we use a ZIV test.
    if (source_node->GetType() != SENode::RecurrentExpr &&
        destination_node->GetType() != SENode::RecurrentExpr) {
      if (ZIVTest(source_node, destination_node,
                  &distance_vector_entries[subscript])) {
        distance_vector->direction = DistanceVector::Directions::NONE;
        return true;
      }
    }

    // source is not a recurrent expr but destination is so we can try a
    // WeakZeroSrcTest.
    if (source_node->GetType() != SENode::RecurrentExpr &&
        destination_node->GetType() == SENode::RecurrentExpr) {
      if (WeakZeroSourceSIVTest(
              source_node, destination_node->AsSERecurrentNode(),
              destination_node->AsSERecurrentNode()->GetCoefficient(),
              &distance_vector_entries[subscript])) {
        distance_vector->direction = DistanceVector::Directions::NONE;
        return true;
      }
    }

    // source is a recurrent expr but destination is not so we can try a
    // WeakZeroDestTest.
    if (source_node->GetType() == SENode::RecurrentExpr &&
        destination_node->GetType() != SENode::RecurrentExpr) {
      if (WeakZeroDestinationSIVTest(
              source_node->AsSERecurrentNode(), destination_node,
              source_node->AsSERecurrentNode()->GetCoefficient(),
              &distance_vector_entries[subscript])) {
        distance_vector->direction = DistanceVector::Directions::NONE;
        return true;
      }
    }

    // Both source and destination are recurrent exprs. We should narrow down to
    // StrongSIV
    // or WeakCrossingSIV tests.
    if (source_node->GetType() == SENode::RecurrentExpr &&
        destination_node->GetType() == SENode::RecurrentExpr) {
      SERecurrentNode* source_recurrent_expr = source_node->AsSERecurrentNode();
      SERecurrentNode* destination_recurrent_expr =
          destination_node->AsSERecurrentNode();

      // If the coefficients are identical we can use StrongSIV.
      if (source_recurrent_expr->GetCoefficient() ==
          destination_recurrent_expr->GetCoefficient()) {
        if (StrongSIVTest(
                source_recurrent_expr->AsSERecurrentNode(),
                destination_recurrent_expr->AsSERecurrentNode(),
                source_recurrent_expr->AsSERecurrentNode()->GetCoefficient(),
                &distance_vector_entries[subscript])) {
          distance_vector->direction = DistanceVector::Directions::NONE;
          return true;
        }
      }

      // If the coefficients are opposite (coefficient_1 == -coefficient_2) we
      // can use a
      // WeakCrossingSIV test.
      if (source_recurrent_expr->GetCoefficient() ==
          scalar_evolution_.CreateNegation(
              destination_recurrent_expr->GetCoefficient())) {
        if (WeakCrossingSIVTest(
                source_recurrent_expr->AsSERecurrentNode(),
                destination_recurrent_expr->AsSERecurrentNode(),
                source_recurrent_expr->AsSERecurrentNode()->GetCoefficient(),
                &distance_vector_entries[subscript])) {
          distance_vector->direction = DistanceVector::Directions::NONE;
          return true;
        }
      }
    }
  }

  // We were unable to prove independence so must gather all of the direction
  // information we found.

  distance_vector->direction = DistanceVector::Directions::NONE;
  for (size_t subscript = 0; subscript < distance_vector_entries.size();
       ++subscript) {
    if (distance_vector_entries.size() == 1) {
      distance_vector->distance = distance_vector_entries[0].distance;
    }
    distance_vector->direction = static_cast<DistanceVector::Directions>(
        distance_vector->direction |
        distance_vector_entries[subscript].direction);
    if (distance_vector_entries[subscript].peel_first) {
      distance_vector->peel_first = true;
    }
    if (distance_vector_entries[subscript].peel_last) {
      distance_vector->peel_last = true;
    }
  }

  return false;
}

bool LoopDependenceAnalysis::ZIVTest(SENode* source, SENode* destination,
                                     DistanceVector* distance_vector) {
  // If source == destination, dependence with direction = and distance 0.
  if (source == destination) {
    distance_vector->direction = DistanceVector::Directions::EQ;
    distance_vector->distance = 0;
    return false;
  } else {
    // Otherwise we prove independence.
    distance_vector->direction = DistanceVector::Directions::NONE;
    return true;
  }
}

bool LoopDependenceAnalysis::StrongSIVTest(SERecurrentNode* source,
                                           SERecurrentNode* destination,
                                           SENode* coefficient,
                                           DistanceVector* distance_vector) {
  // Build an SENode for distance.
  SENode* source_offset = source->GetOffset();
  SENode* destination_offset = destination->GetOffset();
  SENode* offset_delta = scalar_evolution_.SimplifyExpression(
      scalar_evolution_.CreateSubtraction(source_offset, destination_offset));

  // Scalar evolution doesn't perform division, so we must fold to constants and
  // do it manually.
  // We must check the offset delta and coefficient are constants.
  int64_t distance = 0;
  SEConstantNode* delta_constant = offset_delta->AsSEConstantNode();
  SEConstantNode* coefficient_constant = coefficient->AsSEConstantNode();
  if (delta_constant && coefficient_constant) {
    int64_t delta_value = delta_constant->FoldToSingleValue();
    int64_t coefficient_value = coefficient_constant->FoldToSingleValue();
    // Check if the distance is not integral to try to prove independence.
    if (delta_value % coefficient_value != 0) {
      distance_vector->direction = DistanceVector::Directions::NONE;
      return true;
    } else {
      distance = delta_value / coefficient_value;
    }
  } else {
    // If we can't fold delta and coefficient to single values we can't produce
    // distance.
    // As a result we can't perform the rest of the pass and must assume
    // dependence in all directions.
    distance_vector->distance = DistanceVector::Directions::ALL;
    return false;
  }

  // Next we gather the upper and lower bounds as constants if possible. If
  // distance > upper_bound - lower_bound we prove independence.
  SEConstantNode* lower_bound = GetLowerBound()->AsSEConstantNode();
  SEConstantNode* upper_bound = GetUpperBound()->AsSEConstantNode();
  if (lower_bound && upper_bound) {
    SENode* bounds = scalar_evolution_.SimplifyExpression(
        scalar_evolution_.CreateSubtraction(upper_bound, lower_bound));

    if (bounds->GetType() == SENode::SENodeType::Constant) {
      int64_t bounds_value = bounds->AsSEConstantNode()->FoldToSingleValue();

      // If the absolute value of the distance is > upper bound - lower bound
      // then we prove independence.
      if (llabs(distance) > bounds_value) {
        distance_vector->direction = DistanceVector::Directions::NONE;
        distance_vector->distance = distance;
        return true;
      }
    }
  }

  // Otherwise we can get a direction as follows
  //             { < if distance > 0
  // direction = { = if distance == 0
  //             { > if distance < 0

  if (distance > 0) {
    distance_vector->direction = DistanceVector::Directions::LT;
    distance_vector->distance = distance;

    return false;
  }
  if (distance == 0) {
    distance_vector->direction = DistanceVector::Directions::EQ;
    distance_vector->distance = 0;
    return false;
  }
  if (distance < 0) {
    distance_vector->direction = DistanceVector::Directions::GT;
    distance_vector->distance = distance;
    return false;
  }

  // We were unable to prove independence or discern any additional information
  // Must assume <=> direction.
  distance_vector->direction = DistanceVector::Directions::ALL;
  return false;
}

bool LoopDependenceAnalysis::SymbolicStrongSIVTest(
    SENode* source, SENode* destination, DistanceVector* distance_vector) {
  SENode* source_destination_delta = scalar_evolution_.SimplifyExpression(
      scalar_evolution_.CreateSubtraction(source, destination));
  // Using the offset delta we can prove loop bounds independence under some
  // symbolic cases
  if (IsProvablyOutwithLoopBounds(source_destination_delta)) {
    distance_vector->direction = DistanceVector::Directions::NONE;
  }
  return false;
}

bool LoopDependenceAnalysis::WeakZeroSourceSIVTest(
    SENode* source, SERecurrentNode* destination, SENode* coefficient,
    DistanceVector* distance_vector) {
  // Build an SENode for distance.
  SENode* destination_offset = destination->GetOffset();
  SENode* delta = scalar_evolution_.SimplifyExpression(
      scalar_evolution_.CreateSubtraction(source, destination_offset));

  // Scalar evolution doesn't perform division, so we must fold to constants and
  // do it manually.
  int64_t distance = 0;
  SEConstantNode* delta_constant = delta->AsSEConstantNode();
  SEConstantNode* coefficient_constant = coefficient->AsSEConstantNode();
  if (delta_constant && coefficient_constant) {
    int64_t delta_value = delta_constant->FoldToSingleValue();
    int64_t coefficient_value = coefficient_constant->FoldToSingleValue();
    // Check if the distance is not integral.
    if (delta_value % coefficient_value != 0) {
      distance_vector->direction = DistanceVector::Directions::NONE;
      return true;
    } else {
      distance = delta_value / coefficient_value;
    }
  }

  // If we can prove the distance is outside the bounds we prove independence.
  SEConstantNode* lower_bound = GetLowerBound()->AsSEConstantNode();
  SEConstantNode* upper_bound = GetUpperBound()->AsSEConstantNode();
  if (lower_bound && upper_bound) {
    int64_t lower_bound_value = lower_bound->FoldToSingleValue();
    int64_t upper_bound_value = upper_bound->FoldToSingleValue();
    if (!IsWithinBounds(distance, lower_bound_value, upper_bound_value)) {
      distance_vector->direction = DistanceVector::Directions::NONE;
      distance_vector->distance = distance;
      return true;
    }
  }

  // Now we want to see if we can detect to peel the first or last iterations.

  // We get the FirstTripValue as FirstTripInduction * destination_coeff +
  // destination_offset.
  // We build the value of the first trip as an SENode.
  SENode* induction_first_trip_SENode = GetFirstTripInductionNode();
  SENode* induction_first_trip_mult_coefficient_SENode =
      scalar_evolution_.CreateMultiplyNode(induction_first_trip_SENode,
                                           coefficient);
  SENode* first_trip_SENode =
      scalar_evolution_
          .SimplifyExpression(scalar_evolution_.CreateAddNode(
              induction_first_trip_mult_coefficient_SENode, destination_offset))
          ->AsSEConstantNode();

  // If source == FirstTripValue, peel_first.
  if (first_trip_SENode != nullptr) {
    if (source == first_trip_SENode) {
      // We have found that peeling the first iteration will break dependency.
      distance_vector->peel_first = true;
      return false;
    }
  }

  // We get the LastTripValue as LastTripInduction * destination_coeff +
  // destination_offset.
  // We build the value of the final trip as an SENode.
  SENode* induction_final_trip_SENode = GetFinalTripInductionNode();
  SENode* induction_final_trip_mult_coefficient_SENode =
      scalar_evolution_.CreateMultiplyNode(induction_final_trip_SENode,
                                           coefficient);
  SENode* final_trip_SENode =
      scalar_evolution_
          .SimplifyExpression(scalar_evolution_.CreateAddNode(
              induction_final_trip_mult_coefficient_SENode, destination_offset))
          ->AsSEConstantNode();

  // If source == LastTripValue, peel_last.
  if (final_trip_SENode != nullptr) {
    if (source == final_trip_SENode) {
      // We have found that peeling the last iteration will break dependency.
      distance_vector->peel_last = true;
      return false;
    }
  }

  // We were unable to prove independence or discern any additional information.
  // Must assume <=> direction.
  distance_vector->direction = DistanceVector::Directions::ALL;
  return false;
}

bool LoopDependenceAnalysis::WeakZeroDestinationSIVTest(
    SERecurrentNode* source, SENode* destination, SENode* coefficient,
    DistanceVector* distance_vector) {
  // Build an SENode for distance.
  SENode* source_offset = source->GetOffset();
  SENode* delta = scalar_evolution_.SimplifyExpression(
      scalar_evolution_.CreateSubtraction(destination, source_offset));

  // Scalar evolution doesn't perform division, so we must fold to constants and
  // do it manually.
  int64_t distance = 0;
  SEConstantNode* delta_constant = delta->AsSEConstantNode();
  SEConstantNode* coefficient_constant = coefficient->AsSEConstantNode();
  if (delta_constant && coefficient_constant) {
    int64_t delta_value = delta_constant->FoldToSingleValue();
    int64_t coefficient_value = coefficient_constant->FoldToSingleValue();
    // Check if the distance is not integral.
    if (delta_value % coefficient_value != 0) {
      distance_vector->direction = DistanceVector::Directions::NONE;
      return true;
    } else {
      distance = delta_value / coefficient_value;
    }
  }

  // If we can prove the distance is outside the bounds we prove independence.
  SEConstantNode* lower_bound = GetLowerBound()->AsSEConstantNode();
  SEConstantNode* upper_bound = GetUpperBound()->AsSEConstantNode();
  if (lower_bound && upper_bound) {
    int64_t lower_bound_value = lower_bound->FoldToSingleValue();
    int64_t upper_bound_value = upper_bound->FoldToSingleValue();
    if (!IsWithinBounds(distance, lower_bound_value, upper_bound_value)) {
      distance_vector->direction = DistanceVector::Directions::NONE;
      distance_vector->distance = distance;
      return true;
    }
  }

  // Now we want to see if we can detect to peel the first or last iterations.

  // We get the FirstTripValue as FirstTripInduction * source_coeff +
  // source_offset.
  // We build the value of the first trip as an SENode.
  SENode* induction_first_trip_SENode = GetFirstTripInductionNode();
  SENode* induction_first_trip_mult_coefficient_SENode =
      scalar_evolution_.CreateMultiplyNode(induction_first_trip_SENode,
                                           coefficient);
  SENode* first_trip_SENode =
      scalar_evolution_.SimplifyExpression(scalar_evolution_.CreateAddNode(
          induction_first_trip_mult_coefficient_SENode, source_offset));

  // If destination == FirstTripValue, peel_first.
  if (first_trip_SENode != nullptr) {
    if (destination == first_trip_SENode) {
      // We have found that peeling the first iteration will break dependency.
      distance_vector->peel_first = true;
      return false;
    }
  }

  // We get the LastTripValue as LastTripInduction * source_coeff +
  // source_offset.
  // We build the value of the final trip as an SENode.
  SENode* induction_final_trip_SENode = GetFinalTripInductionNode();
  SENode* induction_final_trip_mult_coefficient_SENode =
      scalar_evolution_.CreateMultiplyNode(induction_final_trip_SENode,
                                           coefficient);
  SENode* final_trip_SENode =
      scalar_evolution_.SimplifyExpression(scalar_evolution_.CreateAddNode(
          induction_final_trip_mult_coefficient_SENode, source_offset));

  // If destination == LastTripValue, peel_last.
  if (final_trip_SENode != nullptr) {
    if (destination == final_trip_SENode) {
      // We have found that peeling the last iteration will break dependency.
      distance_vector->peel_last = true;
      return false;
    }
  }

  // We were unable to prove independence or discern any additional information.
  // Must assume <=> direction.
  distance_vector->direction = DistanceVector::Directions::ALL;
  return false;
}

bool LoopDependenceAnalysis::WeakCrossingSIVTest(
    SERecurrentNode* source, SERecurrentNode* destination, SENode* coefficient,
    DistanceVector* distance_vector) {
  // Build an SENode for distance.
  SENode* offset_delta =
      scalar_evolution_.SimplifyExpression(scalar_evolution_.CreateSubtraction(
          destination->GetOffset(), source->GetOffset()));

  // Scalar evolution doesn't perform division, so we must fold to constants and
  // do it manually.
  int64_t distance = 0;
  SEConstantNode* delta_constant = offset_delta->AsSEConstantNode();
  SEConstantNode* coefficient_constant = coefficient->AsSEConstantNode();
  if (delta_constant && coefficient_constant) {
    int64_t delta_value = delta_constant->FoldToSingleValue();
    int64_t coefficient_value = coefficient_constant->FoldToSingleValue();
    // Check if the distance is not integral or if it has a non-integral part
    // equal to 1/2.
    if (delta_value % (2 * coefficient_value) != 0 ||
        (delta_value % (2 * coefficient_value)) / (2 * coefficient_value) !=
            0.5) {
      distance_vector->direction = DistanceVector::Directions::NONE;
      return true;
    } else {
      distance = delta_value / (2 * coefficient_value);
    }

    if (distance == 0) {
      distance_vector->direction = DistanceVector::Directions::EQ;
      distance_vector->distance = 0;
      return false;
    }
  }

  // We were unable to prove independence or discern any additional information.
  // Must assume <=> direction.
  distance_vector->direction = DistanceVector::Directions::ALL;
  return false;
}

SENode* LoopDependenceAnalysis::GetLowerBound() {
  ir::Instruction* cond_inst = loop_.GetConditionInst();
  if (!cond_inst) {
    return nullptr;
  }
  switch (cond_inst->opcode()) {
    case SpvOpULessThan:
    case SpvOpSLessThan:
    case SpvOpULessThanEqual:
    case SpvOpSLessThanEqual: {
      ir::Instruction* lower_inst = context_->get_def_use_mgr()->GetDef(
          cond_inst->GetSingleWordInOperand(0));
      if (lower_inst->opcode() == SpvOpPhi) {
        lower_inst = context_->get_def_use_mgr()->GetDef(
            lower_inst->GetSingleWordInOperand(0));
        // We don't handle looking through multiple phis.
        if (lower_inst->opcode() == SpvOpPhi) {
          return nullptr;
        }
      }
      SENode* lower_bound = scalar_evolution_.SimplifyExpression(
          scalar_evolution_.AnalyzeInstruction(lower_inst));
      return lower_bound;
      break;
    }
    case SpvOpUGreaterThan:
    case SpvOpSGreaterThan: {
      ir::Instruction* lower_inst = context_->get_def_use_mgr()->GetDef(
          cond_inst->GetSingleWordInOperand(1));
      if (lower_inst->opcode() == SpvOpPhi) {
        lower_inst = context_->get_def_use_mgr()->GetDef(
            lower_inst->GetSingleWordInOperand(0));
        // We don't handle looking through multiple phis.
        if (lower_inst->opcode() == SpvOpPhi) {
          return nullptr;
        }
      }
      SENode* lower_bound = scalar_evolution_.SimplifyExpression(
          scalar_evolution_.AnalyzeInstruction(lower_inst));
      if (lower_bound) {
        return scalar_evolution_.SimplifyExpression(
            scalar_evolution_.CreateAddNode(
                lower_bound, scalar_evolution_.CreateConstant(1)));
      }
      break;
    }
    case SpvOpUGreaterThanEqual:
    case SpvOpSGreaterThanEqual: {
      ir::Instruction* lower_inst = context_->get_def_use_mgr()->GetDef(
          cond_inst->GetSingleWordInOperand(1));
      if (lower_inst->opcode() == SpvOpPhi) {
        lower_inst = context_->get_def_use_mgr()->GetDef(
            lower_inst->GetSingleWordInOperand(0));
        // We don't handle looking through multiple phis.
        if (lower_inst->opcode() == SpvOpPhi) {
          return nullptr;
        }
      }
      SENode* lower_bound = scalar_evolution_.SimplifyExpression(
          scalar_evolution_.AnalyzeInstruction(lower_inst));
      return lower_bound;
    } break;
    default:
      return nullptr;
  }
  return nullptr;
}

SENode* LoopDependenceAnalysis::GetUpperBound() {
  ir::Instruction* cond_inst = loop_.GetConditionInst();
  if (!cond_inst) {
    return nullptr;
  }
  switch (cond_inst->opcode()) {
    case SpvOpULessThan:
    case SpvOpSLessThan: {
      ir::Instruction* upper_inst = context_->get_def_use_mgr()->GetDef(
          cond_inst->GetSingleWordInOperand(1));
      if (upper_inst->opcode() == SpvOpPhi) {
        upper_inst = context_->get_def_use_mgr()->GetDef(
            upper_inst->GetSingleWordInOperand(0));
        // We don't handle looking through multiple phis.
        if (upper_inst->opcode() == SpvOpPhi) {
          return nullptr;
        }
      }
      SENode* upper_bound = scalar_evolution_.SimplifyExpression(
          scalar_evolution_.AnalyzeInstruction(upper_inst));
      if (upper_bound) {
        return scalar_evolution_.SimplifyExpression(
            scalar_evolution_.CreateSubtraction(
                upper_bound, scalar_evolution_.CreateConstant(1)));
      }
    }
    case SpvOpULessThanEqual:
    case SpvOpSLessThanEqual: {
      ir::Instruction* upper_inst = context_->get_def_use_mgr()->GetDef(
          cond_inst->GetSingleWordInOperand(1));
      if (upper_inst->opcode() == SpvOpPhi) {
        upper_inst = context_->get_def_use_mgr()->GetDef(
            upper_inst->GetSingleWordInOperand(0));
        // We don't handle looking through multiple phis.
        if (upper_inst->opcode() == SpvOpPhi) {
          return nullptr;
        }
      }
      SENode* upper_bound = scalar_evolution_.SimplifyExpression(
          scalar_evolution_.AnalyzeInstruction(upper_inst));
      return upper_bound;
    } break;
    case SpvOpUGreaterThan:
    case SpvOpSGreaterThan:
    case SpvOpUGreaterThanEqual:
    case SpvOpSGreaterThanEqual: {
      ir::Instruction* upper_inst = context_->get_def_use_mgr()->GetDef(
          cond_inst->GetSingleWordInOperand(0));
      if (upper_inst->opcode() == SpvOpPhi) {
        upper_inst = context_->get_def_use_mgr()->GetDef(
            upper_inst->GetSingleWordInOperand(0));
        // We don't handle looking through multiple phis.
        if (upper_inst->opcode() == SpvOpPhi) {
          return nullptr;
        }
      }
      SENode* upper_bound = scalar_evolution_.SimplifyExpression(
          scalar_evolution_.AnalyzeInstruction(upper_inst));
      return upper_bound;
      break;
    }
    default:
      return nullptr;
  }
  return nullptr;
}

std::pair<SENode*, SENode*> LoopDependenceAnalysis::GetLoopLowerUpperBounds() {
  SENode* lower_bound_SENode = GetLowerBound();
  SENode* upper_bound_SENode = GetUpperBound();

  return std::make_pair(lower_bound_SENode, upper_bound_SENode);
}

bool LoopDependenceAnalysis::IsWithinBounds(int64_t value, int64_t bound_one,
                                            int64_t bound_two) {
  if (bound_one < bound_two) {
    // If |bound_one| is the lower bound.
    return (value >= bound_one && value <= bound_two);
  } else if (bound_one > bound_two) {
    // If |bound_two| is the lower bound.
    return (value >= bound_two && value <= bound_one);
  } else {
    // Both bounds have the same value.
    return value == bound_one;
  }
}

bool LoopDependenceAnalysis::IsProvablyOutwithLoopBounds(SENode* distance) {
  SENode* lower_bound = GetLowerBound();
  SENode* upper_bound = GetUpperBound();
  if (!lower_bound || !upper_bound) {
    return false;
  }
  // We can attempt to deal with symbolic cases by subtracting |distance| and
  // the bound nodes. If we can subtract, simplify and produce a SEConstantNode
  // we
  // can produce some information.
  SENode* bounds = scalar_evolution_.SimplifyExpression(
      scalar_evolution_.CreateSubtraction(upper_bound, lower_bound));

  SEConstantNode* distance_minus_bounds =
      scalar_evolution_
          .SimplifyExpression(
              scalar_evolution_.CreateSubtraction(distance, bounds))
          ->AsSEConstantNode();
  if (distance_minus_bounds) {
    // If distance - bounds > 0 we prove the distance is outwith the loop
    // bounds.
    if (distance_minus_bounds->FoldToSingleValue() > 0) {
      return true;
    }
  }

  return false;
}

SENode* LoopDependenceAnalysis::GetTripCount() {
  ir::BasicBlock* condition_block = loop_.FindConditionBlock();
  if (!condition_block) {
    return nullptr;
  }
  ir::Instruction* induction_instr =
      loop_.FindConditionVariable(condition_block);
  if (!induction_instr) {
    return nullptr;
  }
  ir::Instruction* cond_instr = loop_.GetConditionInst();
  if (!cond_instr) {
    return nullptr;
  }

  size_t iteration_count = 0;

  // We have to check the instruction type here. If the condition instruction
  // isn't of one of the below types we can't calculate the trip count.
  switch (cond_instr->opcode()) {
    case SpvOpULessThan:
    case SpvOpSLessThan:
    case SpvOpULessThanEqual:
    case SpvOpSLessThanEqual:
    case SpvOpUGreaterThan:
    case SpvOpSGreaterThan:
    case SpvOpUGreaterThanEqual:
    case SpvOpSGreaterThanEqual:
      if (loop_.FindNumberOfIterations(
              induction_instr, &*condition_block->tail(), &iteration_count)) {
        return scalar_evolution_.CreateConstant(
            static_cast<int64_t>(iteration_count));
      }
      break;
    default:
      return nullptr;
  }

  return nullptr;
}

SENode* LoopDependenceAnalysis::GetFirstTripInductionNode() {
  ir::BasicBlock* condition_block = loop_.FindConditionBlock();
  if (!condition_block) {
    return nullptr;
  }
  ir::Instruction* induction_instr =
      loop_.FindConditionVariable(condition_block);
  if (!induction_instr) {
    return nullptr;
  }
  int64_t induction_initial_value = 0;
  if (!loop_.GetInductionInitValue(induction_instr, &induction_initial_value)) {
    return nullptr;
  }

  SENode* induction_init_SENode = scalar_evolution_.SimplifyExpression(
      scalar_evolution_.CreateConstant(induction_initial_value));
  return induction_init_SENode;
}

SENode* LoopDependenceAnalysis::GetFinalTripInductionNode() {
  ir::BasicBlock* condition_block = loop_.FindConditionBlock();
  if (!condition_block) {
    return nullptr;
  }
  ir::Instruction* induction_instr =
      loop_.FindConditionVariable(condition_block);
  if (!induction_instr) {
    return nullptr;
  }
  SENode* trip_count = GetTripCount();

  int64_t induction_initial_value = 0;
  if (!loop_.GetInductionInitValue(induction_instr, &induction_initial_value)) {
    return nullptr;
  }

  ir::Instruction* step_instr =
      loop_.GetInductionStepOperation(induction_instr);

  SENode* induction_init_SENode =
      scalar_evolution_.CreateConstant(induction_initial_value);
  SENode* step_SENode = scalar_evolution_.AnalyzeInstruction(step_instr);
  SENode* total_change_SENode =
      scalar_evolution_.CreateMultiplyNode(step_SENode, trip_count);
  SENode* final_iteration =
      scalar_evolution_.SimplifyExpression(scalar_evolution_.CreateAddNode(
          induction_init_SENode, total_change_SENode));

  return final_iteration;
}

ir::LoopDescriptor* LoopDependenceAnalysis::GetLoopDescriptor() {
  return context_->GetLoopDescriptor(loop_.GetHeaderBlock()->GetParent());
}

void LoopDependenceAnalysis::DumpIterationSpaceAsDot(std::ostream& out_stream) {
  out_stream << "digraph {\n";

  for (uint32_t id : loop_.GetBlocks()) {
    ir::BasicBlock* block = context_->cfg()->block(id);
    for (ir::Instruction& inst : *block) {
      if (inst.opcode() == SpvOp::SpvOpStore ||
          inst.opcode() == SpvOp::SpvOpLoad) {
        memory_access_to_indice_[&inst] = {};

        const ir::Instruction* access_chain =
            context_->get_def_use_mgr()->GetDef(inst.GetSingleWordInOperand(0));

        for (uint32_t i = 1u; i < access_chain->NumInOperands(); ++i) {
          const ir::Instruction* index = context_->get_def_use_mgr()->GetDef(
              access_chain->GetSingleWordInOperand(i));
          memory_access_to_indice_[&inst].push_back(
              scalar_evolution_.AnalyzeInstruction(index));
        }
      }
    }
  }

  scalar_evolution_.DumpAsDot(out_stream);
  out_stream << "}\n";
}

}  // namespace opt
}  // namespace spvtools
