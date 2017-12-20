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
               OpEntryPoint Fragment %2 "main" %3
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 330
               OpName %2 "main"
               OpName %4 "i"
               OpName %5 "j"
               OpName %6 "k"
               OpName %3 "c"
               OpDecorate %3 Location 0
          %7 = OpTypeVoid
          %8 = OpTypeFunction %7
          %9 = OpTypeInt 32 1
         %10 = OpTypePointer Function %9
         %11 = OpConstant %9 0
         %12 = OpConstant %9 10
         %13 = OpTypeBool
         %14 = OpConstant %9 11
         %15 = OpConstant %9 1
         %16 = OpConstant %9 12
         %17 = OpTypeFloat 32
         %18 = OpTypeVector %17 4
         %19 = OpTypePointer Output %18
          %3 = OpVariable %19 Output
          %2 = OpFunction %7 None %8
         %20 = OpLabel
          %4 = OpVariable %10 Function
          %5 = OpVariable %10 Function
          %6 = OpVariable %10 Function
               OpStore %4 %11
               OpBranch %21
         %21 = OpLabel
               OpLoopMerge %22 %23 None
               OpBranch %24
         %24 = OpLabel
         %25 = OpLoad %9 %4
         %26 = OpSLessThan %13 %25 %12
               OpBranchConditional %26 %27 %22
         %27 = OpLabel
               OpStore %5 %11
               OpStore %6 %11
               OpBranch %28
         %28 = OpLabel
               OpLoopMerge %29 %30 None
               OpBranch %31
         %31 = OpLabel
         %32 = OpLoad %9 %5
         %33 = OpSLessThan %13 %32 %14
               OpBranchConditional %33 %34 %29
         %34 = OpLabel
               OpBranch %30
         %30 = OpLabel
         %35 = OpLoad %9 %5
         %36 = OpIAdd %9 %35 %15
               OpStore %5 %36
               OpBranch %28
         %29 = OpLabel
               OpBranch %37
         %37 = OpLabel
               OpLoopMerge %38 %39 None
               OpBranch %40
         %40 = OpLabel
         %41 = OpLoad %9 %6
         %42 = OpSLessThan %13 %41 %16
               OpBranchConditional %42 %43 %38
         %43 = OpLabel
               OpBranch %39
         %39 = OpLabel
         %44 = OpLoad %9 %6
         %45 = OpIAdd %9 %44 %15
               OpStore %6 %45
               OpBranch %37
         %38 = OpLabel
               OpBranch %23
         %23 = OpLabel
         %46 = OpLoad %9 %4
         %47 = OpIAdd %9 %46 %15
               OpStore %4 %47
               OpBranch %21
         %22 = OpLabel
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
  ir::LoopDescriptor ld{f};

  EXPECT_EQ(ld.NumLoops(), 3u);

  // Invalid basic block id.
  EXPECT_EQ(ld[0u], nullptr);
  // Not a loop header.
  EXPECT_EQ(ld[20], nullptr);

  ir::Loop& parent_loop = *ld[21];
  EXPECT_TRUE(parent_loop.HasNestedLoops());
  EXPECT_FALSE(parent_loop.IsNested());
  EXPECT_EQ(parent_loop.GetDepth(), 1u);
  EXPECT_EQ(std::distance(parent_loop.begin(), parent_loop.end()), 2u);
  EXPECT_EQ(parent_loop.GetHeaderBlock(), spvtest::GetBasicBlock(f, 21));
  EXPECT_EQ(parent_loop.GetLatchBlock(), spvtest::GetBasicBlock(f, 23));
  EXPECT_EQ(parent_loop.GetMergeBlock(), spvtest::GetBasicBlock(f, 22));

  ir::Loop& child_loop_1 = *ld[28];
  EXPECT_FALSE(child_loop_1.HasNestedLoops());
  EXPECT_TRUE(child_loop_1.IsNested());
  EXPECT_EQ(child_loop_1.GetDepth(), 2u);
  EXPECT_EQ(std::distance(child_loop_1.begin(), child_loop_1.end()), 0u);
  EXPECT_EQ(child_loop_1.GetHeaderBlock(), spvtest::GetBasicBlock(f, 28));
  EXPECT_EQ(child_loop_1.GetLatchBlock(), spvtest::GetBasicBlock(f, 30));
  EXPECT_EQ(child_loop_1.GetMergeBlock(), spvtest::GetBasicBlock(f, 29));

  ir::Loop& child_loop_2 = *ld[37];
  EXPECT_FALSE(child_loop_2.HasNestedLoops());
  EXPECT_TRUE(child_loop_2.IsNested());
  EXPECT_EQ(child_loop_2.GetDepth(), 2u);
  EXPECT_EQ(std::distance(child_loop_2.begin(), child_loop_2.end()), 0u);
  EXPECT_EQ(child_loop_2.GetHeaderBlock(), spvtest::GetBasicBlock(f, 37));
  EXPECT_EQ(child_loop_2.GetLatchBlock(), spvtest::GetBasicBlock(f, 39));
  EXPECT_EQ(child_loop_2.GetMergeBlock(), spvtest::GetBasicBlock(f, 38));
}

}  // namespace
