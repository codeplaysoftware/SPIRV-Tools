// Copyright (c) 2018 Google Inc.
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

  // Process each function in the module
  for (ir::Function& f : *module) {
    modified |= ProcessFunction(&f);
  }
  return modified;
}

bool LICMPass::ProcessFunction(ir::Function* f) {
  bool modified = false;
  ir::LoopDescriptor loop_descriptor{f};

  // dom_analysis = ir_context->GetDominatorAnalysis(f, cfg);

  // Process each loop in the function
  for (ir::Loop& loop : loop_descriptor) {
    modified |= ProcessLoop(&loop, f);
  }
  return modified;
}

bool LICMPass::ProcessLoop(ir::Loop* loop, ir::Function* f) {
  // Process all nested loops first
  for (ir::Loop* nested_loop : *loop) {
    ProcessLoop(nested_loop, f);
  }

  std::queue<ir::Instruction*> loop_iv_instr{};
  GatherAllImmediatelyInvariantInstructions(loop, &loop_iv_instr);

  return ProcessInstructionList(loop, &loop_iv_instr);
}

void LICMPass::GatherAllImmediatelyInvariantInstructions(
    ir::Loop* loop, std::queue<ir::Instruction*>* loop_iv_instr) {
  for (uint32_t bb_id : loop->GetBlocks()) {
    ir::BasicBlock* bb = ir_context->get_instr_block(bb_id);
    for (ir::Instruction& inst : *bb) {
      if (!inst.HasSideEffects() && AllOperandsOutsideLoop(loop, &inst)) {
        loop_iv_instr->push(&inst);
      }
    }
  }
}

void LICMPass::HoistInstruction(ir::BasicBlock* pre_header_bb,
                                ir::Instruction* inst) {
  pre_header_bb->tail()->InsertBefore(std::move(inst));
  ir_context->set_instr_block(inst, pre_header_bb);
}

bool LICMPass::AllOperandsOutsideLoop(ir::Loop* loop, ir::Instruction* inst) {
  analysis::DefUseManager* def_use_mgr = ir_context->get_def_use_mgr();
  bool all_outside_loop = true;

  const std::function<void(uint32_t*)> operand_test_lambda =
      [&def_use_mgr, &loop, &all_outside_loop](uint32_t* id) {
        if (loop->IsInsideLoop(def_use_mgr->GetDef(*id))) {
          all_outside_loop = false;
        }
      };

  inst->ForEachInId(operand_test_lambda);
  return all_outside_loop;
}

bool LICMPass::ProcessInstructionList(
    ir::Loop* loop, std::queue<ir::Instruction*>* loop_iv_instr) {
  if (loop_iv_instr->empty()) {
    return false;
  }

  ir::BasicBlock* pre_header_bb = loop->GetPreHeaderBlock();

  const std::function<void(ir::Instruction*)> check_users_now_invariant =
      [this, &loop, &loop_iv_instr](ir::Instruction* inst) {
        if (!inst->HasSideEffects() && AllOperandsOutsideLoop(loop, inst)) {
          loop_iv_instr->push(inst);
        }
      };

  while (!loop_iv_instr->empty()) {
    ir::Instruction* inst = loop_iv_instr->front();
    loop_iv_instr->pop();
    HoistInstruction(pre_header_bb, inst);
    ir_context->get_def_use_mgr()->ForEachUser(inst, check_users_now_invariant);
  }

  return true;
}

}  // namespace opt
}  // namespace spvtools
