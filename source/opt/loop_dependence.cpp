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
  /*    return ZIVTest(*source_node,
                     *destination_node);*/

  return SIVTest(source_node, destination_node);
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
  // If source can be proven to equal destination then we have proved
  // dependence.
  if (scalar_evolution_.CanProveEqual(source, destination)) {
    return true;
  }

  // If we can prove not equal then we have prove independence.
  if (scalar_evolution_.CanProveNotEqual(source, destination)) {
    return false;
  }

  // Otherwise, we must assume they are dependent.
  return true;
}

bool LoopDependenceAnalysis::SIVTest(SENode* source, SENode* destination) {
  return StrongSIVTest(source, destination);
}

// Takes the form a*i + c1, a*i + c2
// When c1 and c2 are loop invariant and a is constant
// distance = (c1 - c2)/a
//              < if distance > 0
// direction =  = if distance = 0
//              > if distance < 0

bool LoopDependenceAnalysis::StrongSIVTest(SENode* source,
                                           SENode* destination) {
  // Get |Delta| as |(c1 - c2)|

  // Compare the distance between source and destination and the trip count.
  // If the distance is greater, there is no dependence

  // Try to compute the distance
  // If both |Delta| and |a| are constant
  //   Check |a| divides by |Delta| exactly.
  //   If not, no dependence
  // Otherwise distance = |Delta| / |a|
  // From this we can take the direction vector

  // Else if |Delta| == 0, as 0/|a| == 0
  //   distance = 0 and direction is =

  // Else
  //   If |a| == 1
  //     distance = delta since X/a == X
  //   Else
  //     Try to find a direction

  // TODO(Alexander): Above

  // Second

  SENode* new_negation = scalar_evolution_.CreateNegation(destination);
  // SENode* new_add =
  SENode* distance = scalar_evolution_.CreateAddNode(source, new_negation);

  int64_t value = 0;
  distance->FoldToSingleValue(&value);

  std::cout << value << std::endl;

  return true;
}

// Takes the form a1*i + c1, a2*i + c2
// Where a1 and a2 are constant and different
bool LoopDependenceAnalysis::WeakSIVTest() {
  return false;
}

// Takes the form a1*i + c1, a2*i + c2
// when a1 = 0
// i = (c2 - c1) / a2
bool LoopDependenceAnalysis::WeakZeroSourceSIVTest() {
  return false;
}

// Takes the form a1*i + c1, a2*i + c2
// when a2 = 0
// i = (c2 - c1) / a1
bool LoopDependenceAnalysis::WeakZeroDestinationSIVTest() {
  return false;
}

// Takes the form a1*i + c1, a2*i + c2
// When a1 = -a2
// i = (c2 - c1) / 2*a1
bool LoopDependenceAnalysis::WeakCrossingSIVTest() {
  return false;}

}  // namespace opt
}  // namespace spvtools
