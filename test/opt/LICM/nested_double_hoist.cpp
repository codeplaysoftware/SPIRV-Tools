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
  for (int i = 0; i < 10; i++) {
    for (int j = 0; j < 10; j++) {
      // hoist a out of both loops
      int a = 10;
    }
  }
}
*/
TEST_F(PassClassTest, NestedDoubleHoist) {
  const std::string before_hoist = R"(OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main"
OpExecutionMode %main OriginUpperLeft
OpSource GLSL 440
OpName %main "main"
OpName %i "i"
OpName %j "j"
OpName %a "a"
%void = OpTypeVoid
%7 = OpTypeFunction %void
%int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
%int_0 = OpConstant %int 0
%int_10 = OpConstant %int 10
%bool = OpTypeBool
%int_1 = OpConstant %int 1
%main = OpFunction %void None %7
%14 = OpLabel
%i = OpVariable %_ptr_Function_int Function
%j = OpVariable %_ptr_Function_int Function
%a = OpVariable %_ptr_Function_int Function
OpStore %i %int_0
OpBranch %15
%15 = OpLabel
OpLoopMerge %16 %17 None
OpBranch %18
%18 = OpLabel
%19 = OpLoad %int %i
%20 = OpSLessThan %bool %19 %int_10
OpBranchConditional %20 %21 %16
%21 = OpLabel
OpStore %j %int_0
OpBranch %22
%22 = OpLabel
OpLoopMerge %23 %24 None
OpBranch %25
%25 = OpLabel
%26 = OpLoad %int %j
%27 = OpSLessThan %bool %26 %int_10
OpBranchConditional %27 %28 %23
%28 = OpLabel
OpStore %a %int_10
OpBranch %24
%24 = OpLabel
%29 = OpLoad %int %j
%30 = OpIAdd %int %29 %int_1
OpStore %j %30
OpBranch %22
%23 = OpLabel
OpBranch %17
%17 = OpLabel
%31 = OpLoad %int %i
%32 = OpIAdd %int %31 %int_1
OpStore %i %32
OpBranch %15
%16 = OpLabel
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
OpName %i "i"
OpName %j "j"
OpName %a "a"
%void = OpTypeVoid
%7 = OpTypeFunction %void
%int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
%int_0 = OpConstant %int 0
%int_10 = OpConstant %int 10
%bool = OpTypeBool
%int_1 = OpConstant %int 1
%main = OpFunction %void None %7
%14 = OpLabel
%i = OpVariable %_ptr_Function_int Function
%j = OpVariable %_ptr_Function_int Function
%a = OpVariable %_ptr_Function_int Function
OpStore %i %int_0
OpStore %a %int_10
OpBranch %15
%15 = OpLabel
OpLoopMerge %16 %17 None
OpBranch %18
%18 = OpLabel
%19 = OpLoad %int %i
%20 = OpSLessThan %bool %19 %int_10
OpBranchConditional %20 %21 %16
%21 = OpLabel
OpStore %j %int_0
OpBranch %22
%22 = OpLabel
OpLoopMerge %23 %24 None
OpBranch %25
%25 = OpLabel
%26 = OpLoad %int %j
%27 = OpSLessThan %bool %26 %int_10
OpBranchConditional %27 %28 %23
%28 = OpLabel
OpBranch %24
%24 = OpLabel
%29 = OpLoad %int %j
%30 = OpIAdd %int %29 %int_1
OpStore %j %30
OpBranch %22
%23 = OpLabel
OpBranch %17
%17 = OpLabel
%31 = OpLoad %int %i
%32 = OpIAdd %int %31 %int_1
OpStore %i %32
OpBranch %15
%16 = OpLabel
OpReturn
OpFunctionEnd
)";

  SinglePassRunAndCheck<opt::LICMPass>(before_hoist, after_hoist, true);
}

}  // namespace





