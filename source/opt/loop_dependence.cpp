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

  void DumpIterationSpaceAsDot(std::ostream& out_stream) {
    out_stream << "digraph {\n";

    for (uint32_t id : loop_.GetBlocks()) {
      ir::BasicBlock* block = context_->cfg()->block(id);
      for (ir::Instruction& inst : *block) {
        if (inst.opcode() == SpvOp::SpvOpStore ||
            inst.opcode() == SpvOp::SpvOpLoad) {
          memory_access_to_indice_[&inst] = {};

          const ir::Instruction* access_chain =
              context_->get_def_use_mgr()->GetDef(
                  inst.GetSingleWordInOperand(0));

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

 private:
  ir::IRContext* context_;

  // The loop we are analysing the dependence of.
  const ir::Loop& loop_;

  ScalarEvolutionAnalysis scalar_evolution_;

  std::map<const ir::Instruction*, std::vector<SENode*>>
      memory_access_to_indice_;

  bool ZIVTest(const SENode& source, const SENode& destination) {
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

  bool SIVTest(SENode* source, SENode* destination) {
    return StrongSIVTest(source, destination);
  }

  bool StrongSIVTest(SENode* source, SENode* destination) {
    SENode* new_negation = scalar_evolution_.CreateNegation(destination);
    //SENode* new_add = 
    SENode* distance = scalar_evolution_.CreateAddNode(source, new_negation);

    int64_t value = 0;
    distance->FoldToSingleValue(&value);

    std::cout << value << std::endl;

    return true;
  }



} // namespace opt
} // namespace spvtools


