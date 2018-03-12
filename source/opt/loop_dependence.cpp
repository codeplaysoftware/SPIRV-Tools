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
                                           DVEntry* dv_entry) {
  ir::Instruction* src_access_chain =
      context_->get_def_use_mgr()->GetDef(source->GetSingleWordInOperand(0));
  ir::Instruction* dest_access_chain = context_->get_def_use_mgr()->GetDef(
      destination->GetSingleWordInOperand(0));

  // If the access chains aren't collecting from the same structure there is no
  // dependence
  ir::Instruction* src_arr = context_->get_def_use_mgr()->GetDef(
      src_access_chain->GetSingleWordInOperand(0));
  ir::Instruction* dest_arr = context_->get_def_use_mgr()->GetDef(
      dest_access_chain->GetSingleWordInOperand(0));
  if (src_arr != dest_arr) {
    dv_entry->direction = DVDirections::NONE;
    return true;
  }

  // If the access chains somehow have a different number of operands they store
  // and load must be independent
  if (src_access_chain->NumOperands() != dest_access_chain->NumOperands()) {
    dv_entry->direction = DVDirections::NONE;
    return true;
  }

  // To handle multiple subscripts we must get every operand in the access
  // chains past the first
  std::vector<ir::Instruction*> src_subscripts{};
  std::vector<ir::Instruction*> dest_subscripts{};
  std::vector<DVEntry> dv_entries{};
  for (int i = 1; i < static_cast<int>(src_access_chain->NumInOperandWords());
       ++i) {
    src_subscripts.push_back(context_->get_def_use_mgr()->GetDef(
        src_access_chain->GetSingleWordInOperand(i)));
    dest_subscripts.push_back(context_->get_def_use_mgr()->GetDef(
        dest_access_chain->GetSingleWordInOperand(i)));
    dv_entries.push_back(DVEntry{});
  }

  // Go through each subscript testing for independence.
  // If any subscript results in independence, we prove independence between the
  // load and store.
  // If we can't prove independence we store what information we can gather in
  // a DVEntry
  for (size_t subscript = 0; subscript < src_subscripts.size(); ++subscript) {
    SENode* src_node = scalar_evolution_.SimplifyExpression(
        scalar_evolution_.AnalyzeInstruction(src_subscripts[subscript]));
    SENode* dest_node = scalar_evolution_.SimplifyExpression(
        scalar_evolution_.AnalyzeInstruction(dest_subscripts[subscript]));

    // If either node is simplified to a CanNotCompute we can't perform any
    // analysis so must assume <=> dependence and return
    if (src_node->GetType() == SENode::CanNotCompute ||
        dest_node->GetType() == SENode::CanNotCompute) {
      dv_entry->direction = DVDirections::ALL;
      break;
    }

    // Neither node is a recurrent expr so we use a ZIV test
    if (src_node->GetType() != SENode::RecurrentExpr &&
        dest_node->GetType() != SENode::RecurrentExpr) {
      if (ZIVTest(src_node, dest_node, &dv_entries[subscript])) {
        dv_entry->direction = DVDirections::NONE;
        return true;
      }
    }

    // src is not a recurrent expr but dest is, so we can try a WeakZeroSrcTest
    if (src_node->GetType() != SENode::RecurrentExpr &&
        dest_node->GetType() == SENode::RecurrentExpr) {
      if (WeakZeroSourceSIVTest(
              src_node, dest_node->AsSERecurrentNode(),
              dest_node->AsSERecurrentNode()->GetCoefficient(),
              &dv_entries[subscript])) {
        dv_entry->direction = DVDirections::NONE;
        return true;
      }
    }

    // src is a recurrent expr but dest is not, so we can try a WeakZeroDestTest
    if (src_node->GetType() == SENode::RecurrentExpr &&
        dest_node->GetType() != SENode::RecurrentExpr) {
      if (WeakZeroDestinationSIVTest(
              src_node->AsSERecurrentNode(), dest_node,
              src_node->AsSERecurrentNode()->GetCoefficient(),
              &dv_entries[subscript])) {
        dv_entry->direction = DVDirections::NONE;
        return true;
      }
    }

    // Both src and dest are recurrent exprs. We should narrow down to StrongSIV
    // or WeakCrossingSIV tests.
    if (src_node->GetType() == SENode::RecurrentExpr &&
        dest_node->GetType() == SENode::RecurrentExpr) {
      SERecurrentNode* src_rec = src_node->AsSERecurrentNode();
      SERecurrentNode* dest_rec = dest_node->AsSERecurrentNode();

      // If the coefficients are identical we can use StrongSIV
      if (src_rec->GetCoefficient() == dest_rec->GetCoefficient()) {
        if (StrongSIVTest(src_rec->AsSERecurrentNode(),
                          dest_rec->AsSERecurrentNode(),
                          src_rec->AsSERecurrentNode()->GetCoefficient(),
                          &dv_entries[subscript])) {
          dv_entry->direction = DVDirections::NONE;
          return true;
        }
      }

      // If the coefficients are opposite (coeff_1 == -coeff_2) we can use a
      // WeakCrossingSIV test.
      if (src_rec->GetCoefficient() ==
          scalar_evolution_.CreateNegation(dest_rec->GetCoefficient())) {
        if (WeakCrossingSIVTest(src_rec->AsSERecurrentNode(),
                                dest_rec->AsSERecurrentNode(),
                                src_rec->AsSERecurrentNode()->GetCoefficient(),
                                &dv_entries[subscript])) {
          dv_entry->direction = DVDirections::NONE;
          return true;
        }
      }
    }
  }

  // We were unable to prove independence so must gather all of the direction
  // information we found

  dv_entry->direction = DVDirections::NONE;
  for (size_t subscript = 0; subscript < dv_entries.size(); ++subscript) {
    dv_entry->direction = static_cast<DVDirections>(
        dv_entry->direction | dv_entries[subscript].direction);
    if (dv_entries[subscript].peel_first) {
      dv_entry->peel_first = true;
    }
    if (dv_entries[subscript].peel_last) {
      dv_entry->peel_last = true;
    }
  }

  return false;
}

bool LoopDependenceAnalysis::ZIVTest(SENode* source, SENode* destination,
                                     DVEntry* dv_entry) {
  // If source == destination, dependence with direction = and distance 0
  if (source == destination) {
    dv_entry->direction = DVDirections::EQ;
    dv_entry->distance = 0;
    return false;
  } else {
    // Otherwise we prove independence
    dv_entry->direction = DVDirections::NONE;
    return true;
  }
}

// Takes the form a*i + c1, a*i + c2
// When c1 and c2 are loop invariant and a is constant
// distance = (c1 - c2)/a
//              < if distance > 0
// direction =  = if distance = 0
//              > if distance < 0
bool LoopDependenceAnalysis::StrongSIVTest(SERecurrentNode* source,
                                           SERecurrentNode* destination,
                                           SENode* coefficient,
                                           DVEntry* dv_entry) {
  // Build an SENode for distance
  SENode* src_offset = source->GetOffset();
  SENode* dest_offset = destination->GetOffset();
  SENode* offset_delta = scalar_evolution_.SimplifyExpression(
      scalar_evolution_.CreateSubtraction(src_offset, dest_offset));

  // Scalar evolution doesn't perform division, so we must fold to constants and
  // do it manually.
  // We must check the offset delta and coefficient are constants
  int64_t distance = 0;
  SEConstantNode* delta_cnst = offset_delta->AsSEConstantNode();
  SEConstantNode* coefficient_cnst = coefficient->AsSEConstantNode();
  if (delta_cnst && coefficient_cnst) {
    int64_t delta_val = delta_cnst->FoldToSingleValue();
    int64_t coeff_val = coefficient_cnst->FoldToSingleValue();
    // Check if the distance is not integral to try to prove independence
    if (delta_val % coeff_val != 0) {
      dv_entry->direction = DVDirections::NONE;
      return true;
    } else {
      distance = delta_val / coeff_val;
    }
  } else {
    // If we can't fold delta and coefficient to single values we can't produce
    // distance.
    // As a result we can't perform the rest of the pass and must assume
    // dependence in all directions
    dv_entry->distance = DVDirections::ALL;
    return false;
  }

  // Next we gather the upper and lower bounds as constants if possible. If
  // distance > upper_bound - lower_bound we prove independence
  SEConstantNode* lower_bound = GetLowerBound();
  SEConstantNode* upper_bound = GetUpperBound();
  if (lower_bound && upper_bound) {
    SENode* bounds = scalar_evolution_.SimplifyExpression(
        scalar_evolution_.CreateSubtraction(upper_bound, lower_bound));

    if (bounds->GetType() == SENode::SENodeType::Constant) {
      int64_t bounds_val = bounds->AsSEConstantNode()->FoldToSingleValue();

      // If the absolute value of the distance is > upper bound - lower bound
      // then
      // we prove independence
      if (llabs(distance) > bounds_val) {
        dv_entry->direction = DVDirections::NONE;
        dv_entry->distance = distance;
        return true;
      }
    }
  }

  // Otherwise we can get a direction as follows
  //             { < if distance > 0
  // direction = { = if distance == 0
  //             { > if distance < 0

  if (distance > 0) {
    dv_entry->direction = DVDirections::LT;
    dv_entry->distance = distance;

    return false;
  }
  if (distance == 0) {
    dv_entry->direction = DVDirections::EQ;
    dv_entry->distance = 0;
    return false;
  }
  if (distance < 0) {
    dv_entry->direction = DVDirections::GT;
    dv_entry->distance = distance;
    return false;
  }

  // We were unable to prove independence or discern any additional information
  // Must assume <=> direction
  dv_entry->direction = DVDirections::ALL;
  return false;
}

// Takes the form c1, a2*i + c2
// distance = (c1 - c2) / a2
bool LoopDependenceAnalysis::WeakZeroSourceSIVTest(SENode* source,
                                                   SERecurrentNode* destination,
                                                   SENode* coefficient,
                                                   DVEntry* dv_entry) {
  // Build an SENode for distance
  SENode* dest_offset = destination->GetOffset();
  SENode* delta = scalar_evolution_.SimplifyExpression(
      scalar_evolution_.CreateSubtraction(source, dest_offset));

  // Scalar evolution doesn't perform division, so we must fold to constants and
  // do it manually.
  int64_t distance = 0;
  SEConstantNode* delta_cnst = delta->AsSEConstantNode();
  SEConstantNode* coefficient_cnst = coefficient->AsSEConstantNode();
  if (delta_cnst && coefficient_cnst) {
    int64_t delta_val = delta_cnst->FoldToSingleValue();
    int64_t coeff_val = coefficient_cnst->FoldToSingleValue();
    // Check if the distance is not integral
    if (delta_val % coeff_val != 0) {
      dv_entry->direction = DVDirections::NONE;
      return true;
    } else {
      distance = delta_val / coeff_val;
    }
  }

  // If we can prove the distance is outside the bounds we prove independence
  SEConstantNode* lower_bound = GetLowerBound();
  SEConstantNode* upper_bound = GetUpperBound();
  if (lower_bound && upper_bound) {
    int64_t lower_bound_val = lower_bound->FoldToSingleValue();
    int64_t upper_bound_val = upper_bound->FoldToSingleValue();
    if (!IsWithinBounds(distance, lower_bound_val, upper_bound_val)) {
      dv_entry->direction = DVDirections::NONE;
      dv_entry->distance = distance;
      return true;
    }
  }

  // Now we want to see if we can detect to peel the first or last iterations

  // If source == FirstTripInduction * dest_coeff + dest_offset
  // peel first

  // We build the value of the first trip value as an SENode
  SENode* induction_first_trip_SENode = GetFirstTripInductionNode();
  SENode* induction_first_trip_mult_coeff_SENode =
      scalar_evolution_.CreateMultiplyNode(induction_first_trip_SENode,
                                           coefficient);
  SENode* first_trip_SENode =
      scalar_evolution_
          .SimplifyExpression(scalar_evolution_.CreateAddNode(
              induction_first_trip_mult_coeff_SENode, dest_offset))
          ->AsSEConstantNode();

  // If source == FirstTripValue, peel_first
  if (first_trip_SENode != nullptr) {
    if (source == first_trip_SENode) {
      // We have found that peeling the first iteration will break dependency
      dv_entry->peel_first = true;
      return false;
    }
  }

  // If source == LastTripInduction * dest_coeff + dest_offset
  // peel last

  // We build the value of the final trip value as an SENode
  SENode* induction_final_trip_SENode = GetFinalTripInductionNode();
  SENode* induction_final_trip_mult_coeff_SENode =
      scalar_evolution_.CreateMultiplyNode(induction_final_trip_SENode,
                                           coefficient);
  SENode* final_trip_SENode =
      scalar_evolution_
          .SimplifyExpression(scalar_evolution_.CreateAddNode(
              induction_final_trip_mult_coeff_SENode, dest_offset))
          ->AsSEConstantNode();

  // If source == LastTripValue, peel_last
  if (final_trip_SENode != nullptr) {
    if (source == final_trip_SENode) {
      // We have found that peeling the last iteration will break dependency
      dv_entry->peel_last = true;
      return false;
    }
  }

  // We were unable to prove independence or discern any additional information
  // Must assume <=> direction
  dv_entry->direction = DVDirections::ALL;
  return false;
}

// Takes the form a1*i + c1, c2
// distance = (c2 - c1) / a1
bool LoopDependenceAnalysis::WeakZeroDestinationSIVTest(SERecurrentNode* source,
                                                        SENode* destination,
                                                        SENode* coefficient,
                                                        DVEntry* dv_entry) {
  // Build an SENode for distance
  SENode* src_offset = source->GetOffset();
  SENode* delta = scalar_evolution_.SimplifyExpression(
      scalar_evolution_.CreateSubtraction(destination, src_offset));

  // Scalar evolution doesn't perform division, so we must fold to constants and
  // do it manually.
  int64_t distance = 0;
  SEConstantNode* delta_cnst = delta->AsSEConstantNode();
  SEConstantNode* coefficient_cnst = coefficient->AsSEConstantNode();
  if (delta_cnst && coefficient_cnst) {
    int64_t delta_val = delta_cnst->FoldToSingleValue();
    int64_t coeff_val = coefficient_cnst->FoldToSingleValue();
    // Check if the distance is not integral
    if (delta_val % coeff_val != 0) {
      dv_entry->direction = DVDirections::NONE;
      return true;
    } else {
      distance = delta_val / coeff_val;
    }
  }

  // If we can prove the distance is outside the bounds we prove independence
  SEConstantNode* lower_bound = GetLowerBound();
  SEConstantNode* upper_bound = GetUpperBound();
  if (lower_bound && upper_bound) {
    int64_t lower_bound_val = lower_bound->FoldToSingleValue();
    int64_t upper_bound_val = upper_bound->FoldToSingleValue();
    if (!IsWithinBounds(distance, lower_bound_val, upper_bound_val)) {
      dv_entry->direction = DVDirections::NONE;
      dv_entry->distance = distance;
      return true;
    }
  }

  // Now we want to see if we can detect to peel the first or last iterations

  // If destination == FirstTripInduction * src_coeff + src_offset
  // peel first

  // We build the value of the first trip as an SENode
  SENode* induction_first_trip_SENode = GetFirstTripInductionNode();
  SENode* induction_first_trip_mult_coeff_SENode =
      scalar_evolution_.CreateMultiplyNode(induction_first_trip_SENode,
                                           coefficient);
  SENode* first_trip_SENode =
      scalar_evolution_.SimplifyExpression(scalar_evolution_.CreateAddNode(
          induction_first_trip_mult_coeff_SENode, src_offset));

  // If destination == FirstTripValue, peel_first
  if (first_trip_SENode != nullptr) {
    if (destination == first_trip_SENode) {
      // We have found that peeling the first iteration will break dependency
      dv_entry->peel_first = true;
      return false;
    }
  }

  // If destination == LastTripInduction * src_coeff + src_offset
  // peel last

  // We build the value of the final trip as an SENode
  SENode* induction_final_trip_SENode = GetFinalTripInductionNode();
  SENode* induction_final_trip_mult_coeff_SENode =
      scalar_evolution_.CreateMultiplyNode(induction_final_trip_SENode,
                                           coefficient);
  SENode* final_trip_SENode =
      scalar_evolution_.SimplifyExpression(scalar_evolution_.CreateAddNode(
          induction_final_trip_mult_coeff_SENode, src_offset));

  // If destination == LastTripValue, peel_last
  if (final_trip_SENode != nullptr) {
    if (destination == final_trip_SENode) {
      // We have found that peeling the last iteration will break dependency
      dv_entry->peel_last = true;
      return false;
    }
  }

  // We were unable to prove independence or discern any additional information
  // Must assume <=> direction
  dv_entry->direction = DVDirections::ALL;
  return false;
}

// Takes the form a1*i + c1, a2*i + c2
// When a1 = -a2
// distance = (c2 - c1) / 2*a1
bool LoopDependenceAnalysis::WeakCrossingSIVTest(SERecurrentNode* source,
                                                 SERecurrentNode* destination,
                                                 SENode* coefficient,
                                                 DVEntry* dv_entry) {
  // Build an SENode for distance
  SENode* offset_delta =
      scalar_evolution_.SimplifyExpression(scalar_evolution_.CreateSubtraction(
          destination->GetOffset(), source->GetOffset()));

  // Scalar evolution doesn't perform division, so we must fold to constants and
  // do it manually.
  int64_t distance = 0;
  SEConstantNode* delta_cnst = offset_delta->AsSEConstantNode();
  SEConstantNode* coefficient_cnst = coefficient->AsSEConstantNode();
  if (delta_cnst && coefficient_cnst) {
    int64_t delta_val = delta_cnst->FoldToSingleValue();
    int64_t coeff_val = coefficient_cnst->FoldToSingleValue();
    // Check if the distance is not integral or if it has a non-integral part
    // equal to 1/2
    if (delta_val % (2 * coeff_val) != 0 ||
        (delta_val % (2 * coeff_val)) / (2 * coeff_val) != 0.5) {
      dv_entry->direction = DVDirections::NONE;
      return true;
    } else {
      distance = delta_val / (2 * coeff_val);
    }

    if (distance == 0) {
      dv_entry->direction = DVDirections::EQ;
      dv_entry->distance = 0;
      return false;
    }
  }

  // We were unable to prove independence or discern any additional information
  // Must assume <=> direction
  dv_entry->direction = DVDirections::ALL;
  return false;
}

// Takes the form a1*i + c1, a2*i + c2
// Where a1 and a2 are constant and different
// bool LoopDependenceAnalysis::WeakSIVTest(SENode* source, SENode* destination,
//                                         SENode* src_coeff, SENode*
//                                         dest_coeff,
//                                         DVEntry* dv_entry) {
//  return false;
//}

bool LoopDependenceAnalysis::IsWithinBounds(int64_t value, int64_t bound_one,
                                            int64_t bound_two) {
  if (bound_one < bound_two) {
    // If |bound_one| is the lower bound
    return (value >= bound_one && value <= bound_two);
  } else if (bound_one > bound_two) {
    // If |bound_two| is the lower bound
    return (value >= bound_two && value <= bound_one);
  } else {
    // Both bounds have the same value
    return value == bound_one;
  }
}

SEConstantNode* LoopDependenceAnalysis::GetLowerBound() {
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
        // We don't handle looking through multiple phis
        if (lower_inst->opcode() == SpvOpPhi) {
          return nullptr;
        }
      }
      SEConstantNode* lower_bound =
          scalar_evolution_
              .SimplifyExpression(
                  scalar_evolution_.AnalyzeInstruction(lower_inst))
              ->AsSEConstantNode();
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
        // We don't handle looking through multiple phis
        if (lower_inst->opcode() == SpvOpPhi) {
          return nullptr;
        }
      }
      SEConstantNode* lower_bound =
          scalar_evolution_
              .SimplifyExpression(
                  scalar_evolution_.AnalyzeInstruction(lower_inst))
              ->AsSEConstantNode();
      if (lower_bound) {
        return scalar_evolution_
            .SimplifyExpression(scalar_evolution_.CreateAddNode(
                lower_bound, scalar_evolution_.CreateConstant(1)))
            ->AsSEConstantNode();
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
        // We don't handle looking through multiple phis
        if (lower_inst->opcode() == SpvOpPhi) {
          return nullptr;
        }
      }
      SEConstantNode* lower_bound =
          scalar_evolution_
              .SimplifyExpression(
                  scalar_evolution_.AnalyzeInstruction(lower_inst))
              ->AsSEConstantNode();
      return lower_bound;
    } break;
    default:
      return nullptr;
  }
  return nullptr;
}

SEConstantNode* LoopDependenceAnalysis::GetUpperBound() {
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
        // We don't handle looking through multiple phis
        if (upper_inst->opcode() == SpvOpPhi) {
          return nullptr;
        }
      }
      SEConstantNode* upper_bound =
          scalar_evolution_
              .SimplifyExpression(
                  scalar_evolution_.AnalyzeInstruction(upper_inst))
              ->AsSEConstantNode();
      if (upper_bound) {
        return scalar_evolution_
            .SimplifyExpression(scalar_evolution_.CreateSubtraction(
                upper_bound, scalar_evolution_.CreateConstant(1)))
            ->AsSEConstantNode();
      }
    }
    case SpvOpULessThanEqual:
    case SpvOpSLessThanEqual: {
      ir::Instruction* upper_inst = context_->get_def_use_mgr()->GetDef(
          cond_inst->GetSingleWordInOperand(1));
      if (upper_inst->opcode() == SpvOpPhi) {
        upper_inst = context_->get_def_use_mgr()->GetDef(
            upper_inst->GetSingleWordInOperand(0));
        // We don't handle looking through multiple phis
        if (upper_inst->opcode() == SpvOpPhi) {
          return nullptr;
        }
      }
      SEConstantNode* upper_bound =
          scalar_evolution_
              .SimplifyExpression(
                  scalar_evolution_.AnalyzeInstruction(upper_inst))
              ->AsSEConstantNode();
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
        // We don't handle looking through multiple phis
        if (upper_inst->opcode() == SpvOpPhi) {
          return nullptr;
        }
      }
      SEConstantNode* upper_bound =
          scalar_evolution_
              .SimplifyExpression(
                  scalar_evolution_.AnalyzeInstruction(upper_inst))
              ->AsSEConstantNode();
      return upper_bound;
      break;
    }
    default:
      return nullptr;
  }
  return nullptr;
}

/*
SEConstantNode* LoopDependenceAnalysis::GetLowerBound() {
  ir::Instruction* lower_bound_inst = loop_.GetLowerBoundInst();
  ir::Instruction* upper_bound_inst = loop_.GetUpperBoundInst();
  if (!lower_bound_inst || !upper_bound_inst) {
    return nullptr;
  }

  SEConstantNode* lower_SENode =
      scalar_evolution_
          .SimplifyExpression(
              scalar_evolution_.AnalyzeInstruction(lower_bound_inst))
          ->AsSEConstantNode();
  SEConstantNode* upper_SENode =
      scalar_evolution_
          .SimplifyExpression(
              scalar_evolution_.AnalyzeInstruction(upper_bound_inst))
          ->AsSEConstantNode();
  if (!lower_SENode || !upper_SENode) {
    return nullptr;
  }
  int64_t lower_val = lower_SENode->FoldToSingleValue();
  int64_t upper_val = lower_SENode->FoldToSingleValue();
  if (lower_val < upper_val) {
    return lower_SENode;
  }
  if (lower_val == upper_val) {
    return lower_SENode;
  }
  if (lower_val > upper_val) {
    return upper_SENode;
  }
  // We couldn't determine which bound instr was lower and can't determine they
  // are equal. As a result it is not safe to return either bound
  return nullptr;
}

SEConstantNode* LoopDependenceAnalysis::GetUpperBound() {
  ir::Instruction* lower_bound_inst = loop_.GetLowerBoundInst();
  ir::Instruction* upper_bound_inst = loop_.GetUpperBoundInst();
  if (!lower_bound_inst || !upper_bound_inst) {
    return nullptr;
  }

  SEConstantNode* lower_SENode =
      scalar_evolution_
          .SimplifyExpression(
              scalar_evolution_.AnalyzeInstruction(lower_bound_inst))
          ->AsSEConstantNode();
  SEConstantNode* upper_SENode =
      scalar_evolution_
          .SimplifyExpression(
              scalar_evolution_.AnalyzeInstruction(upper_bound_inst))
          ->AsSEConstantNode();
  if (!lower_SENode || !upper_SENode) {
    return nullptr;
  }
  int64_t lower_val = lower_SENode->FoldToSingleValue();
  int64_t upper_val = lower_SENode->FoldToSingleValue();
  if (lower_val < upper_val) {
    return upper_SENode;
  }
  if (lower_val == upper_val) {
    return lower_SENode;
  }
  if (lower_val > upper_val) {
    return lower_SENode;
  }
  // We couldn't determine which bound instr was higher and can't determine they
  // are equal. As a result it is not safe to return either bound
  return nullptr;
}

*/

std::pair<SEConstantNode*, SEConstantNode*>
LoopDependenceAnalysis::GetLoopLowerUpperBounds() {
  SEConstantNode* lower_bound_SENode = GetLowerBound();
  SEConstantNode* upper_bound_SENode = GetUpperBound();

  return std::make_pair(lower_bound_SENode, upper_bound_SENode);
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
