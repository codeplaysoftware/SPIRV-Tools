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
    int64_t induction_variable_count =
        CountInductionVariables(source_node, destination_node);

    // If either node is simplified to a CanNotCompute we can't perform any
    // analysis so must assume <=> dependence and return.
    if (source_node->GetType() == SENode::CanNotCompute ||
        destination_node->GetType() == SENode::CanNotCompute) {
      distance_vector->direction = DistanceVector::Directions::ALL;
      break;
    }

    // We have no induction variables so can apply a ZIV test.
    if (induction_variable_count == 0) {
      if (ZIVTest(source_node, destination_node,
                  &distance_vector_entries[subscript])) {
        distance_vector->direction = DistanceVector::Directions::NONE;
        return true;
      }
    }

    // We have only one induction variable so should attempt an SIV test.
    if (induction_variable_count == 1) {
      int64_t source_induction_count = CountInductionVariables(source_node);
      int64_t destination_induction_count =
          CountInductionVariables(destination_node);

      // If the source node has no induction variables we can apply a
      // WeakZeroSrcTest.
      if (source_induction_count == 0) {
        if (WeakZeroSourceSIVTest(
                source_node, destination_node->AsSERecurrentNode(),
                destination_node->AsSERecurrentNode()->GetCoefficient(),
                &distance_vector_entries[subscript])) {
          distance_vector->direction = DistanceVector::Directions::NONE;
          return true;
        }
      }

      // If the destination has no induction variables we can apply a
      // WeakZeroDestTest.
      if (destination_induction_count == 0) {
        if (WeakZeroDestinationSIVTest(
                source_node->AsSERecurrentNode(), destination_node,
                source_node->AsSERecurrentNode()->GetCoefficient(),
                &distance_vector_entries[subscript])) {
          distance_vector->direction = DistanceVector::Directions::NONE;
          return true;
        }
      }

      // We now need to collect the SERecurrentExpr nodes from source and
      // destination. We do not handle cases where source or destination have
      // multiple SERecurrentExpr nodes.
      std::vector<SERecurrentNode*> source_recurrent_nodes =
          source_node->CollectRecurrentNodes();
      std::vector<SERecurrentNode*> destination_recurrent_nodes =
          destination_node->CollectRecurrentNodes();
      if (source_recurrent_nodes.size() == 1 &&
          destination_recurrent_nodes.size() == 1) {
        SERecurrentNode* source_recurrent_expr =
            *source_recurrent_nodes.begin();
        SERecurrentNode* destination_recurrent_expr =
            *destination_recurrent_nodes.begin();

        // If the coefficients are identical we can apply a StrongSIVTest.
        if (source_recurrent_expr->GetCoefficient() ==
            destination_recurrent_expr->GetCoefficient()) {
          if (StrongSIVTest(source_node, destination_node,
                            source_recurrent_expr->GetCoefficient(),
                            &distance_vector_entries[subscript])) {
            distance_vector->direction = DistanceVector::Directions::NONE;
            return true;
          }
        }

        // If the coefficients are of equal magnitude and opposite sign we can
        // apply a WeakCrossingSIVTest.
        if (source_recurrent_expr->GetCoefficient() ==
            scalar_evolution_.CreateNegation(
                destination_recurrent_expr->GetCoefficient())) {
          if (WeakCrossingSIVTest(source_node, destination_node,
                                  source_recurrent_expr->GetCoefficient(),
                                  &distance_vector_entries[subscript])) {
            distance_vector->direction = DistanceVector::Directions::NONE;
            return true;
          }
        }
      }
    }

    // We have multiple induction variables so should attempt an MIV test.
    if (induction_variable_count > 1) {
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

bool LoopDependenceAnalysis::StrongSIVTest(SENode* source, SENode* destination,
                                           SENode* coefficient,
                                           DistanceVector* distance_vector) {
  // If both source and destination are SERecurrentNodes we can perform tests
  // based on distance.
  // If either source or destination contain value unknown nodes or if one or
  // both are not SERecurrentNodes we must attempt a symbolic test.
  std::vector<SEValueUnknown*> source_value_unknown_nodes =
      source->CollectValueUnknownNodes();
  std::vector<SEValueUnknown*> destination_value_unknown_nodes =
      destination->CollectValueUnknownNodes();
  if (source_value_unknown_nodes.size() > 0 ||
      destination_value_unknown_nodes.size() > 0 ||
      !source->AsSERecurrentNode() || !destination->AsSERecurrentNode()) {
    return SymbolicStrongSIVTest(source, destination, distance_vector);
  }

  // Build an SENode for distance.
  SENode* source_offset = source->AsSERecurrentNode()->GetOffset();
  SENode* destination_offset = destination->AsSERecurrentNode()->GetOffset();
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
  if (first_trip_SENode) {
    if (source == first_trip_SENode) {
      // We have found that peeling the first iteration will break dependency.
      distance_vector->peel_first = true;
      return false;
    }
  }

  // We get the LastTripValue as LastTripInduction * destination_coeff +
  // destination_offset.
  // We build the value of the final trip as an SENode.
  SENode* induction_final_trip_SENode = GetFinalTripInductionNode(coefficient);
  SENode* induction_final_trip_mult_coefficient_SENode =
      scalar_evolution_.CreateMultiplyNode(induction_final_trip_SENode,
                                           coefficient);
  SENode* final_trip_SENode =
      scalar_evolution_
          .SimplifyExpression(scalar_evolution_.CreateAddNode(
              induction_final_trip_mult_coefficient_SENode, destination_offset))
          ->AsSEConstantNode();

  // If source == LastTripValue, peel_last.
  if (final_trip_SENode) {
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
  if (first_trip_SENode) {
    if (destination == first_trip_SENode) {
      // We have found that peeling the first iteration will break dependency.
      distance_vector->peel_first = true;
      return false;
    }
  }

  // We get the LastTripValue as LastTripInduction * source_coeff +
  // source_offset.
  // We build the value of the final trip as an SENode.
  SENode* induction_final_trip_SENode = GetFinalTripInductionNode(coefficient);
  SENode* induction_final_trip_mult_coefficient_SENode =
      scalar_evolution_.CreateMultiplyNode(induction_final_trip_SENode,
                                           coefficient);
  SENode* final_trip_SENode =
      scalar_evolution_.SimplifyExpression(scalar_evolution_.CreateAddNode(
          induction_final_trip_mult_coefficient_SENode, source_offset));

  // If destination == LastTripValue, peel_last.
  if (final_trip_SENode) {
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
    SENode* source, SENode* destination, SENode* coefficient,
    DistanceVector* distance_vector) {
  // We currently can't handle symbolic WeakCrossingSIVTests. If either source
  // or destination are not SERecurrentNodes we must exit.
  if (!source->AsSERecurrentNode() || !destination->AsSERecurrentNode()) {
    distance_vector->direction = DistanceVector::Directions::ALL;
    return false;
  }

  // Build an SENode for distance.
  SENode* offset_delta =
      scalar_evolution_.SimplifyExpression(scalar_evolution_.CreateSubtraction(
          destination->AsSERecurrentNode()->GetOffset(),
          source->AsSERecurrentNode()->GetOffset()));

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

}  // namespace opt
}  // namespace spvtools
