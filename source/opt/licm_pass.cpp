// Copyright (c) 2017 Google Inc.
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

#include "licm_pass.h"
#include "cfg.h"
#include "module.h"

#include "pass.h"

namespace spvtools {
namespace opt {

LICMPass::LICMPass(){};

Pass::Status LICMPass::Process(ir::IRContext* context) {
  bool modified = false;

  if (context != nullptr) {
    ir_context = context;
    modified = ProcessIRContext();
  }

  return modified ? Status::SuccessWithChange : Status::SuccessWithoutChange;
}

bool LICMPass::ProcessIRContext() {
  bool modified = false;
  ir::Module* module = ir_context->module();
  ir::CFG cfg(ir_context->module());

  // Process each function in the module
  for (ir::Function& f : *module) {
    modified |= ProcessFunction(&f, cfg);
  }
  return modified;
}

bool LICMPass::ProcessFunction(ir::Function* f, ir::CFG& cfg) {
  bool modified = false;
  ir::LoopDescriptor loop_descriptor{f};

  dom_analysis = ir_context->GetDominatorAnalysis(f, cfg);

  // Process each loop in the function
  for (ir::Loop& loop : loop_descriptor) {
    modified |= ProcessLoop(loop, f);
  }
  return modified;
}

bool LICMPass::ProcessLoop(ir::Loop& loop, ir::Function* f) {
  // Process all nested loops first
  if (loop.HasNestedLoops()) {
    for (ir::Loop*& nested_loop : loop) {
      ProcessLoop(*nested_loop, f);
    }
  }

  ir::InstructionList invariants_list{};
  if (FindLoopInvariants(loop, &invariants_list)) {
    ir::BasicBlock* pre_header = loop.GetPreHeaderBlock();
    // Insert the new list of invariants into the pre_header block
    return HoistInstructions(pre_header, &invariants_list);
  }
  return false;
}

bool LICMPass::HoistInstructions(ir::BasicBlock* pre_header_bb,
                                 ir::InstructionList* invariants_list) {
  if (pre_header_bb == nullptr || invariants_list == nullptr) {
    return false;
  }

  // Get preheader branch instruction
  auto pre_header_branch_inst_it = --pre_header_bb->end();

  pre_header_branch_inst_it.MoveBefore(invariants_list);

  return true;
}

std::vector<ir::BasicBlock*> LICMPass::FindValidBasicBlocks(ir::Loop& loop) {
  std::vector<ir::BasicBlock*> blocks = {};
  std::vector<ir::BasicBlock*> nested_blocks = FindAllNestedBasicBlocks(loop);

  opt::DominatorTree& tree = dom_analysis->GetDomTree();

  // Find every basic block in the loop, excluding the header, merge, and blocks
  // belonging to a nested loop
  auto begin_it = tree.get_iterator(loop.GetHeaderBlock());
  for (; begin_it != tree.end(); ++begin_it) {
    ir::BasicBlock* cur_block = begin_it->bb_;
    if (cur_block == loop.GetHeaderBlock()) continue;
    if (dom_analysis->Dominates(loop.GetMergeBlock(), cur_block)) continue;

    // Check block is not nested within another loop
    bool nested = false;
    for (auto nested_it = nested_blocks.begin();
         nested_it != nested_blocks.end(); ++nested_it) {
      if (cur_block == *nested_it) {
        nested = true;
        break;
      }
    }

    if (!nested) blocks.push_back(cur_block);
  }
  return blocks;
}

std::vector<ir::BasicBlock*> LICMPass::FindAllLoopBlocks(ir::Loop* loop) {
  std::vector<ir::BasicBlock*> blocks = {};

  opt::DominatorTree& tree = dom_analysis->GetDomTree();

  // Every block dominated by the header but not by the merge block of the loop
  auto begin_it = tree.get_iterator(loop->GetHeaderBlock());
  for (; begin_it != tree.end(); ++begin_it) {
    ir::BasicBlock* cur_block = begin_it->bb_;
    if (dom_analysis->Dominates(loop->GetMergeBlock(), cur_block)) break;

    blocks.push_back(cur_block);
  }
  return blocks;
}

std::vector<ir::BasicBlock*> LICMPass::FindAllNestedBasicBlocks(
    ir::Loop& loop) {
  std::vector<ir::BasicBlock*> blocks = {};

  opt::DominatorTree& tree = dom_analysis->GetDomTree();

  if (loop.HasNestedLoops()) {
    // Go through each nested loop
    for (ir::Loop* nested_loop : loop) {
      // Test the blocks of the nested loop against the dominator tree
      auto tree_it = tree.get_iterator(nested_loop->GetHeaderBlock());
      for (; tree_it != tree.end(); ++tree_it) {
        if (dom_analysis->Dominates(nested_loop->GetMergeBlock(), tree_it->bb_))
          break;
        blocks.push_back(tree_it->bb_);
      }

      // Add the header and merge blocks, as they won't be caught in the above
      // loop
      blocks.push_back(nested_loop->GetHeaderBlock());
      blocks.push_back(nested_loop->GetMergeBlock());
    }
  }

  return blocks;
}

bool LICMPass::FindLoopInvariants(ir::Loop& loop,
                                  ir::InstructionList* invariants_list) {
  std::map<ir::Instruction*, bool> invariants_map{};
  std::vector<ir::Instruction*> invars{};

  // There are some initial variants from the loop
  // The loop header OpLabel and OpBranch
  invariants_map.emplace(&*loop.GetHeaderBlock()->begin(), false);
  invariants_map.emplace(&*loop.GetHeaderBlock()->tail(), false);
  // The loop latch OpBranch
  invariants_map.emplace(&*loop.GetLatchBlock()->tail(), false);
  // The loop end of the loop body OpBranch to latch
  ir::Instruction* branch_to_latch_inst =
      &*dom_analysis->ImmediateDominator(loop.GetLatchBlock()->id())->tail();
  invariants_map.emplace(branch_to_latch_inst, false);
  // The loop condition OpBranch
  ir::BasicBlock* loop_condition_bb = ir_context->get_instr_block(
      (loop.GetHeaderBlock()->tail())->begin()->words.front());
  invariants_map.emplace(&*loop_condition_bb->tail(), false);

  std::vector<ir::BasicBlock*> valid_blocks = FindValidBasicBlocks(loop);
  for (ir::BasicBlock* block : valid_blocks) {
    for (ir::Instruction& inst : *block) {
      if (invariants_map.find(&inst) == invariants_map.end()) {
        if (IsInvariant(loop, &invariants_map, &inst, 0)) {
          invariants_map.emplace(std::make_pair(&inst, true));
          invars.push_back(&inst);
        } else {
          invariants_map.emplace(std::make_pair(&inst, false));
        }
      }
    }
  }

  for (auto invar_it = invars.begin(); invar_it != invars.end(); ++invar_it) {
    invariants_list->push_back(std::unique_ptr<ir::Instruction>(*invar_it));
  }

  // Return if there were invariants found
  return invariants_list->begin() != invariants_list->end();
}

bool LICMPass::IsInvariant(ir::Loop& loop,
                           std::map<ir::Instruction*, bool>* invariants_map,
                           ir::Instruction* inst, const uint32_t ignore_id) {
  // The following always are or are not invariant
  // TODO(Alexander) OpStore is invariant iff
  // the stored value is invariant wrt the loop
  // if the value is the result of another operation it must
  // be created from other invariants wrt the loop.
  // If a variable is stored to with a value whos creation involved the variable
  // that store is variant, i.e. a += 1 == a = a + 1
  // TODO(Alexander) Branches to the loop header, continue, merge
  switch (inst->opcode()) {
    // Barriers are variant
    case SpvOpControlBarrier:
    case SpvOpMemoryBarrier:
    case SpvOpMemoryNamedBarrier:
    // Phi instructions are variant
    case SpvOpPhi:
    // Function calls are variant
    case SpvOpFunctionCall:
    // Nops can't be moved
    case SpvOpNop:
      invariants_map->emplace(std::make_pair(inst, false));
      return false;
    // Type definitions and OpName are invariant
    case SpvOpTypeVoid:
    case SpvOpTypeBool:
    case SpvOpTypeInt:
    case SpvOpTypeFloat:
    case SpvOpTypeVector:
    case SpvOpTypeMatrix:
    case SpvOpTypeImage:
    case SpvOpTypeSampler:
    case SpvOpTypeSampledImage:
    case SpvOpTypeArray:
    case SpvOpTypeRuntimeArray:
    case SpvOpTypeStruct:
    case SpvOpTypeOpaque:
    case SpvOpTypePointer:
    case SpvOpTypeFunction:
    case SpvOpTypeEvent:
    case SpvOpTypeDeviceEvent:
    case SpvOpTypeReserveId:
    case SpvOpTypeQueue:
    case SpvOpTypePipe:
    case SpvOpTypeForwardPointer:
    case SpvOpConstantTrue:
    case SpvOpConstantFalse:
    case SpvOpConstant:
    case SpvOpName:
      invariants_map->emplace(std::make_pair(inst, true));
      return true;
    default:
      break;
  }

  // Check if this instruction has already been calculated
  auto map_val = invariants_map->find(inst);
  if (map_val != invariants_map->end()) {
    return map_val->second;
  }

  // Recurse though all instructions leading this instruction. If any of them is
  // variant wrt the loop, all instructions using that instruction are variant.
  bool invariant = true;
  for (ir::Operand& operand : *inst) {
    switch (operand.type) {
      // These operand types do not lead to further instructions which may be
      // variant
      case SPV_OPERAND_TYPE_RESULT_ID:
      case SPV_OPERAND_TYPE_LITERAL_INTEGER:
      case SPV_OPERAND_TYPE_STORAGE_CLASS:
        break;
      default:
        uint32_t operand_id = operand.words.front();
        ir::Instruction* next_inst =
            ir_context->get_def_use_mgr()->GetDef(operand_id);
        // If we are at an OpStore, we should ignore this store when searching
        // later uses, so we provide the instructions unique_id to avoid
        // finding ourselves in a loop when searching uses of the instruction
        // later
        switch (inst->opcode()) {
          case SpvOpStore:
            invariant &=
                IsInvariant(loop, invariants_map, next_inst, inst->unique_id());
            break;
          // Stops infinite looping when a variable is redeclared
          case SpvOpVariable:
            if (*--operand.words.end() == inst->result_id()) {
              break;
            }
          default:
            invariant &= IsInvariant(loop, invariants_map, next_inst, 0);
        }
    }
  }

  // If this instruction has no invariants leading to it wrt to the loop, we
  // must now look at it's uses within the loop to find it is invariant
  std::vector<ir::Instruction*> using_insts = {};

  std::function<void(ir::Instruction*)> collect_users =
      [this, &loop, &using_insts, &collect_users](ir::Instruction* user) {
        if (loop.IsInsideLoop(user)) {
          using_insts.push_back(user);
          if (user->result_id() != 0) {
            ir_context->get_def_use_mgr()->ForEachUser(user, collect_users);
          }
        }
      };

  if (inst->result_id() != 0) {
    ir_context->get_def_use_mgr()->ForEachUser(inst, collect_users);
  }

  for (ir::Instruction* user : using_insts) {
    if (user->opcode() == SpvOpStore &&
        user->begin()->words.front() == inst->result_id() &&
        user->unique_id() != ignore_id) {
      invariants_map->emplace(std::make_pair(user, false));
      invariant = false;
    }
  }

  // The instructions has been proved variant or invariant, so cache the result
  invariants_map->emplace(std::make_pair(inst, invariant));

  return invariant;
}

}  // namespace opt
}  // namespace spvtools
