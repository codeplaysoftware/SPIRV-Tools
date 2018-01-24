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

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "../assembly_builder.h"
#include "../function_utils.h"
#include "../pass_fixture.h"
#include "../pass_utils.h"
#include "opt/loop_unroller.h"
#include "opt/loop_utils.h"
#include "opt/pass.h"

namespace {

using namespace spvtools;
using ::testing::UnorderedElementsAre;

template <int factor>
class PartialUnrollerTestPass : public opt::Pass {
 public:
  PartialUnrollerTestPass() : Pass() {}

  const char* name() const override { return "Loop unroller"; }

  Status Process(ir::IRContext* context) override {
    bool changed = false;
    for (ir::Function& f : *context->module()) {
      opt::LoopUtils loop_utils{&f, context};

      for (auto& loop : loop_utils.GetLoopDescriptor()) {
        if (loop_utils.PartiallyUnroll(&loop, factor)) {
          changed = true;
        }
      }
    }

    if (changed) return Pass::Status::SuccessWithChange;
    return Pass::Status::SuccessWithoutChange;
  }
};

using PassClassTest = PassTest<::testing::Test>;

/*
Generated from the following GLSL
#version 330 core
layout(location = 0) out vec4 c;
void main() {
  int lower_bound = 1;
  float x[10];
  for (int i = lower_bound; i < 10; ++i) {
    x[i] = 1.0f;
  }
}
*/
TEST_F(PassClassTest, SimpleFullyUnrollTest) {
  // clang-format off
  // With opt::LocalMultiStoreElimPass
  const std::string text = R"(OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %2 "main" %3
OpExecutionMode %2 OriginUpperLeft
OpSource GLSL 410
OpName %2 "main"
OpName %3 "in_upper_bound"
OpName %4 "x"
OpDecorate %3 Flat
OpDecorate %3 Location 0
%5 = OpTypeVoid
%6 = OpTypeFunction %5
%7 = OpTypeInt 32 1
%8 = OpTypePointer Function %7
%9 = OpConstant %7 0
%10 = OpTypePointer Input %7
%3 = OpVariable %10 Input
%11 = OpTypeBool
%12 = OpTypeFloat 32
%13 = OpTypeInt 32 0
%14 = OpConstant %13 10
%15 = OpTypeArray %12 %14
%16 = OpTypePointer Function %15
%17 = OpConstant %12 1
%18 = OpTypePointer Function %12
%19 = OpConstant %7 1
%2 = OpFunction %5 None %6
%20 = OpLabel
%4 = OpVariable %16 Function
OpBranch %21
%21 = OpLabel
%22 = OpPhi %7 %9 %20 %23 %24
OpLoopMerge %25 %24 None
OpBranch %26
%26 = OpLabel
%27 = OpLoad %7 %3
%28 = OpSLessThan %11 %22 %27
OpBranchConditional %28 %29 %25
%29 = OpLabel
%30 = OpAccessChain %18 %4 %22
OpStore %30 %17
OpBranch %24
%24 = OpLabel
%23 = OpIAdd %7 %22 %19
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

  opt::LoopUnroller loop_unroller;
  SetDisassembleOptions(SPV_BINARY_TO_TEXT_OPTION_NO_HEADER);

  // Make sure the pass doesn't run
  SinglePassRunAndCheck<opt::LoopUnroller>(text, text, false);
  SinglePassRunAndCheck<PartialUnrollerTestPass<1>>(text, text, false);
  SinglePassRunAndCheck<PartialUnrollerTestPass<2>>(text, text, false);
}

}  // namespace
