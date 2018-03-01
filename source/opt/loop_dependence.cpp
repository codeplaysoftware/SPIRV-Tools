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
  // If we can prove not equal then we have prove independence.
  if (scalar_evolution_.CanProveNotEqual(source, destination)) {
    return true;
  }

  // If source can be proven to equal destination then we have proved
  // dependence.
  if (scalar_evolution_.CanProveEqual(source, destination)) {
    return false;
  }

  // Otherwise, we must assume they are dependent.
  return false;
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
                                           int64_t coefficient,
                                           DVentry dv_entry) {
  ScalarEvolutionAnalysis scal_evo_analysis = ScalarEvolutionAnalysis(context_);

  // First we must collect some data for use in the pass
  // The loop bounds
  // The distance between the load and store (delta)

  // Get the loop bounds as upper_bound - lower_bound
  SENode* lower_SENode =
      scal_evo_analysis.AnalyzeInstruction(loop_.GetLowerBoundInst());
  SENode* upper_SENode =
      scal_evo_analysis.AnalyzeInstruction(loop_.GetupperBoundInst());

  // Find the absolute value of the bounds.
  int64_t lower_bound_value = 0;
  int64_t upper_bound_value = 0;
  if (!lower_SENode->FoldToSingleValue(lower_bound_value) ||
      !upper_SENode->FoldToSingleValue(upper_bound_value)) {
    // We can't get the bounds of the loop, so return false
    // This will be different when we deal with symbolics
    return false;
  }
  int64_t bound_value = upper_bound_value - lower_bound_value;

  // Get |Delta| as |(c1 - c2)|
  int64_t src_const = 0;
  int64_t dest_const = 0;
  if (!source->GetConstantValue(src_const) ||
      !destination->GetConstantValue(dest_const)) {
    // We can't get the constant terms required to calculate delta, so return
    // false.
    // This will be different when we deal with symbolics
    return false;
  }

  int64_t delta = src_const - dest_const;

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
bool LoopDependenceAnalysis::WeakZeroSourceSIVTest() { return false; }

// Takes the form a1*i + c1, a2*i + c2
// when a2 = 0
// i = (c2 - c1) / a1
bool LoopDependenceAnalysis::WeakZeroDestinationSIVTest() { return false; }

// Takes the form a1*i + c1, a2*i + c2
// When a1 = -a2
// i = (c2 - c1) / 2*a1
bool LoopDependenceAnalysis::WeakCrossingSIVTest() { return false; }

}  // namespace opt
}  // namespace spvtools
