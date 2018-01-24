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
    modified |= ProcessLoop(loop, f);
  }
  return modified;
}

bool LICMPass::ProcessLoop(ir::Loop& loop, ir::Function* f) {
  // Process all nested loops first
  for (ir::Loop* nested_loop : loop) {
    ProcessLoop(*nested_loop, f);
  }

  std::vector<ir::Instruction*> instructions{};
  GatherAllLoopInstructions(loop, &instructions);

  return ProcessInstructionList(loop, &instructions);
}

void LICMPass::GatherAllLoopInstructions(
    ir::Loop& loop, std::vector<ir::Instruction*>* instructions) {
  for (uint32_t bb_id : loop.GetBlocks()) {
    ir::BasicBlock* bb = ir_context->get_instr_block(bb_id);
    for (ir::Instruction& inst : *bb) {
      if (!DoesOpcodeHaveSideEffects(inst.opcode())) {
        instructions->push_back(&inst);
      }
    }
  }
}

void LICMPass::HoistInstruction(ir::BasicBlock* pre_header_bb,
                                ir::Instruction* inst) {
  ir::InstructionList inst_list{};
  inst_list.push_back(std::unique_ptr<ir::Instruction>(inst));
  auto pre_header_branch_inst_it = pre_header_bb->tail();

  pre_header_branch_inst_it.MoveBefore(&inst_list);
  ir_context->set_instr_block(inst, pre_header_bb);
}

bool LICMPass::AllOperandsOutsideLoop(ir::Loop& loop,
                                         ir::Instruction* inst) {
  for (ir::Operand& operand : *inst) {
    if (operand.type == SPV_OPERAND_TYPE_ID) {
      if (loop.IsInsideLoop(
              ir_context->get_def_use_mgr()->GetDef(operand.words.front()))) {
        return false;
      }
    }
  }
  return true;
}

bool LICMPass::ProcessInstructionList(
    ir::Loop& loop, std::vector<ir::Instruction*>* instructions) {
  std::queue<ir::Instruction*> instruction_queue{};
  ir::BasicBlock* pre_header_bb = loop.GetPreHeaderBlock();
  bool list_exhausted = false;
  bool modified = false;

  if (pre_header_bb == nullptr) {
    return false;
  }

  while (!list_exhausted) {
    list_exhausted = true;

    // Collect all currently invariant instructions
    for (ir::Instruction* inst : *instructions) {
      if (AllOperandsOutsideLoop(loop, inst)) {
        instruction_queue.push(inst);
      }
    }

    // Check if we found any invariants
    if (!instruction_queue.empty()) {
      modified = true;
      list_exhausted = false;
    }

    // Hoist all invariants
    while (!instruction_queue.empty()) {
      HoistInstruction(pre_header_bb, instruction_queue.front());
      instruction_queue.pop();
    }
  }

  return modified;
}

bool LICMPass::DoesOpcodeHaveSideEffects(SpvOp opcode) {
  switch (opcode) {
    // We do not check side effects from function calls, so they must be marked
    // as variant
    case SpvOpFunctionCall:
    // Loads and stores present after multiple store elimination have side
    // effects
    case SpvOpStore:
    case SpvOpLoad:
    // Any control flow is marked as having side effects
    case SpvOpBranch:
    case SpvOpBranchConditional:
    case SpvOpSelectionMerge:
    case SpvOpLoopMerge:
    case SpvOpLabel:
    // We do not check side effects from barriers, so they must be marked as
    // variant
    case SpvOpControlBarrier:
    case SpvOpMemoryBarrier:
    case SpvOpMemoryNamedBarrier:
    case SpvOpTypeNamedBarrier:
    case SpvOpNamedBarrierInitialize:
    // We do not check side effects from atomics, so they must be marked as
    // variant
    case SpvOpAtomicLoad:
    case SpvOpAtomicStore:
    case SpvOpAtomicExchange:
    case SpvOpAtomicCompareExchange:
    case SpvOpAtomicCompareExchangeWeak:
    case SpvOpAtomicIIncrement:
    case SpvOpAtomicIDecrement:
    case SpvOpAtomicIAdd:
    case SpvOpAtomicISub:
    case SpvOpAtomicSMin:
    case SpvOpAtomicUMin:
    case SpvOpAtomicSMax:
    case SpvOpAtomicUMax:
    case SpvOpAtomicAnd:
    case SpvOpAtomicOr:
    case SpvOpAtomicXor:
    case SpvOpAtomicFlagTestAndSet:
    case SpvOpAtomicFlagClear:
    // We do not check side effects from Phi, so they must be marked as variant
    case SpvOpPhi:
    // Unreachables can not be moved, so must be marked variant
    case SpvOpUnreachable:
    // Nops can't be moved
    case SpvOpNop:
    // We don't deal with extension specific Ops
    case SpvOpSubgroupBallotKHR:
    case SpvOpSubgroupFirstInvocationKHR:
    case SpvOpSubgroupAllKHR:
    case SpvOpSubgroupAnyKHR:
    case SpvOpSubgroupAllEqualKHR:
    case SpvOpSubgroupReadInvocationKHR:
    case SpvOpGroupIAddNonUniformAMD:
    case SpvOpGroupFAddNonUniformAMD:
    case SpvOpGroupFMinNonUniformAMD:
    case SpvOpGroupUMinNonUniformAMD:
    case SpvOpGroupSMinNonUniformAMD:
    case SpvOpGroupFMaxNonUniformAMD:
    case SpvOpGroupUMaxNonUniformAMD:
    case SpvOpGroupSMaxNonUniformAMD:
    case SpvOpFragmentMaskFetchAMD:
    case SpvOpFragmentFetchAMD:
    case SpvOpSubgroupShuffleINTEL:
    case SpvOpSubgroupShuffleDownINTEL:
    case SpvOpSubgroupShuffleUpINTEL:
    case SpvOpSubgroupShuffleXorINTEL:
    case SpvOpSubgroupBlockReadINTEL:
    case SpvOpSubgroupBlockWriteINTEL:
    case SpvOpSubgroupImageBlockReadINTEL:
    case SpvOpSubgroupImageBlockWriteINTEL:
    // We don't currently deal with instructions which require capabilities
    // Requires capability pipes
    case SpvOpReadPipe:
    case SpvOpWritePipe:
    case SpvOpReservedReadPipe:
    case SpvOpReservedWritePipe:
    case SpvOpReserveReadPipePackets:
    case SpvOpReserveWritePipePackets:
    case SpvOpCommitReadPipe:
    case SpvOpCommitWritePipe:
    case SpvOpIsValidReserveId:
    case SpvOpGetNumPipePackets:
    case SpvOpGetMaxPipePackets:
    case SpvOpGroupReserveReadPipePackets:
    case SpvOpGroupReserveWritePipePackets:
    case SpvOpGroupCommitReadPipe:
    case SpvOpGroupCommitWritePipe:
    // Requires capability PipeStorage
    case SpvOpTypePipeStorage:
    case SpvOpConstantPipeStorage:
    case SpvOpCreatePipeFromPipeStorage:
    // Requires capability DeviceEnqueue
    case SpvOpEnqueueMarker:
    case SpvOpEnqueueKernel:
    case SpvOpGetKernelNDrangeSubGroupCount:
    case SpvOpGetKernelNDrangeMaxSubGroupSize:
    case SpvOpGetKernelWorkGroupSize:
    case SpvOpGetKernelPreferredWorkGroupSizeMultiple:
    case SpvOpRetainEvent:
    case SpvOpReleaseEvent:
    case SpvOpCreateUserEvent:
    case SpvOpIsValidEvent:
    case SpvOpSetUserEventStatus:
    case SpvOpCaptureEventProfilingInfo:
    case SpvOpGetDefaultQueue:
    case SpvOpBuildNDRange:
    // Requires capability SparseResidency
    case SpvOpImageSparseSampleImplicitLod:
    case SpvOpImageSparseSampleExplicitLod:
    case SpvOpImageSparseSampleDrefImplicitLod:
    case SpvOpImageSparseSampleDrefExplicitLod:
    case SpvOpImageSparseSampleProjImplicitLod:
    case SpvOpImageSparseSampleProjExplicitLod:
    case SpvOpImageSparseSampleProjDrefImplicitLod:
    case SpvOpImageSparseSampleProjDrefExplicitLod:
    case SpvOpImageSparseFetch:
    case SpvOpImageSparseGather:
    case SpvOpImageSparseDrefGather:
    case SpvOpImageSparseTexelsResident:
    case SpvOpImageSparseRead:
    // Requires capability Shader
    case SpvOpBitFieldInsert:
    case SpvOpBitFieldSExtract:
    case SpvOpBitFieldUExtract:
    case SpvOpBitReverse:
    case SpvOpDPdx:
    case SpvOpDPdy:
    case SpvOpFwidth:
    case SpvOpKill:
    case SpvOpImageSampleImplicitLod:
    case SpvOpImageSampleDrefImplicitLod:
    case SpvOpImageSampleDrefExplicitLod:
    case SpvOpImageSampleProjImplicitLod:
    case SpvOpImageSampleProjExplicitLod:
    case SpvOpImageSampleProjDrefImplicitLod:
    case SpvOpImageSampleProjDrefExplicitLod:
    case SpvOpImageGather:
    case SpvOpImageDrefGather:
    // Requires capability DerivativeControl
    case SpvOpDPdxFine:
    case SpvOpDPdyFine:
    case SpvOpFwidthFine:
    case SpvOpDPdxCoarse:
    case SpvOpDPdyCoarse:
    case SpvOpFwidthCoarse:
    // Requires capability Geometry
    case SpvOpEmitVertex:
    case SpvOpEndPrimitive:
    case SpvOpEmitStreamVertex:
    case SpvOpEndStreamPrimitive:
    // Requires capability SubgroupDispatch
    case SpvOpGetKernelLocalSizeForSubgroupCount:
    case SpvOpGetKernelMaxNumSubgroups:
    // Requires capability Kernel
    case SpvOpGenericPtrMemSemantics:
    case SpvOpImageQueryFormat:
    case SpvOpImageQueryOrder:
    // Requires capability ImageQuery
    case SpvOpImageQueryLod:
    // Requires capabilities Kernel, ImageQuery
    case SpvOpImageQuerySizeLod:
    case SpvOpImageQuerySize:
    case SpvOpImageQueryLevels:
    case SpvOpImageQuerySamples:
    // Requires capability Matrix
    case SpvOpTranspose:
    // While SpvOpMax is not a valid instruction, it must be included in this
    // switch
    case SpvOpMax:
      return true;
    default:
      return false;
  }
}

}  // namespace opt
}  // namespace spvtools
