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
#include "opt/loop_descriptor.h"
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
  int i = 0;
  for (; i < 10; ++i) {
    int j = 0;
    int k = 0;
    for (; j < 11; ++j) {}
    for (; k < 12; ++k) {}
  }
}
*/
TEST_F(PassClassTest, BasicVisitFromEntryPoint) {
  const std::string text = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main" %47
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 330
               OpName %4 "main"
               OpName %8 "i"
               OpName %19 "j"
               OpName %20 "k"
               OpName %47 "c"
               OpDecorate %47 Location 0
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %6 = OpTypeInt 32 1
          %7 = OpTypePointer Function %6
          %9 = OpConstant %6 0
         %16 = OpConstant %6 10
         %17 = OpTypeBool
         %27 = OpConstant %6 11
         %30 = OpConstant %6 1
         %38 = OpConstant %6 12
         %44 = OpTypeFloat 32
         %45 = OpTypeVector %44 4
         %46 = OpTypePointer Output %45
         %47 = OpVariable %46 Output
          %4 = OpFunction %2 None %3
          %5 = OpLabel
          %8 = OpVariable %7 Function
         %19 = OpVariable %7 Function
         %20 = OpVariable %7 Function
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
               OpStore %19 %9
               OpStore %20 %9
               OpBranch %21
         %21 = OpLabel
               OpLoopMerge %23 %24 None
               OpBranch %25
         %25 = OpLabel
         %26 = OpLoad %6 %19
         %28 = OpSLessThan %17 %26 %27
               OpBranchConditional %28 %22 %23
         %22 = OpLabel
               OpBranch %24
         %24 = OpLabel
         %29 = OpLoad %6 %19
         %31 = OpIAdd %6 %29 %30
               OpStore %19 %31
               OpBranch %21
         %23 = OpLabel
               OpBranch %32
         %32 = OpLabel
               OpLoopMerge %34 %35 None
               OpBranch %36
         %36 = OpLabel
         %37 = OpLoad %6 %20
         %39 = OpSLessThan %17 %37 %38
               OpBranchConditional %39 %33 %34
         %33 = OpLabel
               OpBranch %35
         %35 = OpLabel
         %40 = OpLoad %6 %20
         %41 = OpIAdd %6 %40 %30
               OpStore %20 %41
               OpBranch %32
         %34 = OpLabel
               OpBranch %13
         %13 = OpLabel
         %42 = OpLoad %6 %8
         %43 = OpIAdd %6 %42 %30
               OpStore %8 %43
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
  opt::LoopDescriptor ld{f};

  EXPECT_EQ(ld.NumLoops(), 3u);

  const opt::Loop& parent_loop = ld.GetLoop(0);
  EXPECT_TRUE(parent_loop.HasNestedLoops());
  EXPECT_EQ(parent_loop.GetNumNestedLoops(), 2u);
  EXPECT_EQ(parent_loop.GetStartBB(), spvtest::GetBasicBlock(f, 10));
  EXPECT_EQ(parent_loop.GetContinueBB(), spvtest::GetBasicBlock(f, 13));
  EXPECT_EQ(parent_loop.GetMergeBB(), spvtest::GetBasicBlock(f, 12));

  const opt::Loop& child_loop_1 = ld.GetLoop(1);
  EXPECT_FALSE(child_loop_1.HasNestedLoops());
  EXPECT_EQ(child_loop_1.GetNumNestedLoops(), 0u);
  EXPECT_EQ(child_loop_1.GetStartBB(), spvtest::GetBasicBlock(f, 21));
  EXPECT_EQ(child_loop_1.GetContinueBB(), spvtest::GetBasicBlock(f, 24));
  EXPECT_EQ(child_loop_1.GetMergeBB(), spvtest::GetBasicBlock(f, 23));

  const opt::Loop& child_loop_2 = ld.GetLoop(2);
  EXPECT_FALSE(child_loop_2.HasNestedLoops());
  EXPECT_EQ(child_loop_2.GetNumNestedLoops(), 0u);
  EXPECT_EQ(child_loop_2.GetStartBB(), spvtest::GetBasicBlock(f, 32));
  EXPECT_EQ(child_loop_2.GetContinueBB(), spvtest::GetBasicBlock(f, 35));
  EXPECT_EQ(child_loop_2.GetMergeBB(), spvtest::GetBasicBlock(f, 34));
}

}  // namespace
