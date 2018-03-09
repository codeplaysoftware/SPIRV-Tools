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
  SENode* source_node = memory_access_to_indice_[source][0];
  SENode* destination_node = memory_access_to_indice_[destination][0];

  // TODO(Alexander): Check source and destination are loading and storing from
  // the same variables. If not, there is no dependence

  SENode* src_coeff = source_node->GetCoefficient();
  SENode* dest_coeff = destination_node->GetCoefficient();

  // If the subscripts have no coefficients, preform a ZIV test
  if (!src_coeff && !dest_coeff) {
    if (ZIVTest(source_node, destination_node, dv_entry)) return true;
  }

  // If the subscript takes the form [c1] = [a*i + c2] use weak zero source SIV
  if (!src_coeff && dest_coeff) {
    if (WeakZeroSourceSIVTest(source_node, destination_node, dest_coeff,
                              dv_entry))
      return true;
  }

  // If the subscript takes the form [a*i + c1] = [c2] use weak zero dest SIV
  if (src_coeff && !dest_coeff) {
    if (WeakZeroDestinationSIVTest(source_node, destination_node, src_coeff,
                                   dv_entry))
      return true;
  }

  // If the subscript takes the form [a*i + c1] = [a*i + c2] use strong SIV
  if (src_coeff && dest_coeff && src_coeff->IsEqual(dest_coeff)) {
    if (StrongSIVTest(source_node, destination_node, src_coeff, dv_entry))
      return true;
  }

  // If the subscript takes the form [a1*i + c1] = [a2*i + c2] where a1 = -a2
  // use weak crossing SIV
  if (src_coeff && dest_coeff &&
      src_coeff->IsEqual(scalar_evolution_.CreateNegation(dest_coeff))) {
    if (WeakCrossingSIVTest(source_node, destination_node, src_coeff, dv_entry))
      return true;
  }

  // If the subscript takes the form [a1*i + c1] = [a2*i + c2] use weak SIV
  // if (src_coeff && dest_coeff && !src_coeff->IsEqual(dest_coeff)) {
  //  if (WeakSIVTest(source_node, destination_node, src_coeff, dest_coeff,
  //                  dv_entry))
  //    return true;
  //}
  return false;
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

bool LoopDependenceAnalysis::ZIVTest(SENode* source, SENode* destination,
                                     DVEntry* dv_entry) {
  // If source == destination, dependence with direction = and distance 0
  if (source->IsEqual(destination)) {
    dv_entry->direction = DVEntry::EQ;
    dv_entry->distance = 0;
  } else {
    // Otherwise we prove independence
    dv_entry->direction = DVEntry::NONE;
    return true;
  }

  // We were unable to prove independence or discern any additional information
  // Must assume <=> direction
  dv_entry->direction = DVEntry::ALL;
  return false;
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
  SENode* src_const = source->GetOffset();
  SENode* dest_const = destination->GetOffset();
  SENode* delta = scalar_evolution_.CreateSubtraction(src_const, dest_const);

  // Scalar evolution doesn't perform division, so we must fold to constants and
  // do it manually.
  int64_t delta_val = 0;
  int64_t coeff_val = 0;
  int64_t distance = 0;
  if (delta->CanFoldToConstant() && coefficient->CanFoldToConstant()) {
    delta_val = delta->FoldToSingleValue();
    coeff_val = delta->FoldToSingleValue();
    // Check if the distance is not integral to try to prove independence
    if (delta_val % coeff_val != 0) {
      dv_entry->direction = DVEntry::NONE;
      return true;
    } else {
      distance = delta_val / coeff_val;
    }
  } else {
    // If we can't fold delta and coefficient to single values we can't produce
    // distance.
    // As a result we can't perform the rest of the pass and must assume
    // dependence in all directions
    dv_entry->distance = DVEntry::ALL;
    return false;
  }

  SENode* lower_bound = GetLowerBound();
  SENode* upper_bound = GetUpperBound();
  SENode* bounds =
      scalar_evolution_.CreateSubtraction(upper_bound, lower_bound);

  if (bounds->CanFoldToConstant()) {
    int64_t bounds_val = bounds->FoldToSingleValue();

    // If the absolute value of the distance is > upper bound - lower bound then
    // we prove independence
    if (distance > bounds_val) {
      dv_entry->direction = DVEntry::NONE;
      dv_entry->distance = distance;
      return true;
    }
  }

  // Otherwise we can get a direction as follows
  //             { < if distance > 0
  // direction = { = if distance == 0
  //             { > if distance < 0

  if (distance > 0) {
    dv_entry->direction = DVEntry::LT;
    dv_entry->distance = distance;

    return false;
  }
  if (distance == 0) {
    dv_entry->direction = DVEntry::EQ;
    dv_entry->distance = 0;
    return false;
  }
  if (distance < 0) {
    dv_entry->direction = DVEntry::GT;
    dv_entry->distance = distance;

    return false;
  }

  // We were unable to prove independence or discern any additional information
  // Must assume <=> direction
  dv_entry->direction = DVEntry::ALL;
  return false;
}

// Takes the form a1*i + c1, a2*i + c2
// when a1 = 0
// distance = (c1 - c2) / a2
bool LoopDependenceAnalysis::WeakZeroSourceSIVTest(SENode* source,
                                                   SERecurrentNode* destination,
                                                   SENode* coefficient,
                                                   DVEntry* dv_entry) {
  // Build an SENode for distance
  SENode* src_const = source->GetOffset();
  SENode* dest_const = destination->GetOffset();
  SENode* delta = scalar_evolution_.CreateSubtraction(src_const, dest_const);

  // Scalar evolution doesn't perform division, so we must fold to constants and
  // do it manually.
  int64_t delta_val = 0;
  int64_t coeff_val = 0;
  int64_t distance = 0;
  if (delta->CanFoldToConstant() && coefficient->CanFoldToConstant()) {
    delta_val = delta->FoldToSingleValue();
    coeff_val = delta->FoldToSingleValue();
    // Check if the distance is not integral or if it has a non-integral part
    // equal to 1/2
    if (delta_val % coeff_val != 0) {
      dv_entry->direction = DVEntry::NONE;
      return true;
    } else {
      distance = delta_val / coeff_val;
    }
  }

  int64_t lower_bound_val = 0;
  int64_t upper_bound_val = 0;
  SENode* lower_bound = GetLowerBound();
  SENode* upper_bound = GetUpperBound();
  // If we can prove the distance is outside the bounds we prove independence
  if (lower_bound->CanFoldToConstant() && upper_bound->CanFoldToConstant()) {
    lower_bound_val = lower_bound->FoldToSingleValue();
    upper_bound_val = upper_bound->FoldToSingleValue();
    if (!IsWithinBounds(distance, lower_bound_val, upper_bound_val)) {
      dv_entry->direction = DVEntry::NONE;
      dv_entry->distance = distance;
      return true;
    }
  }

  // Now we want to see if we can detect to peel the first or last iterations

  // We build the value of the first trip value as an SENode and fold it down to
  // a constant value if possible.
  SENode* induction_first_trip_value_SENode = GetFirstTripInductionNode();
  SENode* induction_first_trip_mult_coeff_SENode =
      scalar_evolution_.CreateMultiplyNode(induction_first_trip_value_SENode,
                                           coefficient_);
  SENode* induction_first_trip_SENode = scalar_evolution_.CreateAddNode(
      induction_first_trip_mult_coeff_SENode, src_const);

  // If src_const == FirstTripValue, peel_first
  if (induction_first_trip_SENode->CanFoldToConstant()) {
    int64_t first_trip_value = induction_first_trip_SENode->FoldToSingleValue();
    if (distance == first_trip_value) {
      // We have found that peeling the first iteration will break dependency
      dv_entry->peel_first = true;
      return false;
    }
  }

  // We build the value of the final trip value as an SENode and fold it down to
  // a constant value if possible.
  SENode* induction_final_trip_value_SENode = GetFinalTripInductionNode();
  SENode* induction_final_trip_mult_coeff_SENode =
      scalar_evolution_.CreateMultiplyNode(induction_final_trip_value_SENode,
                                           coefficient_);
  SENode* induction_final_trip_SENode = scalar_evolution_.CreateAddNode(
      induction_final_trip_mult_coeff_SENode, src_const);

  // If src_const == LastTripValue, peel_last
  if (induction_final_trip_SENode->CanFoldToConstant()) {
    int64_t final_trip_value = induction_final_trip_SENode->FoldToSingleValue();
    if (distance == final_trip_value) {
      // We have found that peeling the last iteration will break dependency
      dv_entry->peel_last = true;
      return false;
    }
  }

  // We were unable to prove independence or discern any additional information
  // Must assume <=> direction
  dv_entry->direction = DVEntry::ALL;
  return false;
}

// Takes the form a1*i + c1, a2*i + c2
// when a2 = 0
// distance = (c2 - c1) / a1
bool LoopDependenceAnalysis::WeakZeroDestinationSIVTest(SERecurrentNode* source,
                                                        SENode* destination,
                                                        SENode* coefficient,
                                                        DVEntry* dv_entry) {
  // Build an SENode for distance
  SENode* src_const = source->GetOffset();
  SENode* dest_const = destination->GetOffset();
  SENode* delta = scalar_evolution_.CreateSubtraction(dest_const, src_const);

  // Scalar evolution doesn't perform division, so we must fold to constants and
  // do it manually.
  int64_t delta_val = 0;
  int64_t coeff_val = 0;
  int64_t distance = 0;
  if (delta->CanFoldToConstant() && coefficient->CanFoldToConstant()) {
    delta_val = delta->FoldToSingleValue();
    coeff_val = delta->FoldToSingleValue();
    // Check if the distance is not integral or if it has a non-integral part
    // equal to 1/2
    if (delta_val % coeff_val != 0) {
      dv_entry->direction = DVEntry::NONE;
      return true;
    } else {
      distance = delta_val / coeff_val;
    }
  }

  int64_t lower_bound_val = 0;
  int64_t upper_bound_val = 0;
  SENode* lower_bound = GetLowerBound();
  SENode* upper_bound = GetUpperBound();
  // If we can prove the distance is outside the bounds we prove independence
  if (lower_bound->CanFoldToConstant() && upper_bound->CanFoldToConstant()) {
    lower_bound_val = lower_bound->FoldToSingleValue();
    upper_bound_val = upper_bound->FoldToSingleValue();
    if (!IsWithinBounds(distance, lower_bound_val, upper_bound_val)) {
      dv_entry->direction = DVEntry::NONE;
      dv_entry->distance = distance;
      return true;
    }
  }

  // Now we want to see if we can detect to peel the first or last iterations

  // We build the value of the first trip value as an SENode and fold it down to
  // a constant value if possible.
  SENode* induction_first_trip_value_SENode = GetFirstTripInductionNode();
  SENode* induction_first_trip_mult_coeff_SENode =
      scalar_evolution_.CreateMultiplyNode(induction_first_trip_value_SENode,
                                           coefficient);
  SENode* induction_first_trip_SENode = scalar_evolution_.CreateAddNode(
      induction_first_trip_mult_coeff_SENode, dest_const);

  // If dest_const == FirstTripValue, peel_first
  if (induction_first_trip_SENode->CanFoldToConstant()) {
    int64_t first_trip_value = induction_first_trip_SENode->FoldToSingleValue();
    if (distance == first_trip_value) {
      // We have found that peeling the first iteration will break dependency
      dv_entry->peel_first = true;
      return false;
    }
  }

  // We build the value of the final trip value as an SENode and fold it down to
  // a constant value if possible.
  SENode* induction_final_trip_value_SENode = GetFinalTripInductionNode();
  SENode* induction_final_trip_mult_coeff_SENode =
      scalar_evolution_.CreateMultiplyNode(induction_final_trip_value_SENode,
                                           coefficient_);
  SENode* induction_final_trip_SENode = scalar_evolution_.CreateAddNode(
      induction_final_trip_mult_coeff_SENode, dest_const);

  // If dest_const == LastTripValue, peel_last
  if (induction_final_trip_SENode->CanFoldToConstant()) {
    int64_t final_trip_value = induction_final_trip_SENode->FoldToSingleValue();
    if (distance == final_trip_value) {
      // We have found that peeling the last iteration will break dependency
      dv_entry->peel_last = true;
      return false;
    }
  }

  // We were unable to prove independence or discern any additional information
  // Must assume <=> direction
  dv_entry->direction = DVEntry::ALL;
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
  SENode* src_const = source->GetOffset();
  SENode* dest_const = destination->GetOffset();
  SENode* delta = scalar_evolution_.CreateSubtraction(dest_const, src_const);

  // Scalar evolution doesn't perform division, so we must fold to constants and
  // do it manually.
  int64_t delta_val = 0;
  int64_t coeff_val = 0;
  int64_t distance = 0;
  if (delta->CanFoldToConstant() && coefficient->CanFoldToConstant()) {
    delta_val = delta->FoldToSingleValue();
    coeff_val = delta->FoldToSingleValue();
    // Check if the distance is not integral or if it has a non-integral part
    // equal to 1/2
    if (delta_val % (2 * coeff_val) != 0 ||
        (delta_val % (2 * coeff_val)) / (2 * coeff_val) != 0.5) {
      dv_entry->direction = DVEntry::NONE;
      return true;
    } else {
      distance = delta_val / (2 * coeff_val);
    }
  }

  if (distance == 0) {
    dv_entry->direction = DVEntry::EQ;
    dv_entry->distance = 0;
    return false;
  }

  // We were unable to prove independence or discern any additional information
  // Must assume <=> direction
  dv_entry->direction = DVEntry::ALL;
  return false;
}

bool LoopDependenceAnalysis::IsWithinBounds(SENode* value, SENode* bound_one,
                                            SENode* bound_two) {
  // If |bound_one| is the lower bound
  if (bound_one->IsLess(bound_two)) {
    return (value->IsGreaterOrEqual(bound_one) &&
            value->IsLessOrEqual(bound_two));
  } else
      // If |bound_two| is the lower bound
      if (bound_one->IsGreater(bound_two)) {
    return (value->IsGreaterOrEqual(bound_two) &&
            value->IsLessOrEqual(bound_one));
  } else {
    // Both bounds have the same value
    return value->IsEqual(bound_one);
  }
}

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

// Takes the form a1*i + c1, a2*i + c2
// Where a1 and a2 are constant and different
// bool LoopDependenceAnalysis::WeakSIVTest(SENode* source, SENode* destination,
//                                         SENode* src_coeff, SENode*
//                                         dest_coeff,
//                                         DVEntry* dv_entry) {
//  return false;
//}

SENode* LoopDependenceAnalysis::GetLowerBound() {
  ir::Instruction* lower_bound_inst = loop_.GetLowerBoundInst();
  ir::Instruction* upper_bound_inst = loop_.GetUpperBoundInst();
  if (!lower_bound_inst || !upper_bound_inst) {
    return nullptr;
  }

  SENode* lower_SENode =
      scalar_evolution_.AnalyzeInstruction(loop_.GetLowerBoundInst());
  SENode* upper_SENode =
      scalar_evolution_.AnalyzeInstruction(loop_.GetUpperBoundInst());
  if (lower_SENode->IsLess(upper_SENode)) {
    return lower_SENode;
  }
  if (lower_SENode->IsEqual(upper_SENode)) {
    return lower_SENode;
  }
  if (lower_SENode->IsGreater(upper_SENode)) {
    return upper_SENode;
  }
  // We couldn't determine which bound instr was lower and can't determine they
  // are equal. As a result it is not safe to return either bound
  return nullptr;
}

SENode* LoopDependenceAnalysis::GetUpperBound() {
  ir::Instruction* lower_bound_inst = loop_.GetLowerBoundInst();
  ir::Instruction* upper_bound_inst = loop_.GetUpperBoundInst();
  if (!lower_bound_inst || !upper_bound_inst) {
    return nullptr;
  }

  SENode* lower_SENode =
      scalar_evolution_.AnalyzeInstruction(loop_.GetLowerBoundInst());
  SENode* upper_SENode =
      scalar_evolution_.AnalyzeInstruction(loop_.GetUpperBoundInst());
  if (lower_SENode->IsLess(upper_SENode)) {
    return upper_SENode;
  }
  if (lower_SENode->IsEqual(upper_SENode)) {
    return lower_SENode;
  }
  if (lower_SENode->IsGreater(upper_SENode)) {
    return lower_SENode;
  }
  // We couldn't determine which bound instr was higher and can't determine they
  // are equal. As a result it is not safe to return either bound
  return nullptr;
}

std::pair<SENode*, SENode*> LoopDependenceAnalysis::GetLoopLowerUpperBounds() {
  SENode* lower_bound_SENode = GetLowerBound();
  SENode* upper_bound_SENode = GetUpperBound();

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
      if (loop_.FindNumberOfIterations(induction_instr, cond_instr,
                                       &iteration_count)) {
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

  SENode* induction_init_SENode =
      scalar_evolution_.CreateConstant(induction_initial_value);
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

  // TODO(Alexander): Ensure all of these are cached so there isn't any memory
  // leakage

  SENode* induction_init_SENode =
      scalar_evolution_.CreateConstant(induction_initial_value);
  SENode* step_SENode = scalar_evolution_.AnalyzeInstruction(step_instr);
  SENode* total_change_SENode =
      scalar_evolution_.CreateMultiplyNode(step_SENode, trip_count);
  SENode* final_iteration = scalar_evolution_.CreateAddNode(
      induction_init_SENode, total_change_SENode);

  return final_iteration;
}

ir::LoopDescriptor* LoopDependenceAnalysis::GetLoopDescriptor() {
  return context_->GetLoopDescriptor(loop_.GetHeaderBlock()->GetParent());
}

}  // namespace opt
}  // namespace spvtools
