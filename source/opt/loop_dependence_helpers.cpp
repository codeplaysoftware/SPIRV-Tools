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
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "opt/basic_block.h"
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

SENode* LoopDependenceAnalysis::GetLowerBound(const ir::Loop* loop) {
  ir::Instruction* cond_inst = loop->GetConditionInst();
  if (!cond_inst) {
    return nullptr;
  }
  ir::Instruction* lower_inst = GetOperandDefinition(cond_inst, 0);
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
        lower_inst = GetOperandDefinition(lower_inst, 0);
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

// TODO(Alexander): Perhaps the -1 and +1 should actually the loop step?
SENode* LoopDependenceAnalysis::GetUpperBound(const ir::Loop* loop) {
  ir::Instruction* cond_inst = loop->GetConditionInst();
  if (!cond_inst) {
    return nullptr;
  }
  ir::Instruction* upper_inst = GetOperandDefinition(cond_inst, 1);
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

bool LoopDependenceAnalysis::IsProvablyOutwithLoopBounds(const ir::Loop* loop,
                                                         SENode* distance,
                                                         SENode* coefficient) {
  // We test to see if we can reduce the coefficient to an integral constant.
  SEConstantNode* coefficient_constant = coefficient->AsSEConstantNode();
  if (!coefficient_constant) {
    PrintDebug(
        "IsProvablyOutwithLoopBounds could not reduce coefficient to a "
        "SEConstantNode so must exit.");
    return false;
  }

  SENode* lower_bound = GetLowerBound(loop);
  SENode* upper_bound = GetUpperBound(loop);
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
        ToString(distance_minus_bounds->FoldToSingleValue()));
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

const ir::Loop* LoopDependenceAnalysis::GetLoopForSubscriptPair(
    std::pair<SENode*, SENode*>* subscript_pair) {
  // Collect all the SERecurrentNodes.
  std::vector<SERecurrentNode*> source_nodes =
      std::get<0>(*subscript_pair)->CollectRecurrentNodes();
  std::vector<SERecurrentNode*> destination_nodes =
      std::get<1>(*subscript_pair)->CollectRecurrentNodes();

  // Collect all the loops stored by the SERecurrentNodes.
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

  // If we didn't find 1 loop |subscript_pair| is a subscript over multiple or 0
  // loops. We don't handle this so return nullptr.
  if (loops.size() != 1) {
    PrintDebug("GetLoopForSubscriptPair found loops.size() != 1.");
    return nullptr;
  }
  return *loops.begin();
}

DistanceEntry* LoopDependenceAnalysis::GetDistanceEntryForLoop(
    const ir::Loop* loop, DistanceVector* distance_vector) {
  if (!loop) {
    return nullptr;
  }

  DistanceEntry* distance_entry = nullptr;
  for (size_t loop_index = 0; loop_index < loops_.size(); ++loop_index) {
    if (loop == loops_[loop_index]) {
      distance_entry = &distance_vector->entries[loop_index];
      break;
    }
  }

  return distance_entry;
}

DistanceEntry* LoopDependenceAnalysis::GetDistanceEntryForSubscriptPair(
    std::pair<SENode*, SENode*>* subscript_pair,
    DistanceVector* distance_vector) {
  const ir::Loop* loop = GetLoopForSubscriptPair(subscript_pair);

  return GetDistanceEntryForLoop(loop, distance_vector);
}

SENode* LoopDependenceAnalysis::GetTripCount(const ir::Loop* loop) {
  ir::BasicBlock* condition_block = loop->FindConditionBlock();
  if (!condition_block) {
    return nullptr;
  }
  ir::Instruction* induction_instr =
      loop->FindConditionVariable(condition_block);
  if (!induction_instr) {
    return nullptr;
  }
  ir::Instruction* cond_instr = loop->GetConditionInst();
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
      if (loop->FindNumberOfIterations(
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

SENode* LoopDependenceAnalysis::GetFirstTripInductionNode(
    const ir::Loop* loop) {
  ir::BasicBlock* condition_block = loop->FindConditionBlock();
  if (!condition_block) {
    return nullptr;
  }
  ir::Instruction* induction_instr =
      loop->FindConditionVariable(condition_block);
  if (!induction_instr) {
    return nullptr;
  }
  int64_t induction_initial_value = 0;
  if (!loop->GetInductionInitValue(induction_instr, &induction_initial_value)) {
    return nullptr;
  }

  SENode* induction_init_SENode = scalar_evolution_.SimplifyExpression(
      scalar_evolution_.CreateConstant(induction_initial_value));
  return induction_init_SENode;
}

SENode* LoopDependenceAnalysis::GetFinalTripInductionNode(
    const ir::Loop* loop, SENode* induction_coefficient) {
  SENode* first_trip_induction_node = GetFirstTripInductionNode(loop);
  if (!first_trip_induction_node) {
    return nullptr;
  }
  // Get trip_count as GetTripCount - 1
  // This is because the induction variable is not stepped on the first
  // iteration of the loop
  SENode* trip_count =
      scalar_evolution_.SimplifyExpression(scalar_evolution_.CreateSubtraction(
          GetTripCount(loop), scalar_evolution_.CreateConstant(1)));
  // Return first_trip_induction_node + trip_count * induction_coefficient
  return scalar_evolution_.SimplifyExpression(scalar_evolution_.CreateAddNode(
      first_trip_induction_node,
      scalar_evolution_.CreateMultiplyNode(trip_count, induction_coefficient)));
}

std::set<const ir::Loop*> LoopDependenceAnalysis::CollectLoops(
    const std::vector<SERecurrentNode*>& recurrent_nodes) {
  // We don't handle loops with more than one induction variable. Therefore we
  // can identify the number of induction variables by collecting all of the
  // loops the collected recurrent nodes belong to.
  std::set<const ir::Loop*> loops{};
  for (auto recurrent_nodes_it = recurrent_nodes.begin();
       recurrent_nodes_it != recurrent_nodes.end(); ++recurrent_nodes_it) {
    loops.insert((*recurrent_nodes_it)->GetLoop());
  }

  return loops;
}

int64_t LoopDependenceAnalysis::CountInductionVariables(SENode* node) {
  if (!node) {
    return -1;
  }

  std::vector<SERecurrentNode*> recurrent_nodes = node->CollectRecurrentNodes();

  // We don't handle loops with more than one induction variable. Therefore we
  // can identify the number of induction variables by collecting all of the
  // loops the collected recurrent nodes belong to.
  std::set<const ir::Loop*> loops = CollectLoops(recurrent_nodes);

  return static_cast<int64_t>(loops.size());
}

std::set<const ir::Loop*> LoopDependenceAnalysis::CollectLoops(
    SENode* source, SENode* destination) {
  if (!source || !destination) {
    return {};
  }

  std::vector<SERecurrentNode*> source_nodes = source->CollectRecurrentNodes();
  std::vector<SERecurrentNode*> destination_nodes =
      destination->CollectRecurrentNodes();

  std::set<const ir::Loop*> loops = CollectLoops(source_nodes);
  std::set<const ir::Loop*> destination_loops = CollectLoops(destination_nodes);

  loops.insert(std::begin(destination_loops), std::end(destination_loops));

  return loops;
}

int64_t LoopDependenceAnalysis::CountInductionVariables(SENode* source,
                                                        SENode* destination) {

  if (!source || !destination) {
    return -1;
  }

  std::set<const ir::Loop*> loops = CollectLoops(source, destination);

  return static_cast<int64_t>(loops.size());
}

ir::Instruction* LoopDependenceAnalysis::GetOperandDefinition(
    const ir::Instruction* instruction, int id) {
  return context_->get_def_use_mgr()->GetDef(
      instruction->GetSingleWordInOperand(id));
}

std::vector<ir::Instruction*> LoopDependenceAnalysis::GetSubscripts(
    const ir::Instruction* instruction) {
  ir::Instruction* access_chain = GetOperandDefinition(instruction, 0);

  std::vector<ir::Instruction*> subscripts;

  for (auto i = 1u; i < access_chain->NumInOperandWords(); ++i) {
    subscripts.push_back(GetOperandDefinition(access_chain, i));
  }

  return subscripts;
}

SENode* LoopDependenceAnalysis::GetConstantTerm(const ir::Loop* loop,
                                                SERecurrentNode* induction) {
  SENode* offset = induction->GetOffset();
  SENode* lower_bound = GetLowerBound(loop);
  if (!offset || !lower_bound) {
    return nullptr;
  }
  SENode* constant_term = scalar_evolution_.SimplifyExpression(
      scalar_evolution_.CreateSubtraction(offset, lower_bound));
  return constant_term;
}

bool LoopDependenceAnalysis::CheckSupportedLoops(
    std::vector<const ir::Loop*> loops) {
  for (auto loop : loops) {
    if (!IsSupportedLoop(loop)) {
      return false;
    }
  }
  return true;
}

bool LoopDependenceAnalysis::IsSupportedLoop(const ir::Loop* loop) {
  std::vector<ir::Instruction*> inductions{};
  loop->GetInductionVariables(inductions);
  if (inductions.size() != 1) {
    return false;
  }
  ir::Instruction* induction = inductions[0];
  SENode* induction_node = scalar_evolution_.SimplifyExpression(
      scalar_evolution_.AnalyzeInstruction(induction));
  if (!induction_node->AsSERecurrentNode()) {
    return false;
  }
  SENode* induction_step =
      induction_node->AsSERecurrentNode()->GetCoefficient();
  if (!induction_step->AsSEConstantNode()) {
    return false;
  }
  if (!(induction_step->AsSEConstantNode()->FoldToSingleValue() == 1 ||
        induction_step->AsSEConstantNode()->FoldToSingleValue() == -1)) {
    return false;
  }
  return true;
}

void LoopDependenceAnalysis::PrintDebug(std::string debug_msg) {
  if (debug_stream_) {
    (*debug_stream_) << debug_msg << "\n";
  }
}

}  // namespace opt
}  // namespace spvtools
