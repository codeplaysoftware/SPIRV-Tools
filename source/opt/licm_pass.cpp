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
  for (ir::Loop*& nested_loop : loop) {
    ProcessLoop(*nested_loop, f);
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
  // If there is no preheader, we have nowhere to insert the invariants, so
  // can't move them
  if (pre_header_bb == nullptr) {
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

  // Find every basic block in the loop, excluding the header, merge, and any
  // blocks belonging to a nested loop
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
  std::unordered_map<ir::Instruction*, bool> invariants_map{};
  std::vector<ir::Instruction*> invars{};

  // There are some initial variants from the loop
  // The loop header OpLoopMerge and OpBranch
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

  // We also need mark all nested loop instructions as variant wrt this loop
  // to ensure any OpBranch instructions to the nested loops are marked variant
  std::vector<ir::BasicBlock*> nested_blocks = FindAllNestedBasicBlocks(loop);
  for (ir::BasicBlock* nested_block : nested_blocks) {
    invariants_map.emplace(std::make_pair(nested_block->GetLabelInst(), false));
    for (ir::Instruction& nested_inst : *nested_block) {
      invariants_map.emplace(std::make_pair(&nested_inst, false));
    }
  }

  std::vector<ir::BasicBlock*> valid_blocks = FindValidBasicBlocks(loop);
  std::vector<ir::Instruction*> visited_insts{};
  for (ir::BasicBlock* block : valid_blocks) {
    for (ir::Instruction& inst : *block) {
      if (invariants_map.find(&inst) == invariants_map.end()) {
        visited_insts.clear();
        if (IsInvariant(loop, &invariants_map, &inst, &visited_insts, 0)) {
          invariants_map.emplace(std::make_pair(&inst, true));
          invars.push_back(&inst);
        } else {
          invariants_map.emplace(std::make_pair(&inst, false));
        }
      } else {
        if (invariants_map.find(&inst)->second == true) {
          invars.push_back(&inst);
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

bool LICMPass::IsInvariant(
    ir::Loop& loop, std::unordered_map<ir::Instruction*, bool>* invariants_map,
    ir::Instruction* inst, std::vector<ir::Instruction*>* visited_insts,
    const uint32_t ignore_id) {
  // We must create a collection of all instructions visited so far, as to avoid
  // infinite looping when a chain of instructions leads back to it's start.
  // When we find an instruction already in this collection during examination
  // of another instructions uses or users we must not examine it again, as we
  // are already in the process of examining it
  visited_insts->push_back(inst);

  // The following always are or are not invariant
  // TODO(Alexander) OpStore is invariant iff
  // the stored value is invariant wrt the loop
  // if the value is the result of another operation it must
  // be created from other invariants wrt the loop.
  // If a variable is stored to with a value whos creation involved the variable
  // that store is variant, i.e. a += 1 == a = a + 1
  // TODO(Alexander) Branches to the loop header, continue, merge
  switch (inst->opcode()) {
    // We do not check side effects from function calls, so they must be marked
    // as variant
    case SpvOpFunctionCall:
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
      invariants_map->emplace(std::make_pair(inst, false));
      return false;
    // The following are always invariant
    case SpvOpExtension:
    case SpvOpExtInstImport:
    case SpvOpExtInst:
    case SpvOpMemoryModel:
    case SpvOpEntryPoint:
    case SpvOpExecutionMode:
    case SpvOpExecutionModeId:
    case SpvOpCapability:
    case SpvOpModuleProcessed:
    // Type definitions are invariant
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
    // Constants are invariant
    case SpvOpConstantTrue:
    case SpvOpConstantFalse:
    case SpvOpConstant:
    case SpvOpConstantComposite:
    case SpvOpConstantSampler:
    case SpvOpConstantNull:
    case SpvOpSpecConstantTrue:
    case SpvOpSpecConstantFalse:
    case SpvOpSpecConstant:
    case SpvOpSpecConstantComposite:
    case SpvOpSpecConstantOp:
    // The start and end of a function are invariant, as they can not occur
    // inside a loop but may be seen from a chain of instructions
    case SpvOpFunction:
    case SpvOpFunctionEnd:
    // Decorates are invariant
    case SpvOpDecorate:
    case SpvOpMemberDecorate:
    case SpvOpDecorationGroup:
    case SpvOpGroupDecorate:
    case SpvOpGroupMemberDecorate:
    case SpvOpDecorateId:
    // Names are invariant
    case SpvOpName:
    case SpvOpMemberName:
      invariants_map->emplace(std::make_pair(inst, true));
      return true;
    // The following Ops can be proved variant or invariant
    case SpvOpVariable:
    case SpvOpImageTexelPointer:
    case SpvOpLoad:
    case SpvOpStore:
    case SpvOpCopyMemory:
    case SpvOpCopyMemorySized:
    case SpvOpAccessChain:
    case SpvOpInBoundsAccessChain:
    case SpvOpPtrAccessChain:
    case SpvOpArrayLength:
    case SpvOpInBoundsPtrAccessChain:
    // Vectors
    case SpvOpVectorExtractDynamic:
    case SpvOpVectorInsertDynamic:
    case SpvOpVectorShuffle:
    // Composites
    case SpvOpCompositeConstruct:
    case SpvOpCompositeExtract:
    case SpvOpCompositeInsert:
    // Copy
    case SpvOpCopyObject:
    // Images
    case SpvOpSampledImage:
    case SpvOpImageSampleExplicitLod:
    case SpvOpImageFetch:
    case SpvOpImageRead:
    case SpvOpImageWrite:
    case SpvOpImage:
    // Conversions
    case SpvOpConvertFToU:
    case SpvOpConvertFToS:
    case SpvOpConvertSToF:
    case SpvOpConvertUToF:
    case SpvOpUConvert:
    case SpvOpSConvert:
    case SpvOpFConvert:
    case SpvOpQuantizeToF16:
    case SpvOpConvertPtrToU:
    case SpvOpSatConvertSToU:
    case SpvOpSatConvertUToS:
    case SpvOpConvertUToPtr:
    case SpvOpPtrCastToGeneric:
    case SpvOpGenericCastToPtr:
    case SpvOpGenericCastToPtrExplicit:
    case SpvOpBitcast:
    // Arithmetic
    case SpvOpSNegate:
    case SpvOpFNegate:
    case SpvOpIAdd:
    case SpvOpFAdd:
    case SpvOpISub:
    case SpvOpFSub:
    case SpvOpIMul:
    case SpvOpFMul:
    case SpvOpUDiv:
    case SpvOpSDiv:
    case SpvOpFDiv:
    case SpvOpUMod:
    case SpvOpSRem:
    case SpvOpSMod:
    case SpvOpFRem:
    case SpvOpFMod:
    case SpvOpVectorTimesScalar:
    case SpvOpMatrixTimesScalar:
    case SpvOpVectorTimesMatrix:
    case SpvOpMatrixTimesVector:
    case SpvOpMatrixTimesMatrix:
    case SpvOpOuterProduct:
    case SpvOpDot:
    case SpvOpIAddCarry:
    case SpvOpISubBorrow:
    case SpvOpUMulExtended:
    case SpvOpSMulExtended:
    case SpvOpAny:
    case SpvOpAll:
    case SpvOpIsNan:
    case SpvOpIsInf:
    case SpvOpIsFinite:
    case SpvOpIsNormal:
    case SpvOpSignBitSet:
    case SpvOpLessOrGreater:
    case SpvOpOrdered:
    case SpvOpUnordered:
    case SpvOpLogicalEqual:
    case SpvOpLogicalNotEqual:
    case SpvOpLogicalOr:
    case SpvOpLogicalAnd:
    case SpvOpLogicalNot:
    case SpvOpSelect:
    case SpvOpIEqual:
    case SpvOpINotEqual:
    case SpvOpUGreaterThan:
    case SpvOpSGreaterThan:
    case SpvOpUGreaterThanEqual:
    case SpvOpSGreaterThanEqual:
    case SpvOpULessThan:
    case SpvOpSLessThan:
    case SpvOpULessThanEqual:
    case SpvOpSLessThanEqual:
    case SpvOpFOrdEqual:
    case SpvOpFUnordEqual:
    case SpvOpFOrdNotEqual:
    case SpvOpFUnordNotEqual:
    case SpvOpFOrdLessThan:
    case SpvOpFUnordLessThan:
    case SpvOpFOrdGreaterThan:
    case SpvOpFUnordGreaterThan:
    case SpvOpFOrdLessThanEqual:
    case SpvOpFUnordLessThanEqual:
    case SpvOpFOrdGreaterThanEqual:
    case SpvOpFUnordGreaterThanEqual:
    case SpvOpShiftRightLogical:
    case SpvOpShiftRightArithmetic:
    case SpvOpShiftLeftLogical:
    case SpvOpBitwiseOr:
    case SpvOpBitwiseXor:
    case SpvOpBitwiseAnd:
    case SpvOpNot:
    case SpvOpBitCount:
    case SpvOpSizeOf:
    // Some control Ops can be invariant
    case SpvOpLoopMerge:
    case SpvOpSelectionMerge:
    case SpvOpLabel:
    case SpvOpBranch:
    case SpvOpBranchConditional:
    case SpvOpSwitch:
    case SpvOpReturn:
    case SpvOpReturnValue:
    case SpvOpLifetimeStart:
    case SpvOpLifetimeStop:
    case SpvOpGroupAsyncCopy:
    case SpvOpGroupWaitEvents:
    case SpvOpGroupAll:
    case SpvOpGroupAny:
    case SpvOpGroupBroadcast:
    case SpvOpGroupIAdd:
    case SpvOpGroupFAdd:
    case SpvOpGroupFMin:
    case SpvOpGroupUMin:
    case SpvOpGroupSMin:
    case SpvOpGroupFMax:
    case SpvOpGroupUMax:
    case SpvOpGroupSMax:

    case SpvOpUndef:
    case SpvOpSourceContinued:
    case SpvOpSource:
    case SpvOpSourceExtension:
    case SpvOpString:
    case SpvOpLine:
    case SpvOpFunctionParameter:
    case SpvOpNoLine:
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
            if (std::find(visited_insts->begin(), visited_insts->end(),
                          next_inst) == visited_insts->end()) {
              invariant &= IsInvariant(loop, invariants_map, next_inst,
                                       visited_insts, inst->unique_id());
              break;
            }
          // Stops infinite looping when a variable is redeclared
          case SpvOpVariable:
            if (*--operand.words.end() == inst->result_id()) {
              break;
            }
          default:
            if (std::find(visited_insts->begin(), visited_insts->end(),
                          next_inst) == visited_insts->end()) {
              invariant &= IsInvariant(loop, invariants_map, next_inst,
                                       visited_insts, 0);
            }
        }
    }
  }

  // If this instruction has no invariants leading to it wrt to the loop, we
  // must now look at it's uses within the loop to find it is invariant
  std::vector<ir::Instruction*> users = {};

  std::function<void(ir::Instruction*)> collect_users =
      [this, &loop, &users, &collect_users](ir::Instruction* user) {
        if (loop.IsInsideLoop(user)) {
          users.push_back(user);
          if (user->result_id() != 0) {
            ir_context->get_def_use_mgr()->ForEachUser(user, collect_users);
          }
        }
      };

  if (inst->result_id() != 0) {
    ir_context->get_def_use_mgr()->ForEachUser(inst, collect_users);
  }

  for (ir::Instruction* user : users) {
    // Check if we restore the value we are using in the loop.
    if (user->opcode() == SpvOpStore &&
        user->begin()->words.front() == inst->result_id() &&
        user->unique_id() != ignore_id) {
      invariants_map->emplace(std::make_pair(user, false));
      invariant = false;
    }
    // Check if the current instruction is used by a variant.
    if (std::find(visited_insts->begin(), visited_insts->end(), user) ==
        visited_insts->end()) {
      bool is_user_variant =
          !IsInvariant(loop, invariants_map, user, visited_insts, ignore_id);
      if (loop.IsInsideLoop(user) && is_user_variant) {
        invariant = false;
        break;
      }
    }
  }

  // The instructions has been proved variant or invariant, so cache the result
  invariants_map->emplace(std::make_pair(inst, invariant));

  return invariant;
}

}  // namespace opt
}  // namespace spvtools
