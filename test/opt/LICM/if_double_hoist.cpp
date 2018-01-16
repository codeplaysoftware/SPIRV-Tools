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
void main() {
  int a = 1;
  for (int i = 0; i < 10; i++) {
    for (int j = 0; j < i; j++) {
      if (a == 1) {
        a = 1;
      }
    }
  }
}
*/
TEST_F(PassClassTest, IfDoubleHoist) {
  const std::string before_hoist = R"(OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main"
OpExecutionMode %main OriginUpperLeft
OpSource GLSL 440
OpName %main "main"
OpName %a "a"
OpName %i "i"
OpName %j "j"
%void = OpTypeVoid
%7 = OpTypeFunction %void
%int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
%int_1 = OpConstant %int 1
%int_0 = OpConstant %int 0
%int_10 = OpConstant %int 10
%bool = OpTypeBool
%main = OpFunction %void None %7
%14 = OpLabel
%a = OpVariable %_ptr_Function_int Function
%i = OpVariable %_ptr_Function_int Function
%j = OpVariable %_ptr_Function_int Function
OpStore %a %int_1
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
%27 = OpLoad %int %i
%28 = OpSLessThan %bool %26 %27
OpBranchConditional %28 %29 %23
%29 = OpLabel
%30 = OpLoad %int %a
%31 = OpIEqual %bool %30 %int_1
OpSelectionMerge %32 None
OpBranchConditional %31 %33 %32
%33 = OpLabel
OpStore %a %int_1
OpBranch %32
%32 = OpLabel
OpBranch %24
%24 = OpLabel
%34 = OpLoad %int %j
%35 = OpIAdd %int %34 %int_1
OpStore %j %35
OpBranch %22
%23 = OpLabel
OpBranch %17
%17 = OpLabel
%36 = OpLoad %int %i
%37 = OpIAdd %int %36 %int_1
OpStore %i %37
OpBranch %15
%16 = OpLabel
OpReturn
OpFunctionEnd
)";

  const std::string after_hoist = R"(
)";

  SinglePassRunAndCheck<opt::LICMPass>(before_hoist, after_hoist, true);
}

}  // namespace
