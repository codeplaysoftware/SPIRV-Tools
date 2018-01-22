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
  }
}
*/
TEST_F(PassClassTest, EmptyLoopHoist) {
  const std::string before_hoist = R"(OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main"
OpExecutionMode %main OriginUpperLeft
OpSource GLSL 440
OpName %main "main"
OpName %i "i"
%void = OpTypeVoid
%5 = OpTypeFunction %void
%int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
%int_0 = OpConstant %int 0
%int_10 = OpConstant %int 10
%bool = OpTypeBool
%int_1 = OpConstant %int 1
%main = OpFunction %void None %5
%12 = OpLabel
%i = OpVariable %_ptr_Function_int Function
OpStore %i %int_0
OpBranch %13
%13 = OpLabel
OpLoopMerge %14 %15 None
OpBranch %16
%16 = OpLabel
%17 = OpLoad %int %i
%18 = OpSLessThan %bool %17 %int_10
OpBranchConditional %18 %19 %14
%19 = OpLabel
OpBranch %15
%15 = OpLabel
%20 = OpLoad %int %i
%21 = OpIAdd %int %20 %int_1
OpStore %i %21
OpBranch %13
%14 = OpLabel
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
%void = OpTypeVoid
%5 = OpTypeFunction %void
%int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
%int_0 = OpConstant %int 0
%int_10 = OpConstant %int 10
%bool = OpTypeBool
%int_1 = OpConstant %int 1
%main = OpFunction %void None %5
%12 = OpLabel
%i = OpVariable %_ptr_Function_int Function
OpStore %i %int_0
OpBranch %13
%13 = OpLabel
OpLoopMerge %14 %15 None
OpBranch %16
%16 = OpLabel
%17 = OpLoad %int %i
%18 = OpSLessThan %bool %17 %int_10
OpBranchConditional %18 %19 %14
%19 = OpLabel
OpBranch %15
%15 = OpLabel
%20 = OpLoad %int %i
%21 = OpIAdd %int %20 %int_1
OpStore %i %21
OpBranch %13
%14 = OpLabel
OpReturn
OpFunctionEnd
)";

  SinglePassRunAndCheck<opt::LICMPass>(before_hoist, after_hoist, true);
}

}  // namespace
