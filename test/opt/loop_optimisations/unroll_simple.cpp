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

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "../assembly_builder.h"
#include "../function_utils.h"
#include "../pass_fixture.h"
#include "../pass_utils.h"
#include "opt/loop_unroller.h"
#include "opt/pass.h"

namespace {

using namespace spvtools;
using ::testing::UnorderedElementsAre;

using PassClassTest = PassTest<::testing::Test>;

/*
Generated from the following GLSL
#version 330 core
layout(location = 0) out vec4 c;
void main() {
  float x[10];
  for (int i = 0; i < 10; ++i) {
    x[0] = 1.0f;
  }
}
*/
TEST_F(PassClassTest, BasicVisitFromEntryPoint) {
  // Unoptimised.
  /*  const std::string text = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main" %33
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 330
               OpName %4 "main"
               OpName %8 "i"
               OpName %24 "x"
               OpName %33 "c"
               OpDecorate %33 Location 0
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
         %23 = OpTypePointer Function %22
         %25 = OpConstant %19 1
         %26 = OpTypePointer Function %19
         %29 = OpConstant %6 1
         %31 = OpTypeVector %19 4
         %32 = OpTypePointer Output %31
         %33 = OpVariable %32 Output
          %4 = OpFunction %2 None %3
          %5 = OpLabel
          %8 = OpVariable %7 Function
         %24 = OpVariable %23 Function
               OpStore %8 %9
               OpBranch %10
         %10 = OpLabel
               OpLoopMerge %12 %13 None
               OpBranch %14
         %14 = OpLabel
         %15 = OpLoad %6 %8
         %18 = OpSLessThan %17 %15 %16
               OpBranchConditional %18 %11 %12
         %11 = OpLabel
         %27 = OpAccessChain %26 %24 %9
               OpStore %27 %25
               OpBranch %13
         %13 = OpLabel
         %28 = OpLoad %6 %8
         %30 = OpIAdd %6 %28 %29
               OpStore %8 %30
               OpBranch %10
         %12 = OpLabel
               OpReturn
               OpFunctionEnd
  )";*/

  // With opt::LocalMultiStoreElimPass
  const std::string text = R"(
         OpCapability Shader
         %1 = OpExtInstImport "GLSL.std.450"
         OpMemoryModel Logical GLSL450
         OpEntryPoint Fragment %2 "main" %3
         OpExecutionMode %2 OriginUpperLeft
         OpSource GLSL 330
         OpName %2 "main"
         OpName %5 "x"
         OpName %3 "c"
         OpDecorate %3 Location 0
         %6 = OpTypeVoid
         %7 = OpTypeFunction %6
         %8 = OpTypeInt 32 1
         %9 = OpTypePointer Function %8
         %10 = OpConstant %8 0
         %11 = OpConstant %8 10
         %12 = OpTypeBool
         %13 = OpTypeFloat 32
         %14 = OpTypeInt 32 0
         %15 = OpConstant %14 10
         %16 = OpTypeArray %13 %15
         %17 = OpTypePointer Function %16
         %18 = OpConstant %13 1
         %19 = OpTypePointer Function %13
         %20 = OpConstant %8 1
         %21 = OpTypeVector %13 4
         %22 = OpTypePointer Output %21
         %3 = OpVariable %22 Output
         %2 = OpFunction %6 None %7
         %23 = OpLabel
         %5 = OpVariable %17 Function
         OpBranch %24
         %24 = OpLabel
         %34 = OpPhi %8 %10 %23 %33 %26
         OpLoopMerge %25 %26 None
         OpBranch %27
         %27 = OpLabel
         %29 = OpSLessThan %12 %34 %11
         OpBranchConditional %29 %30 %25
         %30 = OpLabel
         %31 = OpAccessChain %19 %5 %10
         OpStore %31 %18
         OpBranch %26
         %26 = OpLabel
         %33 = OpIAdd %8 %34 %20
         OpBranch %24
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

  std::cout << "Pre-opt binary\n";

  std::cout << text << "\n\n\n";
  opt::LoopUnroller loop_unroller;
  SetDisassembleOptions(SPV_BINARY_TO_TEXT_OPTION_NO_HEADER);
  std::cout << std::get<0>(
      SinglePassRunAndDisassemble<opt::LoopUnroller>(text, false, true));
}

}  // namespace
