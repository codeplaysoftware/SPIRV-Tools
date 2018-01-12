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
  int b = 2;
  int hoist = 0;
  for (int i = 0; i < 10; i++) {
    // invariant
    hoist = a + b;
  }
}
*/
TEST_F(PassClassTest, SimpleHoist) {
  const std::string before_hoist = R"(OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main"
OpExecutionMode %main OriginUpperLeft
OpSource GLSL 440
OpName %main "main"
OpName %a "a"
OpName %b "b"
OpName %hoist "hoist"
OpName %i "i"
%void = OpTypeVoid
%8 = OpTypeFunction %void
%int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
%int_1 = OpConstant %int 1
%int_2 = OpConstant %int 2
%int_0 = OpConstant %int 0
%int_10 = OpConstant %int 10
%bool = OpTypeBool
%main = OpFunction %void None %8
%16 = OpLabel
%a = OpVariable %_ptr_Function_int Function
%b = OpVariable %_ptr_Function_int Function
%hoist = OpVariable %_ptr_Function_int Function
%i = OpVariable %_ptr_Function_int Function
OpStore %a %int_1
OpStore %b %int_2
OpStore %hoist %int_0
OpStore %i %int_0
OpBranch %17
%17 = OpLabel
OpLoopMerge %18 %19 None
OpBranch %20
%20 = OpLabel
%21 = OpLoad %int %i
%22 = OpSLessThan %bool %21 %int_10
OpBranchConditional %22 %23 %18
%23 = OpLabel
%24 = OpLoad %int %a
%25 = OpLoad %int %b
%26 = OpIAdd %int %24 %25
OpStore %hoist %26
OpBranch %19
%19 = OpLabel
%27 = OpLoad %int %i
%28 = OpIAdd %int %27 %int_1
OpStore %i %28
OpBranch %17
%18 = OpLabel
OpReturn
OpFunctionEnd
)";


  const std::string after_hoist = R"(OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main"
OpExecutionMode %main OriginUpperLeft
OpSource GLSL 440
OpName %main "main"
OpName %a "a"
OpName %b "b"
OpName %hoist "hoist"
OpName %i "i"
%void = OpTypeVoid
%8 = OpTypeFunction %void
%int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
%int_1 = OpConstant %int 1
%int_2 = OpConstant %int 2
%int_0 = OpConstant %int 0
%int_10 = OpConstant %int 10
%bool = OpTypeBool
%main = OpFunction %void None %8
%16 = OpLabel
%a = OpVariable %_ptr_Function_int Function
%b = OpVariable %_ptr_Function_int Function
%hoist = OpVariable %_ptr_Function_int Function
%i = OpVariable %_ptr_Function_int Function
OpStore %a %int_1
OpStore %b %int_2
OpStore %hoist %int_0
OpStore %i %int_0
%24 = OpLoad %int %a
%25 = OpLoad %int %b
%26 = OpIAdd %int %24 %25
OpStore %hoist %26
OpBranch %17
%17 = OpLabel
OpLoopMerge %18 %19 None
OpBranch %20
%20 = OpLabel
%21 = OpLoad %int %i
%22 = OpSLessThan %bool %21 %int_10
OpBranchConditional %22 %23 %18
%23 = OpLabel
OpBranch %19
%19 = OpLabel
%27 = OpLoad %int %i
%28 = OpIAdd %int %27 %int_1
OpStore %i %28
OpBranch %17
%18 = OpLabel
OpReturn
OpFunctionEnd
)";

  SinglePassRunAndCheck<opt::LICMPass>(before_hoist, after_hoist, true);
}

}  // namespace
