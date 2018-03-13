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

SENode* LoopDependenceAnalysis::GetLowerBound() {
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
        // We don't handle looking through multiple phis.
        if (lower_inst->opcode() == SpvOpPhi) {
          return nullptr;
        }
      }
      SENode* lower_bound = scalar_evolution_.SimplifyExpression(
          scalar_evolution_.AnalyzeInstruction(lower_inst));
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
        // We don't handle looking through multiple phis.
        if (lower_inst->opcode() == SpvOpPhi) {
          return nullptr;
        }
      }
      SENode* lower_bound = scalar_evolution_.SimplifyExpression(
          scalar_evolution_.AnalyzeInstruction(lower_inst));
      if (lower_bound) {
        return scalar_evolution_.SimplifyExpression(
            scalar_evolution_.CreateAddNode(
                lower_bound, scalar_evolution_.CreateConstant(1)));
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
        // We don't handle looking through multiple phis.
        if (lower_inst->opcode() == SpvOpPhi) {
          return nullptr;
        }
      }
      SENode* lower_bound = scalar_evolution_.SimplifyExpression(
          scalar_evolution_.AnalyzeInstruction(lower_inst));
      return lower_bound;
    } break;
    default:
      return nullptr;
  }
  return nullptr;
}

SENode* LoopDependenceAnalysis::GetUpperBound() {
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
        // We don't handle looking through multiple phis.
        if (upper_inst->opcode() == SpvOpPhi) {
          return nullptr;
        }
      }
      SENode* upper_bound = scalar_evolution_.SimplifyExpression(
          scalar_evolution_.AnalyzeInstruction(upper_inst));
      if (upper_bound) {
        return scalar_evolution_.SimplifyExpression(
            scalar_evolution_.CreateSubtraction(
                upper_bound, scalar_evolution_.CreateConstant(1)));
      }
    }
    case SpvOpULessThanEqual:
    case SpvOpSLessThanEqual: {
      ir::Instruction* upper_inst = context_->get_def_use_mgr()->GetDef(
          cond_inst->GetSingleWordInOperand(1));
      if (upper_inst->opcode() == SpvOpPhi) {
        upper_inst = context_->get_def_use_mgr()->GetDef(
            upper_inst->GetSingleWordInOperand(0));
        // We don't handle looking through multiple phis.
        if (upper_inst->opcode() == SpvOpPhi) {
          return nullptr;
        }
      }
      SENode* upper_bound = scalar_evolution_.SimplifyExpression(
          scalar_evolution_.AnalyzeInstruction(upper_inst));
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
        // We don't handle looking through multiple phis.
        if (upper_inst->opcode() == SpvOpPhi) {
          return nullptr;
        }
      }
      SENode* upper_bound = scalar_evolution_.SimplifyExpression(
          scalar_evolution_.AnalyzeInstruction(upper_inst));
      return upper_bound;
      break;
    }
    default:
      return nullptr;
  }
  return nullptr;
}

std::pair<SENode*, SENode*> LoopDependenceAnalysis::GetLoopLowerUpperBounds() {
  SENode* lower_bound_SENode = GetLowerBound();
  SENode* upper_bound_SENode = GetUpperBound();

  return std::make_pair(lower_bound_SENode, upper_bound_SENode);
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

bool LoopDependenceAnalysis::IsProvablyOutwithLoopBounds(SENode* distance) {
  SENode* lower_bound = GetLowerBound();
  SENode* upper_bound = GetUpperBound();
  if (!lower_bound || !upper_bound) {
    return false;
  }
  // We can attempt to deal with symbolic cases by subtracting |distance| and
  // the bound nodes. If we can subtract, simplify and produce a SEConstantNode
  // we
  // can produce some information.
  SENode* bounds = scalar_evolution_.SimplifyExpression(
      scalar_evolution_.CreateSubtraction(upper_bound, lower_bound));

  SEConstantNode* distance_minus_bounds =
      scalar_evolution_
          .SimplifyExpression(
              scalar_evolution_.CreateSubtraction(distance, bounds))
          ->AsSEConstantNode();
  if (distance_minus_bounds) {
    // If distance - bounds > 0 we prove the distance is outwith the loop
    // bounds.
    if (distance_minus_bounds->FoldToSingleValue() > 0) {
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
      source->CollectRecurrentNodes();

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
