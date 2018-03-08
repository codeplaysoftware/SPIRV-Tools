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
    if (WeakCrossingSIVTest(source_node, destination_node, src_coeff,
                            dest_coeff, dv_entry))
      return true;
  }

  // If the subscript takes the form [a1*i + c1] = [a2*i + c2] use weak SIV
  if (src_coeff && dest_coeff && !src_coeff->IsEqual(dest_coeff)) {
    if (WeakSIVTest(source_node, destination_node, src_coeff, dest_coeff,
                    dv_entry))
      return true;
  }
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
  return false;
}

// Takes the form a*i + c1, a*i + c2
// When c1 and c2 are loop invariant and a is constant
// distance = (c1 - c2)/a
//              < if distance > 0
// direction =  = if distance = 0
//              > if distance < 0
bool LoopDependenceAnalysis::StrongSIVTest(SENode* source, SENode* destination,
                                           SENode* coefficient,
                                           DVentry* dv_entry) {
  // Build an SENode for distance
  SENode* src_const = source->GetConstant();
  SENode* dest_const = destination->GetConstant();

  SENode* delta = scalar_evolution_.CreateSubtraction(src_const, dest_const);
  SENode* distance = scalar_evolution_.CreateDivision(delta, coefficient);

  SENode* trip_count = nullptr;
  if (!GetTripCount(&trip_count)) {
    return false;
  }

  // If abs(delta) > trip_count
  SENode* abs_delta = delta->abs();
  if (abs_delta->IsGreater(trip_count)) {
    // Prove independence
    dv_entry->direction = DVEntry::NONE;
    return true;
  }

  SENode* distance = scalar_evolution_.CreateDivision(delta, coefficient);
  if (!coeff_div_delta) {
    return false;
  }

  // Now attempt to compute distance
  // Only do this if we can fold the distance to a constant value
  if (distance->Foldable()) {
    // Check that coefficient divides delta exactly.
    // If it does not, we prove independence
    if (!distance->IsIntegral()) {
      dv_entry->direction = DVEntry::NONE;
      return true;
    }

    if (distance->IsGreater(0)) {
      // direction LT
      dv_entry->direction = DVEntry::LT;
      return false;
    } else if (distance->IsLess(0)) {
      // direction GT
      dv_entry->direction = DVEntry::GT;
      return false;
    } else {
      // direction EQ
      dv_entry->direction = DVEntry::EQ;
      return false;
    }
  } else if (delta->IsEqual(0)) {
    // direction = EQ
    // distance = 0
    dv_entry->direction = DVEntry::EQ;
    dv_entry->distance = 0;
    return false;
  } else {
    if (coefficient->IsEqual(1)) {
      // distance = delta
      dv_entry->distance = delta->FoldToSingleValue();
      return false;
    }
  }

  // Can't prove independence
  return false;
}

// Takes the form a1*i + c1, a2*i + c2
// when a1 = 0
// i = (c2 - c1) / a2
bool LoopDependenceAnalysis::WeakZeroSourceSIVTest(SENode* source,
                                                   SENode* destination,
                                                   SENode* coefficient,
                                                   DVentry* dv_entry) {
  // Build an SENode for i
  SENode* src_const = source->GetConstant();
  SENode* dest_const = destination->GetConstant();

  SENode* const_sub = scalar_evolution_.CreateNegation(src_const, dest_const);
  SENode* i = scalar_evolution_.CreateDivision(const_sub, coefficient);

  // If i is not an integer, there is no dependence
  if (!i->isIntegral()) {
    dv_entry->direction = NONE;
    return true;
  }

  SENode* upper_bound = nullptr;
  if (!GetUpperBound(&upper_bound)) {
    return false;
  }

  // If i < 0 or > upper_bound, there is no dependence
  if (i->LessThan(0) || i->GreaterThan(upper_bound)) {
    dv_entry->direction = NONE;
    return true;
  }

  // If i = 0, the direction is <= and peeling the 1st iteration will break the
  // dependence
  if (i->IsZero()) {
    dv_entry->direction = DVEntry::LE;
    dv_entry->peel_first = true;
    return false;
  }

  // If i = upper_bound, the dependence is >= and peeling the last iteration
  // will break the dependence
  if (i->IsEqual(upper_bound)) {
    dv_entry->direction = DVEntry::GE;
    dv_entry->peel_last = true;
    return false;
  }

  // Otherwise we can't prove an independence or dependence direction so assume
  // <=>
  return false;
}

// Takes the form a1*i + c1, a2*i + c2
// when a2 = 0
// i = (c2 - c1) / a1
bool LoopDependenceAnalysis::WeakZeroDestinationSIVTest(SENode* source,
                                                        SENode* destination,
                                                        SENode* coefficient,
                                                        DVentry* dv_entry) {
  // Build an SENode for i
  SENode* src_const = source->GetConstant();
  SENode* dest_const = destination->GetConstant();

  SENode* const_sub = scalar_evolution_.CreateNegation(dest_const, src_const);
  SENode* i = scalar_evolution_.CreateDivision(const_sub, coefficient);

  // If i is not an integer, there is no dependence
  if (!i->IsIntegral()) {
    dv_entry->direction = DVEntry::NONE;
    return true;
  }

  SENode* upper_bound = nullptr;
  if (!GetUpperBound(&upper_bound)) {
    return false;
  }

  // If i < 0 or > upper_bound, there is no dependence
  if (i->LessThan(0) || i->GreaterThan(upper_bound)) {
    dv_entry->direction = DVEntry::NONE;
    return true;
  }

  // If i == 0, the direction is <= and peeling the first iteration will break
  // the dependence
  if (i->IsZero()) {
    dv_entry->peel_first = true;
    return false;
  }

  // If i == upper_bound, the direction is >= and peeling the last iteration
  // will break the dependence
  if (i->IsEqual(upper_bound)) {
    dv_entry->peel_last = true;
    return false;
  }

  // Otherwise we can't prove an independence or dependence direction so assume
  // <=>
  dv_entry->direction = DVEntry::ALL;
  return false;
}

// Takes the form a1*i + c1, a2*i + c2
// When a1 = -a2
// i = (c2 - c1) / 2*a1
bool LoopDependenceAnalysis::WeakCrossingSIVTest(SENode* source,
                                                 SENode* destination,
                                                 SENode* coefficient,
                                                 DVentry* dv_entry) {
  SENode* src_const = source->GetConstant();
  SENode* dest_const = destination->GetConstant();

  SENode* const_sub = scalar_evolution_.CreateNegation(dest_const, src_const);
  SENode* const_2 = scalar_evolution_.CreateConstant(2);
  SENode* mult_coeff =
      scalar_evolution_.CreateMultiplication(const_2, coefficient);
  SENode* i = scalar_evolution_.CreateDivision(const_sub, mult_coeff);

  // If i = 0, there is a dependence of distance 0
  if (i->IsZero()) {
    dv_entry->direction = DVEntry::EQ;
    dv_entry->distance = 0;
  }

  // If i < 0, there is no dependence
  if (i->LessThan(0)) {
    dv_entry->direction = DVEntry::NONE;
    return true;
  }

  SENode* upper_bound = nullptr;
  if (!GetUpperBound(&upper_bound)) {
    return false;
  }

  // If i > upper_bound, there is no dependence
  if (i->GreaterThan(upper_bound)) {
    dv_entry->direction = DVEntry::NONE;
    return true;
  }

  // If i == upper_bound, there is a dependence with distance = 0
  if (i->IsEqual(upper_bound)) {
    dv_entry->direction = DVEntry::EQ;
    dv_entry->distance = 0;
  }
}

bool LoopDependenceAnalysis::IsWithinBounds(SENode* value, SENode* bound_one,
                                            SENode* bound_two) {
  SENode* abs_value = nullptr;

  // Get the absolute value of |value|
  if (value->IsNegative()) {
    abs_value = scalar_evolution_.CreateNegation(value);
  } else {
    abs_value = value;
  }

  // If |bound_one| is the lower bound
  if (bound_one->IsLess(bound_two)) {
    return (abs_value->IsGreaterOrEqual(bound_one) &&
            abs_value->IsLessOrEqual(bound_two));
  } else
      // If |bound_two| is the lower bound
      if (bound_one->IsGreater(bound_two)) {
    return (abs_value->IsGreaterOrEqual(bound_two) &&
            abs_value->IsLessOrEqual(bound_one));
  } else {
    // Both bounds have the same value
    return abs_value->IsEqual(bound_one);
  }
}

// Takes the form a1*i + c1, a2*i + c2
// Where a1 and a2 are constant and different
bool LoopDependenceAnalysis::WeakSIVTest(SENode* source, SENode* destination,
                                         SENode* src_coeff, SENode* dest_coeff,
                                         DVEntry* dv_entry) {
  return false;
}

bool LoopDependenceAnalysis::BannerjeeGCDtest(SENode* source,
                                              SENode* destination,
                                              DVEntry* dv_entry) {
  return false;
}

bool LoopDependenceAnalysis::DeltaTest(SENode* source, SENode* direction) {
  return false;
}

SENode* LoopDependenceAnalysis::GetLowerBound() {
  ir::Instruction* lower_bound_inst = loop_.GetLowerBoundInst();
  if (!lower_bound_inst) {
    return nullptr;
  }

  SENode* lower_SENode =
      scalar_evolution_.AnalyzeInstruction(loop_.GetLowerBoundInst());
  return lower_SENode;
}

SENode* LoopDependenceAnalysis::GetUpperBound() {
  ir::Instruction* upper_bound_inst = loop_.GetUpperBoundInst();
  if (!upper_bound_inst) {
    return nullptr;
  }

  SENode* upper_SENode =
      scalar_evolution_.AnalyzeInstruction(loop_.GetUpperBoundInst());
  return upper_SENode;
}

SENode* LoopDependenceAnalysis::GetLoopLowerUpperBounds() {
  SENode* lower_bound_SENode = GetLowerBound();
  SENode* upper_bound_SENode = GetUpperBound();
  if (!lower_bound_SENode || !upper_bound_SENode) {
    return nullptr;
  }

  return std::make_pair(lower_bound_SENode, upper_bound_SENode);
}

SENode* LoopDependenceAnalysis::GetTripCount() {
  ir::BasicBlock* condition_block = loop_.FindConditionBlock();
  if (!condition_block) {
    return nullptr;
  }
  ir::Instruction* induction_instr = loop_.FindConditionVariable(condition);
  if (!induction_instr) {
    return nullptr;
  }
  ir::Instruction* cond_instr = loop_.GetConditionInst();
  if (!cond_instr) {
    return nullptr;
  }

  int64_t iteration_count = 0;

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
      if (!loop_.FindNumberOfIterations(induction_instr, condition_instr,
                                        &iteration_count)) {
        return nullptr;
      }
      break;
    default:
      return nullptr;
  }

  return nullptr;
}

SENode* LoopDependenceAnalysis::GetFinalTripValue() {
  ir::BasicBlock* condition_block = loop_.FindConditionBlock();
  if (!condition_block) {
    return nullptr;
  }
  ir::Instruction* induction_instr = loop_.FindConditionVariable(condition);
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
      scalar_evolution_.CreateNode(induction_initial_value);
  SENode* step_SENode = scalar_evolution_.AnalyzeInstruction(step_instr);
  SENode* total_change_SENode =
      scalar_evolution_.CreateMultiplication(step_SENode, trip_count);
  SENode* final_iteration = scalar_evolution_.CreateAddNode(
      induction_init_SENode, total_change_SENode);

  return final_iteration;
}

LoopDescriptor* LoopDependenceAnalysis::GetLoopDescriptor() {
  return context_->GetLoopDescriptor(loop_.GetHeaderBlock()->GetParent());
}

}  // namespace opt
}  // namespace spvtools
