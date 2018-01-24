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

#include <gmock/gmock.h>

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "../assembly_builder.h"
#include "../function_utils.h"
#include "../pass_fixture.h"
#include "../pass_utils.h"
#include "opt/licm_pass.h"
#include "opt/loop_descriptor.h"
#include "opt/pass.h"

namespace {

using namespace spvtools;
using ::testing::UnorderedElementsAre;

using PassClassTest = PassTest<::testing::Test>;

/*
  Generated from the following GLSL fragment shader
--eliminate-local-multi-store has also been run on the spv binary
#version 440 core
void main() {
  int a = 4;
  for (int i = 0; i < 10; i++) {
    switch (a) {
      case 1:
        break;
      case 2:
      case 3:
        break;
      default:
        break;
    }
  }
}
*/
TEST_F(PassClassTest, HoistSwitch) {
  const std::string before_hoist = R"(OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main"
OpExecutionMode %main OriginUpperLeft
OpSource GLSL 440
OpName %main "main"
%void = OpTypeVoid
%4 = OpTypeFunction %void
%int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
%int_4 = OpConstant %int 4
%int_0 = OpConstant %int 0
%int_10 = OpConstant %int 10
%bool = OpTypeBool
%int_1 = OpConstant %int 1
%main = OpFunction %void None %4
%12 = OpLabel
OpBranch %13
%13 = OpLabel
%14 = OpPhi %int %int_0 %12 %15 %16
OpLoopMerge %17 %16 None
OpBranch %18
%18 = OpLabel
%19 = OpSLessThan %bool %14 %int_10
OpBranchConditional %19 %20 %17
%20 = OpLabel
OpSelectionMerge %21 None
OpSwitch %int_4 %22 1 %23 2 %24 3 %24
%22 = OpLabel
OpBranch %21
%23 = OpLabel
OpBranch %21
%24 = OpLabel
OpBranch %21
%21 = OpLabel
OpBranch %16
%16 = OpLabel
%15 = OpIAdd %int %14 %int_1
OpBranch %13
%17 = OpLabel
OpReturn
OpFunctionEnd
)";

  const std::string after_hoist = R"(
)";

  SinglePassRunAndCheck<opt::LICMPass>(before_hoist, after_hoist, true);
}

}  // namespace
