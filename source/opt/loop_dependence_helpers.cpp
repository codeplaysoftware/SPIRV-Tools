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

#include <ostream>
#include <utility>
#include <vector>

#include "opt/instruction.h"
#include "opt/scalar_analysis.h"
#include "opt/scalar_analysis_nodes.h"

namespace spvtools {
namespace opt {

bool LoopDependenceAnalysis::IsZIV(
    const std::pair<SENode*, SENode*>& subscript_pair) {
  return CountInductionVariables(subscript_pair.first, subscript_pair.second) ==
         0;
}

bool LoopDependenceAnalysis::IsSIV(
    const std::pair<SENode*, SENode*>& subscript_pair) {
  return CountInductionVariables(subscript_pair.first, subscript_pair.second) ==
         1;
}

bool LoopDependenceAnalysis::IsMIV(
    const std::pair<SENode*, SENode*>& subscript_pair) {
  return CountInductionVariables(subscript_pair.first, subscript_pair.second) >
         1;
}

SENode* LoopDependenceAnalysis::GetLowerBound() {
  ir::Instruction* cond_inst = loop_.GetConditionInst();
  if (!cond_inst) {
    return nullptr;
  }
  ir::Instruction* lower_inst =
      context_->get_def_use_mgr()->GetDef(cond_inst->GetSingleWordInOperand(0));
  switch (cond_inst->opcode()) {
    case SpvOpULessThan:
    case SpvOpSLessThan:
    case SpvOpULessThanEqual:
    case SpvOpSLessThanEqual:
    case SpvOpUGreaterThan:
    case SpvOpSGreaterThan:
    case SpvOpUGreaterThanEqual:
    case SpvOpSGreaterThanEqual: {
      // If we have a phi we are looking at the induction variable. We look
      // through the phi to the initial value of the phi upon entering the loop.
      if (lower_inst->opcode() == SpvOpPhi) {
        lower_inst = context_->get_def_use_mgr()->GetDef(
            lower_inst->GetSingleWordInOperand(0));
        // We don't handle looking through multiple phis.
        if (lower_inst->opcode() == SpvOpPhi) {
          return nullptr;
        }
      }
      return scalar_evolution_.SimplifyExpression(
          scalar_evolution_.AnalyzeInstruction(lower_inst));
    }
    default:
      return nullptr;
  }
}

SENode* LoopDependenceAnalysis::GetUpperBound() {
  ir::Instruction* cond_inst = loop_.GetConditionInst();
  if (!cond_inst) {
    return nullptr;
  }
  ir::Instruction* upper_inst =
      context_->get_def_use_mgr()->GetDef(cond_inst->GetSingleWordInOperand(1));
  switch (cond_inst->opcode()) {
    case SpvOpULessThan:
    case SpvOpSLessThan: {
      // When we have a < condition we must subtract 1 from the analyzed upper
      // instruction.
      SENode* upper_bound = scalar_evolution_.SimplifyExpression(
          scalar_evolution_.CreateSubtraction(
              scalar_evolution_.AnalyzeInstruction(upper_inst),
              scalar_evolution_.CreateConstant(1)));
      return upper_bound;
    }
    case SpvOpUGreaterThan:
    case SpvOpSGreaterThan: {
      // When we have a > condition we must add 1 to the analyzed upper
      // instruction.
      SENode* upper_bound =
          scalar_evolution_.SimplifyExpression(scalar_evolution_.CreateAddNode(
              scalar_evolution_.AnalyzeInstruction(upper_inst),
              scalar_evolution_.CreateConstant(1)));
      return upper_bound;
    }
    case SpvOpULessThanEqual:
    case SpvOpSLessThanEqual:
    case SpvOpUGreaterThanEqual:
    case SpvOpSGreaterThanEqual: {
      // We don't need to modify the results of analyzing when we have <= or >=.
      SENode* upper_bound = scalar_evolution_.SimplifyExpression(
          scalar_evolution_.AnalyzeInstruction(upper_inst));
      return upper_bound;
    }
    default:
      return nullptr;
  }
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

bool LoopDependenceAnalysis::IsProvablyOutwithLoopBounds(SENode* distance,
                                                         SENode* coefficient) {
  // We test to see if we can reduce the coefficient to an integral constant.
  SEConstantNode* coefficient_constant = coefficient->AsSEConstantNode();
  if (!coefficient_constant) {
    PrintDebug(
        "IsProvablyOutwithLoopBounds could not reduce coefficient to a "
        "SEConstantNode so must exit.");
    return false;
  }

  SENode* lower_bound = GetLowerBound();
  SENode* upper_bound = GetUpperBound();
  if (!lower_bound || !upper_bound) {
    PrintDebug(
        "IsProvablyOutwithLoopBounds could not get both the lower and upper "
        "bounds so must exit.");
    return false;
  }
  // If the coefficient is positive we calculate bounds as upper - lower
  // If the coefficient is negative we calculate bounds as lower - upper
  SENode* bounds = nullptr;
  if (coefficient_constant->FoldToSingleValue() >= 0) {
    PrintDebug(
        "IsProvablyOutwithLoopBounds found coefficient >= 0.\n"
        "Using bounds as upper - lower.");
    bounds = scalar_evolution_.SimplifyExpression(
        scalar_evolution_.CreateSubtraction(upper_bound, lower_bound));
  } else {
    PrintDebug(
        "IsProvablyOutwithLoopBounds found coefficient < 0.\n"
        "Using bounds as lower - upper.");
    bounds = scalar_evolution_.SimplifyExpression(
        scalar_evolution_.CreateSubtraction(lower_bound, upper_bound));
  }

  // We can attempt to deal with symbolic cases by subtracting |distance| and
  // the bound nodes. If we can subtract, simplify and produce a SEConstantNode
  // we can produce some information.

  SEConstantNode* distance_minus_bounds =
      scalar_evolution_
          .SimplifyExpression(
              scalar_evolution_.CreateSubtraction(distance, bounds))
          ->AsSEConstantNode();
  if (distance_minus_bounds) {
    PrintDebug(
        "IsProvablyOutwithLoopBounds found distance - bounds as a "
        "SEConstantNode with value " +
        std::to_string(distance_minus_bounds->FoldToSingleValue()));
    // If distance - bounds > 0 we prove the distance is outwith the loop
    // bounds.
    if (distance_minus_bounds->FoldToSingleValue() > 0) {
      PrintDebug(
          "IsProvablyOutwithLoopBounds found distance escaped the loop "
          "bounds.");
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

SENode* LoopDependenceAnalysis::GetFinalTripInductionNode(
    SENode* induction_coefficient) {
  SENode* first_trip_induction_node = GetFirstTripInductionNode();
  if (!first_trip_induction_node) {
    return nullptr;
  }
  // Get trip_count as GetTripCount - 1
  // This is because the induction variable is not stepped on the first
  // iteration of the loop
  SENode* trip_count =
      scalar_evolution_.SimplifyExpression(scalar_evolution_.CreateSubtraction(
          GetTripCount(), scalar_evolution_.CreateConstant(1)));
  // Return first_trip_induction_node + trip_count * induction_coefficient
  return scalar_evolution_.SimplifyExpression(scalar_evolution_.CreateAddNode(
      first_trip_induction_node,
      scalar_evolution_.CreateMultiplyNode(trip_count, induction_coefficient)));
}

int64_t LoopDependenceAnalysis::CountInductionVariables(SENode* node) {
  if (!node) {
    return -1;
  }

  std::vector<SERecurrentNode*> recurrent_nodes = node->CollectRecurrentNodes();

  // We don't handle loops with more than one induction variable. Therefore we
  // can identify the number of induction variables by collecting all of the
  // loops the collected recurrent nodes belong to.
  std::unordered_set<const ir::Loop*> loops{};
  for (auto recurrent_nodes_it = recurrent_nodes.begin();
       recurrent_nodes_it != recurrent_nodes.end(); ++recurrent_nodes_it) {
    loops.insert((*recurrent_nodes_it)->GetLoop());
  }

  return static_cast<int64_t>(loops.size());
}

int64_t LoopDependenceAnalysis::CountInductionVariables(SENode* source,
                                                        SENode* destination) {
  if (!source || !destination) {
    return -1;
  }

  std::vector<SERecurrentNode*> source_nodes = source->CollectRecurrentNodes();
  std::vector<SERecurrentNode*> destination_nodes =
      destination->CollectRecurrentNodes();

  // We don't handle loops with more than one induction variable. Therefore we
  // can identify the number of induction variables by collecting all of the
  // loops the collected recurrent nodes belong to.
  std::unordered_set<const ir::Loop*> loops{};
  for (auto source_nodes_it = source_nodes.begin();
       source_nodes_it != source_nodes.end(); ++source_nodes_it) {
    loops.insert((*source_nodes_it)->GetLoop());
  }
  for (auto destination_nodes_it = destination_nodes.begin();
       destination_nodes_it != destination_nodes.end();
       ++destination_nodes_it) {
    loops.insert((*destination_nodes_it)->GetLoop());
  }

  return static_cast<int64_t>(loops.size());
}

SENode* LoopDependenceAnalysis::GetConstantTerm(SERecurrentNode* induction) {
  SENode* offset = induction->GetOffset();
  SENode* lower_bound = GetLowerBound();
  if (!offset || !lower_bound) {
    return nullptr;
  }
  SENode* constant_term = scalar_evolution_.SimplifyExpression(
      scalar_evolution_.CreateSubtraction(offset, lower_bound));
  return constant_term;
}

void LoopDependenceAnalysis::PrintDebug(std::string debug_msg) {
  if (debug_stream_) {
    (*debug_stream_) << debug_msg << "\n";
  }
}

}  // namespace opt
}  // namespace spvtools
