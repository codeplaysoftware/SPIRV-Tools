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
                                           const ir::Instruction* destination) {
  SENode* source_node = memory_access_to_indice_[source][0];
  SENode* destination_node = memory_access_to_indice_[destination][0];
  bool independence_proved = false;

  // TODO(Alexander): Check source and destination are loading and storing from
  // the same variables. If not, there is no dependence

  // If the subscript is constant, preform a ZIV test

  independence_proved = ZIVTest(*source_node, *destination_node);
  if (independence_proved) return true;

  // If the subscript takes the form [a*i + c1] = [a*i + c2] use strong SIV
  independence_proved = SIVTest(*source_node, *destination_node);
  if (independence_proved) return true;

  // If the subscript takes the form [c1] = [a*i + c2] use weak zero source SIV
  independence_proved = WeakZeroSourceSIVTest();
  if (independence_proved) return true;

  // If the subscript takes the form [a*i + c1] = [c2] use weak zero dest SIV
  independence_proved = WeakZeroDestinationSIVTest();
  if (independence_proved) return true;

  // If the subscript takes the form [a1*i + c1] = [a2*i + c2] where a1 = -a2
  // use weak crossing SIV
  independence_proved = WeakCrossingSIVTest();
  if (independence_proved) return true;

  // If the subscript takes the form [a1*i + c1] = [a2*i + c2] use weak SIV
  independence_proved = WeakSIVTest(source_node, destination_node);
  if (independence_proved) return true;

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

bool LoopDependenceAnalysis::ZIVTest(const SENode& source,
                                     const SENode& destination) {
  int64_t source_value = 0;
  int64_t destintion_value = 0;
  // Fold the nodes to a single value. If we can't fold both of the nodes then
  // we can not prove independence.
  if (source.FoldToSingleValue(&source_value)) {
    return false;
  }
  if (destination.FoldToSingleValue(&destination_value)) {
    return false;
  }
  // If we can prove that the source and destination are not equal values
  // we prove independence
  return source_value == destination_value;
}

bool LoopDependenceAnalysis::SIVTest(SENode* source, SENode* destination,
                                     DVEntry dv_entry) {
  // Get the coefficients of source and destination
  SENode* src_coeff = source->GetCoefficient();
  SENode* dest_coeff = destination->GetCoefficient();
  int64_t src_coeff_val = 0;
  int64_t dest_coeff_val = 0;

  if (!src_coeff->FoldToSingleValue(src_coeff_val) ||
      !dest_coeff->FoldToSingleValue(dest_coeff_val)) {
    // If we can't fold the coefficients to constant values we have an
    // unsupported case.
    return false;
  }

  bool independence_proved = false;

  // If both source and destination have the same coefficients, we can use a
  // strong SIV test
  if (src_coeff_val == dest_coeff_val) {
    independence_proved =
        StrongSIVTest(source, destination, src_coeff_val, dv_entry);
    if (independence_proved) return true;
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
                                           DVentry dv_entry) {
  // Build an SENode for distance
  SENode* src_const = source->GetConstant();
  SENode* dest_const = destination->GetConstant();

  ScalarEvolutionAnalysis scal_evo_analysis = ScalarEvolutionAnalysis(context_);

  SENode* delta = scal_evo_analysis.CreateNegation(src_const, dest_const);
  SENode* distance = scal_evo_analysis.CreateDivision(delta, coefficient);

  SENode* lower_bound = nullptr;
  SENode* upper_bound = nullptr;
  if (!GetLoopBounds(&lower_bound, &upper_bound)) {
    return false;
  }

  // If delta < upper_bound
  if ()









  // First we must collect some data for use in the pass
  // The loop bounds
  // The distance between the load and store (delta)


  int64_t delta = 0;
  if (!GetDelta(source, destination, &delta)) {
    // We couldn't find delta so return false
    return false;
  }

  // Now we have all the required information we need we can perform tests

  // If the distance between source and destination is > than trip count we
  // prove independence.
  if (llabs(delta) > llabs(bound_value)) {
    return true;
  }
  int remainder = delta % coefficient;
  if (remainder != 0) {
    // If the coefficient does not exactly divide delta, we prove independence
    return true;
  }

  // Now check for directions
  if (delta > 0) {
    dv_entry = DVEntry::LT;
    return true;
  }
  if (delta == 0) {
    dv_entry = DVEntry::EQ;
    return true;
  }
  if (delta < 0) {
    dv_entry = DVEntry::GT;
    return true;
  }
  // TODO(Alexander): Set the distance too

  // TODO(Alexander): The tests below are not implemented because we currently
  // rely on |Delta| and |a| being constant. When supporting symbolics
  // we must do the below checks if |Delta| and/or |a| have symbols.

  // Else if |Delta| == 0, as 0/|a| == 0
  //   distance = 0 and direction is =

  // Else
  //   If |a| == 1
  //     distance = delta since X/a == X
  //   Else
  //     Try to find a direction

  // Can't prove independence
  return false;
}

// Takes the form a1*i + c1, a2*i + c2
// Where a1 and a2 are constant and different
bool LoopDependenceAnalysis::WeakSIVTest() { return false; }

// Takes the form a1*i + c1, a2*i + c2
// when a1 = 0
// i = (c2 - c1) / a2
bool LoopDependenceAnalysis::WeakZeroSourceSIVTest(SENode* source,
                                                   SENode* destination,
                                                   SENode* coefficient,
                                                   DVentry dv_entry) {
  // Build an SENode for i
  SENode* src_const = source->GetConstant();
  SENode* dest_const = destination->GetConstant();

  ScalarEvolutionAnalysis scal_evo_analysis = ScalarEvolutionAnalysis(context_);

  SENode* const_sub = scal_evo_analysis.CreateNegation(src_const, dest_const);
  SENode* i = scal_evo_analysis.CreateDivision(const_sub, coefficient);

  // If i is not an integer, there is no dependence
  if (!i->isIntegral()) {

  }

  SENode* upper_bound = nullptr;
  if (!GetUpperBound(&upper_bound)) {
    return false;
  }

  // If i < 0 or > upper_bound, there is no dependence
  if (i->LessThan(0) || i->GreaterThan(upper_bound)) {

  }

  // If i = 0, the direction is <= and peeling the 1st iteration will break the
  // dependence
  if (i->IsZero()) {

  }

  // If i = upper_bound, the dependence is >= and ppeling the last iteration
  // will break the dependence
  if (i->IsEqual(upper_bound)) {

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
                                                        DVentry dv_entry) {
  // Build an SENode for i
  SENode* src_const = source->GetConstant();
  SENode* dest_const = destination->GetConstant();

  ScalarEvolutionAnalysis scal_evo_analysis = ScalarEvolutionAnalysis(context_);

  SENode* const_sub = scal_evo_analysis.CreateNegation(dest_const, src_const);
  SENode* i = scal_evo_analysis.CreateDivision(const_sub, coefficient);

  // If i is not an integer, there is no dependence
  if (!i->IsIntegral()) {
    return true;
  }

  SENode* upper_bound = nullptr;
  if (!GetUpperBound(&upper_bound)) {
    return false;
  }

  // If i < 0 or > upper_bound, there is no dependence
  if (i->LessThan(0) || i->GreaterThan(upper_bound)) {

  }

  // If i == 0, the direction is <= and peeling the first iteration will break
  // the dependence
  if (i->IsZero()) {

  }

  // If i == upper_bound, the direction is >= and peeling the last iteration
  // will break the dependence
  if (i->IsEqual(upper_bound));

  // Otherwise we can't prove an independence or dependence direction so assume
  // <=>
  return false;
}

// Takes the form a1*i + c1, a2*i + c2
// When a1 = -a2
// i = (c2 - c1) / 2*a1
bool LoopDependenceAnalysis::WeakCrossingSIVTest(SENode* source,
                                                 SENode* destination,
                                                 SENode* coefficient,
                                                 DVentry dv_entry) {
  SENode* src_const = source->GetConstant();
  SENode* dest_const = destination->GetConstant();

  ScalarEvolutionAnalysis scal_evo_analysis = ScalarEvolutionAnalysis(context_);

  SENode* const_sub = scal_evo_analysis.CreateNegation(dest_const, src_const);
  SENode* const_2 = scal_evo_analysis.CreateConstant(2);
  SENode* mult_coeff =
      scal_evo_analysis.CreateMultiplication(const_2, coefficient);
  SENode* i = scal_evo_analysis.CreateDivision(const_sub, mult_coeff);

  // If i = 0, there is a dependence of distance 0
  if (i->IsZero()) {
  }

  // If i < 0, there is no dependence
  if (i->LessThan(0)) {
  }

  SENode* upper_bound = nullptr;
  if (!GetUpperBound(&upper_bound)) {
    return false;
  }

  // If i > upper_bound, there is no dependence
  if (i->GreaterThan(upper_bound)) {
  }

  // If i == upper_bound, there is a dependence with distance = 0
  if (i->IsEqual(upper_bound)) {
  }
}

bool LoopDependenceAnalysis::GetLowerBound(SENode** lower_bound) {
  ir::Instruction* lower_bound_inst = loop_.GetLowerBoundInst();
  if (!lower_bound_inst) {
    return false;
  }

  ScalarEvolutionAnalysis scal_evo_analysis = ScalarEvolutionAnalysis(context_);

  SENode* lower_SENode =
      scal_evo_analysis.AnalyzeInstruction(loop_.GetLowerBoundInst());
  if (!lower_SENode) {
    return false;
  }
  *lower_bound = lower_SENode;
  return true;
}

bool LoopDependenceAnalysis::GetUpperBound(SENode** upper_bound) {
  ir::Instruction* upper_bound_inst = loop_.GetUpperBoundInst();
  if (!upper_bound_inst) {
    return false;
  }

  ScalarEvolutionAnalysis scal_evo_analysis = ScalarEvolutionAnalysis(context_);

  SENode* upper_SENode =
      scal_evo_analysis.AnalyzeInstruction(loop_.GetUpperBoundInst());
  if (!upper_SENode) {
    return false;
  }
  *upper_bound = upper_SENode;
  return true;
}

bool LoopDependenceAnalysis::GetLoopBounds(SENode** lower_bound,
                                           SENode** upper_bound) {
  SENode* lower_bound_SENode = nullptr;
  SENode* upper_bound_SENode = nullptr;
  if (!GetLowerBound(&lower_bound_SENode) ||
      !GetUpperBound(&upper_bound_SENode)) {
    return false;
  }

  *lower_bound = lower_bound_node;
  *upper_bound = upper_bound_node;
  return true;
}

}  // namespace opt
}  // namespace spvtools
