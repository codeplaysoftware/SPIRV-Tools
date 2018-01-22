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
  Generated from the following GLSL
#version 440 core
void main(){
  int a = 1;
  int b = 1;
  for (int i = 0; i < 10; i++) {
    if (a == 1) {
      a = 2;
    } else {
      b = 1;
    }
  }
}
*/
TEST_F(PassClassTest, IfElseHoist) {
  const std::string before_hoist = R"(OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main"
OpExecutionMode %main OriginUpperLeft
OpSource GLSL 440
OpName %main "main"
OpName %a "a"
OpName %b "b"
OpName %i "i"
%void = OpTypeVoid
%7 = OpTypeFunction %void
%int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
%int_1 = OpConstant %int 1
%int_0 = OpConstant %int 0
%int_10 = OpConstant %int 10
%bool = OpTypeBool
%int_2 = OpConstant %int 2
%main = OpFunction %void None %7
%15 = OpLabel
%a = OpVariable %_ptr_Function_int Function
%b = OpVariable %_ptr_Function_int Function
%i = OpVariable %_ptr_Function_int Function
OpStore %a %int_1
OpStore %b %int_1
OpStore %i %int_0
OpBranch %16
%16 = OpLabel
OpLoopMerge %17 %18 None
OpBranch %19
%19 = OpLabel
%20 = OpLoad %int %i
%21 = OpSLessThan %bool %20 %int_10
OpBranchConditional %21 %22 %17
%22 = OpLabel
%23 = OpLoad %int %a
%24 = OpIEqual %bool %23 %int_1
OpSelectionMerge %25 None
OpBranchConditional %24 %26 %27
%26 = OpLabel
OpStore %a %int_2
OpBranch %25
%27 = OpLabel
OpStore %b %int_1
OpBranch %25
%25 = OpLabel
OpBranch %18
%18 = OpLabel
%28 = OpLoad %int %i
%29 = OpIAdd %int %28 %int_1
OpStore %i %29
OpBranch %16
%17 = OpLabel
OpReturn
OpFunctionEnd
)";

  const std::string after_hoist = R"(
)";

  SinglePassRunAndCheck<opt::LICMPass>(before_hoist, after_hoist, true);
}

}  // namespace
