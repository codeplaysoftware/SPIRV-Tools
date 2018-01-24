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
void main(){
  int a = 1;
  for (int i = 0; i < 10; i++) {
    if (a == 1) {
      a = 1;
    }
  }
}
*/
TEST_F(PassClassTest, IfHoist) {
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
%int_1 = OpConstant %int 1
%int_0 = OpConstant %int 0
%int_10 = OpConstant %int 10
%bool = OpTypeBool
%main = OpFunction %void None %4
%11 = OpLabel
OpBranch %12
%12 = OpLabel
%13 = OpPhi %int %int_1 %11 %14 %15
%16 = OpPhi %int %int_0 %11 %17 %15
OpLoopMerge %18 %15 None
OpBranch %19
%19 = OpLabel
%20 = OpSLessThan %bool %16 %int_10
OpBranchConditional %20 %21 %18
%21 = OpLabel
%22 = OpIEqual %bool %13 %int_1
OpSelectionMerge %23 None
OpBranchConditional %22 %24 %23
%24 = OpLabel
OpBranch %23
%23 = OpLabel
%14 = OpPhi %int %13 %21 %int_1 %24
OpBranch %15
%15 = OpLabel
%17 = OpIAdd %int %16 %int_1
OpBranch %12
%18 = OpLabel
OpReturn
OpFunctionEnd
)";

  const std::string after_hoist = R"(
)";

  SinglePassRunAndCheck<opt::LICMPass>(before_hoist, after_hoist, true);
}

}  // namespace
