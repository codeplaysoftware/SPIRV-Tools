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
  for(; i < 10; ++i) {
  }
}
*/
TEST_F(PassClassTest, BasicVisitFromEntryPoint) {
  const std::string text = R"(
                OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main" %3
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 330
               OpName %2 "main"
               OpName %5 "i"
               OpName %3 "c"
               OpDecorate %3 Location 0
          %6 = OpTypeVoid
          %7 = OpTypeFunction %6
          %8 = OpTypeInt 32 1
          %9 = OpTypePointer Function %8
         %10 = OpConstant %8 0
         %11 = OpConstant %8 10
         %12 = OpTypeBool
         %13 = OpConstant %8 1
         %14 = OpTypeFloat 32
         %15 = OpTypeVector %14 4
         %16 = OpTypePointer Output %15
          %3 = OpVariable %16 Output
          %2 = OpFunction %6 None %7
         %17 = OpLabel
          %5 = OpVariable %9 Function
               OpStore %5 %10
               OpBranch %18
         %18 = OpLabel
               OpLoopMerge %19 %20 None
               OpBranch %21
         %21 = OpLabel
         %22 = OpLoad %8 %5
         %23 = OpSLessThan %12 %22 %11
               OpBranchConditional %23 %24 %19
         %24 = OpLabel
               OpBranch %20
         %20 = OpLabel
         %25 = OpLoad %8 %5
         %26 = OpIAdd %8 %25 %13
               OpStore %5 %26
               OpBranch %18
         %19 = OpLabel
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
  opt::LoopDescriptor ld{f};

  EXPECT_EQ(ld.NumLoops(), 1u);

  opt::Loop& loop = ld.GetLoop(0);
  EXPECT_EQ(loop.GetLoopHeader(), spvtest::GetBasicBlock(f, 18));
  EXPECT_EQ(loop.GetContinueBB(), spvtest::GetBasicBlock(f, 20));
  EXPECT_EQ(loop.GetMergeBB(), spvtest::GetBasicBlock(f, 19));

  EXPECT_FALSE(loop.HasNestedLoops());
  EXPECT_FALSE(loop.IsNested());
  EXPECT_EQ(loop.GetNumNestedLoops(), 0u);
}

}  // namespace
