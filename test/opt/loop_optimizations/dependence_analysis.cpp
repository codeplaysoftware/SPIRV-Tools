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

#include <gmock/gmock.h>

#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "../assembly_builder.h"
#include "../function_utils.h"
#include "../pass_fixture.h"
#include "../pass_utils.h"

#include "opt/iterator.h"
#include "opt/loop_descriptor.h"
#include "opt/pass.h"
#include "opt/tree_iterator.h"
#include "opt/loop_dependence.h"

namespace {

using namespace spvtools;
using ::testing::UnorderedElementsAre;

using PassClassTest = PassTest<::testing::Test>;


/*
Generated from the following GLSL + --eliminate-local-multi-store

#version 410 core
layout (location = 1) out float array[10];
void main() {
  for (int i = 0; i < 10; ++i) {
    array[i] = array[i+1];
  }
}
*/
TEST_F(PassClassTest, BasicDependenceTest) {
  const std::string text = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main" %24
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 410
               OpName %4 "main"
               OpName %24 "array"
               OpDecorate %24 Location 1
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %6 = OpTypeInt 32 1
          %7 = OpTypePointer Function %6
          %9 = OpConstant %6 0
         %16 = OpConstant %6 10
         %17 = OpTypeBool
         %19 = OpTypeFloat 32
         %20 = OpTypeInt 32 0
         %21 = OpConstant %20 10
         %22 = OpTypeArray %19 %21
         %23 = OpTypePointer Output %22
         %24 = OpVariable %23 Output
         %27 = OpConstant %6 1
         %29 = OpTypePointer Output %19
          %4 = OpFunction %2 None %3
          %5 = OpLabel
               OpBranch %10
         %10 = OpLabel
         %35 = OpPhi %6 %9 %5 %34 %13
               OpLoopMerge %12 %13 None
               OpBranch %14
         %14 = OpLabel
         %18 = OpSLessThan %17 %35 %16
               OpBranchConditional %18 %11 %12
         %11 = OpLabel
         %28 = OpIAdd %6 %35 %27
         %30 = OpAccessChain %29 %24 %28
         %31 = OpLoad %19 %30
         %32 = OpAccessChain %29 %24 %35
               OpStore %32 %31
               OpBranch %13
         %13 = OpLabel
         %34 = OpIAdd %6 %35 %27
               OpBranch %10
         %12 = OpLabel
               OpReturn
               OpFunctionEnd
  )";
  // clang-format on
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << text << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 4);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  opt::LoopDependenceAnalysis analysis {context.get(), ld.GetLoopByIndex(0)};
  analysis.DumpIterationSpaceAsDot(std::cout);
}

/*
Generated from the following GLSL + --eliminate-local-multi-store

#version 410 core
layout (location = 1) out float array[10];
void main() {
  for (int i = 0; i < 10; ++i) {
    array[5] = array[6];
  }
}
*/
TEST_F(PassClassTest, BasicZIV) {
  const std::string text = R"(OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main" %3
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 430
               OpName %2 "main"
               OpName %3 "array"
               OpDecorate %3 Location 1
          %4 = OpTypeVoid
          %5 = OpTypeFunction %4
          %6 = OpTypeInt 32 1
          %7 = OpTypePointer Function %6
          %8 = OpConstant %6 0
          %9 = OpConstant %6 10
         %10 = OpTypeBool
         %11 = OpTypeFloat 32
         %12 = OpTypeInt 32 0
         %13 = OpConstant %12 10
         %14 = OpTypeArray %11 %13
         %15 = OpTypePointer Output %14
          %3 = OpVariable %15 Output
         %16 = OpConstant %6 5
         %17 = OpConstant %6 6
         %18 = OpTypePointer Output %11
         %19 = OpConstant %6 1
          %2 = OpFunction %4 None %5
         %20 = OpLabel
               OpBranch %21
         %21 = OpLabel
         %22 = OpPhi %6 %8 %20 %23 %24
               OpLoopMerge %25 %24 None
               OpBranch %26
         %26 = OpLabel
         %27 = OpSLessThan %10 %22 %9
               OpBranchConditional %27 %28 %25
         %28 = OpLabel
         %29 = OpAccessChain %18 %3 %17
         %30 = OpLoad %11 %29
         %31 = OpAccessChain %18 %3 %16
               OpStore %31 %30
               OpBranch %24
         %24 = OpLabel
         %23 = OpIAdd %6 %22 %19
               OpBranch %21
         %25 = OpLabel
               OpReturn
               OpFunctionEnd
    )";
  // clang-format on
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << text << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 2);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  opt::LoopDependenceAnalysis analysis {context.get(), ld.GetLoopByIndex(0)};
  analysis.DumpIterationSpaceAsDot(std::cout);

  const ir::Instruction* store = nullptr;
  for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f,28)) {
    if (inst.opcode() == SpvOp::SpvOpStore) {
      store = &inst;
    }
  }

  EXPECT_TRUE(store);
  EXPECT_FALSE(
      analysis.GetDependence(store, context->get_def_use_mgr()->GetDef(30)));
}

}  // namespace
