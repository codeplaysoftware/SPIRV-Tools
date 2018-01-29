// Copyright (c) 2016 Google Inc.
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

#include "spirv-tools/optimizer.hpp"

#include "build_module.h"
#include "make_unique.h"
#include "pass_manager.h"
#include "passes.h"

namespace spvtools {

struct Optimizer::PassToken::Impl {
  Impl(std::unique_ptr<opt::Pass> p) : pass(std::move(p)) {}

  std::unique_ptr<opt::Pass> pass;  // Internal implementation pass.
};

Optimizer::PassToken::PassToken(
    std::unique_ptr<Optimizer::PassToken::Impl> impl)
    : impl_(std::move(impl)) {}
Optimizer::PassToken::PassToken(PassToken&& that)
    : impl_(std::move(that.impl_)) {}

Optimizer::PassToken& Optimizer::PassToken::operator=(PassToken&& that) {
  impl_ = std::move(that.impl_);
  return *this;
}

Optimizer::PassToken::~PassToken() {}

struct Optimizer::Impl {
  explicit Impl(spv_target_env env) : target_env(env), pass_manager() {}

  const spv_target_env target_env;  // Target environment.
  opt::PassManager pass_manager;    // Internal implementation pass manager.
};

Optimizer::Optimizer(spv_target_env env) : impl_(new Impl(env)) {}

Optimizer::~Optimizer() {}

void Optimizer::SetMessageConsumer(MessageConsumer c) {
  // All passes' message consumer needs to be updated.
  for (uint32_t i = 0; i < impl_->pass_manager.NumPasses(); ++i) {
    impl_->pass_manager.GetPass(i)->SetMessageConsumer(c);
  }
  impl_->pass_manager.SetMessageConsumer(std::move(c));
}

Optimizer& Optimizer::RegisterPass(PassToken&& p) {
  // Change to use the pass manager's consumer.
  p.impl_->pass->SetMessageConsumer(impl_->pass_manager.consumer());
  impl_->pass_manager.AddPass(std::move(p.impl_->pass));
  return *this;
}

// The legalization passes take a spir-v shader generated by an HLSL front-end
// and turn it into a valid vulkan spir-v shader.  There are two ways in which
// the code will be invalid at the start:
//
// 1) There will be opaque objects, like images, which will be passed around
//    in intermediate objects.  Valid spir-v will have to replace the use of
//    the opaque object with an intermediate object that is the result of the
//    load of the global opaque object.
//
// 2) There will be variables that contain pointers to structured or uniform
//    buffers.  It be legal, the variables must be eliminated, and the
//    references to the structured buffers must use the result of OpVariable
//    in the Uniform storage class.
//
// Optimization in this list must accept shaders with these relaxation of the
// rules.  There is not guarantee that this list of optimizations is able to
// legalize all inputs, but it is on a best effort basis.
//
// The legalization problem is essentially a very general copy propagation
// problem.  The optimization we use are all used to either do copy propagation
// or enable more copy propagation.
Optimizer& Optimizer::RegisterLegalizationPasses() {
  return
      // Make sure uses and definitions are in the same function.
      RegisterPass(CreateInlineExhaustivePass())
          // Make private variable function scope
          .RegisterPass(CreateEliminateDeadFunctionsPass())
          .RegisterPass(CreatePrivateToLocalPass())
          // Split up aggragates so they are easier to deal with.
          .RegisterPass(CreateScalarReplacementPass())
          // Remove loads and stores so everything is in intermediate values.
          // Takes care of copy propagation of non-members.
          .RegisterPass(CreateLocalSingleBlockLoadStoreElimPass())
          .RegisterPass(CreateLocalSingleStoreElimPass())
          .RegisterPass(CreateLocalMultiStoreElimPass())
          // Copy propagate members.  Cleans up code sequences generated by
          // scalar replacement.
          .RegisterPass(CreateInsertExtractElimPass())
          // May need loop unrolling here see
          // https://github.com/Microsoft/DirectXShaderCompiler/pull/930
          .RegisterPass(CreateDeadBranchElimPass())
          // Get rid of unused code that contain traces of illegal code
          // or unused references to unbound external objects
          .RegisterPass(CreateDeadInsertElimPass())
          .RegisterPass(CreateAggressiveDCEPass());
}

Optimizer& Optimizer::RegisterPerformancePasses() {
  return RegisterPass(CreateRemoveDuplicatesPass())
      .RegisterPass(CreateMergeReturnPass())
      .RegisterPass(CreateInlineExhaustivePass())
      .RegisterPass(CreateAggressiveDCEPass())
      .RegisterPass(CreateScalarReplacementPass())
      .RegisterPass(CreateLocalAccessChainConvertPass())
      .RegisterPass(CreateLocalSingleBlockLoadStoreElimPass())
      .RegisterPass(CreateLocalSingleStoreElimPass())
      .RegisterPass(CreateLocalMultiStoreElimPass())
      .RegisterPass(CreateCCPPass())
      .RegisterPass(CreateAggressiveDCEPass())
      .RegisterPass(CreateRedundancyEliminationPass())
      .RegisterPass(CreateInsertExtractElimPass())
      .RegisterPass(CreateDeadInsertElimPass())
      .RegisterPass(CreateDeadBranchElimPass())
      .RegisterPass(CreateIfConversionPass())
      .RegisterPass(CreateAggressiveDCEPass())
      .RegisterPass(CreateBlockMergePass())
      .RegisterPass(CreateRedundancyEliminationPass())
      .RegisterPass(CreateDeadBranchElimPass())
      .RegisterPass(CreateBlockMergePass())
      .RegisterPass(CreateInsertExtractElimPass());
  // Currently exposing driver bugs resulting in crashes (#946)
  // .RegisterPass(CreateCommonUniformElimPass())
}

Optimizer& Optimizer::RegisterSizePasses() {
  return RegisterPass(CreateRemoveDuplicatesPass())
      .RegisterPass(CreateMergeReturnPass())
      .RegisterPass(CreateInlineExhaustivePass())
      .RegisterPass(CreateAggressiveDCEPass())
      .RegisterPass(CreateScalarReplacementPass())
      .RegisterPass(CreateLocalAccessChainConvertPass())
      .RegisterPass(CreateLocalSingleBlockLoadStoreElimPass())
      .RegisterPass(CreateLocalSingleStoreElimPass())
      .RegisterPass(CreateInsertExtractElimPass())
      .RegisterPass(CreateDeadInsertElimPass())
      .RegisterPass(CreateLocalMultiStoreElimPass())
      .RegisterPass(CreateCCPPass())
      .RegisterPass(CreateAggressiveDCEPass())
      .RegisterPass(CreateDeadBranchElimPass())
      .RegisterPass(CreateIfConversionPass())
      .RegisterPass(CreateAggressiveDCEPass())
      .RegisterPass(CreateBlockMergePass())
      .RegisterPass(CreateInsertExtractElimPass())
      .RegisterPass(CreateDeadInsertElimPass())
      .RegisterPass(CreateRedundancyEliminationPass())
      .RegisterPass(CreateCFGCleanupPass())
      // Currently exposing driver bugs resulting in crashes (#946)
      // .RegisterPass(CreateCommonUniformElimPass())
      .RegisterPass(CreateAggressiveDCEPass());
}

bool Optimizer::Run(const uint32_t* original_binary,
                    const size_t original_binary_size,
                    std::vector<uint32_t>* optimized_binary) const {
  std::unique_ptr<ir::IRContext> context =
      BuildModule(impl_->target_env, impl_->pass_manager.consumer(),
                  original_binary, original_binary_size);
  if (context == nullptr) return false;

  auto status = impl_->pass_manager.Run(context.get());
  if (status == opt::Pass::Status::SuccessWithChange ||
      (status == opt::Pass::Status::SuccessWithoutChange &&
       (optimized_binary->data() != original_binary ||
        optimized_binary->size() != original_binary_size))) {
    optimized_binary->clear();
    context->module()->ToBinary(optimized_binary, /* skip_nop = */ true);
  }

  return status != opt::Pass::Status::Failure;
}

Optimizer& Optimizer::SetPrintAll(std::ostream* out) {
  impl_->pass_manager.SetPrintAll(out);
  return *this;
}

Optimizer::PassToken CreateNullPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(MakeUnique<opt::NullPass>());
}

Optimizer::PassToken CreateStripDebugInfoPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::StripDebugInfoPass>());
}

Optimizer::PassToken CreateEliminateDeadFunctionsPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::EliminateDeadFunctionsPass>());
}

Optimizer::PassToken CreateSetSpecConstantDefaultValuePass(
    const std::unordered_map<uint32_t, std::string>& id_value_map) {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::SetSpecConstantDefaultValuePass>(id_value_map));
}

Optimizer::PassToken CreateSetSpecConstantDefaultValuePass(
    const std::unordered_map<uint32_t, std::vector<uint32_t>>& id_value_map) {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::SetSpecConstantDefaultValuePass>(id_value_map));
}

Optimizer::PassToken CreateFlattenDecorationPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::FlattenDecorationPass>());
}

Optimizer::PassToken CreateFreezeSpecConstantValuePass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::FreezeSpecConstantValuePass>());
}

Optimizer::PassToken CreateFoldSpecConstantOpAndCompositePass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::FoldSpecConstantOpAndCompositePass>());
}

Optimizer::PassToken CreateUnifyConstantPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::UnifyConstantPass>());
}

Optimizer::PassToken CreateEliminateDeadConstantPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::EliminateDeadConstantPass>());
}

Optimizer::PassToken CreateDeadVariableEliminationPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::DeadVariableElimination>());
}

Optimizer::PassToken CreateStrengthReductionPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::StrengthReductionPass>());
}

Optimizer::PassToken CreateBlockMergePass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::BlockMergePass>());
}

Optimizer::PassToken CreateInlineExhaustivePass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::InlineExhaustivePass>());
}

Optimizer::PassToken CreateInlineOpaquePass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::InlineOpaquePass>());
}

Optimizer::PassToken CreateLocalAccessChainConvertPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::LocalAccessChainConvertPass>());
}

Optimizer::PassToken CreateLocalSingleBlockLoadStoreElimPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::LocalSingleBlockLoadStoreElimPass>());
}

Optimizer::PassToken CreateLocalSingleStoreElimPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::LocalSingleStoreElimPass>());
}

Optimizer::PassToken CreateInsertExtractElimPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::InsertExtractElimPass>());
}

Optimizer::PassToken CreateDeadInsertElimPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::DeadInsertElimPass>());
}

Optimizer::PassToken CreateDeadBranchElimPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::DeadBranchElimPass>());
}

Optimizer::PassToken CreateLocalMultiStoreElimPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::LocalMultiStoreElimPass>());
}

Optimizer::PassToken CreateAggressiveDCEPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::AggressiveDCEPass>());
}

Optimizer::PassToken CreateCommonUniformElimPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::CommonUniformElimPass>());
}

Optimizer::PassToken CreateCompactIdsPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::CompactIdsPass>());
}

Optimizer::PassToken CreateMergeReturnPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::MergeReturnPass>());
}

std::vector<const char*> Optimizer::GetPassNames() const {
  std::vector<const char*> v;
  for (uint32_t i = 0; i < impl_->pass_manager.NumPasses(); i++) {
    v.push_back(impl_->pass_manager.GetPass(i)->name());
  }
  return v;
}

Optimizer::PassToken CreateCFGCleanupPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::CFGCleanupPass>());
}

Optimizer::PassToken CreateLocalRedundancyEliminationPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::LocalRedundancyEliminationPass>());
}

Optimizer::PassToken CreateLoopInvariantCodeMotionPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(MakeUnique<opt::LICMPass>());
}

Optimizer::PassToken CreateRedundancyEliminationPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::RedundancyEliminationPass>());
}

Optimizer::PassToken CreateRemoveDuplicatesPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::RemoveDuplicatesPass>());
}

Optimizer::PassToken CreateScalarReplacementPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::ScalarReplacementPass>());
}

Optimizer::PassToken CreatePrivateToLocalPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::PrivateToLocalPass>());
}

Optimizer::PassToken CreateCCPPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(MakeUnique<opt::CCPPass>());
}

Optimizer::PassToken CreateWorkaround1209Pass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::Workaround1209>());
}

Optimizer::PassToken CreateIfConversionPass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::IfConversion>());
}

Optimizer::PassToken CreateReplaceInvalidOpcodePass() {
  return MakeUnique<Optimizer::PassToken::Impl>(
      MakeUnique<opt::ReplaceInvalidOpcodePass>());
}
}  // namespace spvtools
