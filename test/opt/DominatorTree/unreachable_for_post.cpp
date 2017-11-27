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
#include "../pass_fixture.h"
#include "../pass_utils.h"
#include "opt/dominator_analysis_pass.h"
#include "opt/pass.h"

namespace {

using namespace spvtools;
using ::testing::UnorderedElementsAre;

using PassClassTest = PassTest<::testing::Test>;

const ir::Function* getFromModule(ir::Module* module, uint32_t id) {
  for (ir::Function& F : *module) {
    if (F.result_id() == id) {
      return &F;
    }
  }
  return nullptr;
}

/*
  Generated from the following GLSL
#version 440 core
void main() {
  for (int i = 0; i < 1; i++) {
    break;
  }
}
*/
TEST_F(PassClassTest, UnreachableNestedIfs) {
  const std::string text = R"(
    OpCapability Shader
    %1 = OpExtInstImport "GLSL.std.450"
         OpMemoryModel Logical GLSL450
         OpEntryPoint Fragment %4 "main"
         OpExecutionMode %4 OriginUpperLeft
         OpSource GLSL 440
         OpName %4 "main"
         OpName %8 "i"
    %2 = OpTypeVoid
    %3 = OpTypeFunction %2
    %6 = OpTypeInt 32 1
    %7 = OpTypePointer Function %6
    %9 = OpConstant %6 0
   %16 = OpConstant %6 1
   %17 = OpTypeBool
    %4 = OpFunction %2 None %3
    %5 = OpLabel
    %8 = OpVariable %7 Function
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
         OpBranch %12
   %13 = OpLabel
   %20 = OpLoad %6 %8
   %21 = OpIAdd %6 %20 %16
         OpStore %8 %21
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
  opt::DominatorAnalysisPass pass;

  const ir::Function* F = getFromModule(module, 4);

  opt::PostDominatorAnalysis* analysis = pass.GetPostDominatorAnalysis(F);

  EXPECT_TRUE(analysis->Dominates(12, 12));
  EXPECT_TRUE(analysis->Dominates(12, 14));
  EXPECT_TRUE(analysis->Dominates(12, 11));
  EXPECT_TRUE(analysis->Dominates(12, 10));
  EXPECT_TRUE(analysis->Dominates(12, 5));
  EXPECT_TRUE(analysis->Dominates(14, 14));
  EXPECT_TRUE(analysis->Dominates(14, 10));
  EXPECT_TRUE(analysis->Dominates(14, 5));
  EXPECT_TRUE(analysis->Dominates(10, 10));
  EXPECT_TRUE(analysis->Dominates(10, 5));
  EXPECT_TRUE(analysis->Dominates(5, 5));

  EXPECT_TRUE(analysis->StrictlyDominates(12, 14));
  EXPECT_TRUE(analysis->StrictlyDominates(12, 11));
  EXPECT_TRUE(analysis->StrictlyDominates(12, 10));
  EXPECT_TRUE(analysis->StrictlyDominates(12, 5));
  EXPECT_TRUE(analysis->StrictlyDominates(14, 10));
  EXPECT_TRUE(analysis->StrictlyDominates(14, 5));
  EXPECT_TRUE(analysis->StrictlyDominates(10, 5));
}

}  // namespace
