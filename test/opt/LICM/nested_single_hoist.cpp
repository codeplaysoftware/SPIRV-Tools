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
      // hoist a out of j loop, but not j loop
      int a = i;
    }
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
OpName %i "i"
OpName %j "j"
OpName %a "a"
%void = OpTypeVoid
%3 = OpTypeFunction %void
%int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
%int_0 = OpConstant %int 0
%int_10 = OpConstant %int 10
%bool = OpTypeBool
%int_1 = OpConstant %int 1
%main = OpFunction %void None %3
%5 = OpLabel
%i = OpVariable %_ptr_Function_int Function
%j = OpVariable %_ptr_Function_int Function
%a = OpVariable %_ptr_Function_int Function
OpStore %i %int_0
OpBranch %10
%10 = OpLabel
OpLoopMerge %12 %13 None
OpBranch %14
%14 = OpLabel
%15 = OpLoad %int %i
%18 = OpSLessThan %bool %15 %int_10
OpBranchConditional %18 %11 %12
%11 = OpLabel
OpStore %j %int_0
OpBranch %20
%20 = OpLabel
OpLoopMerge %22 %23 None
OpBranch %24
%24 = OpLabel
%25 = OpLoad %int %j
%26 = OpSLessThan %bool %25 %int_10
OpBranchConditional %26 %21 %22
%21 = OpLabel
%28 = OpLoad %int %i
OpStore %a %28
OpBranch %23
%23 = OpLabel
%29 = OpLoad %int %j
%31 = OpIAdd %int %29 %int_1
OpStore %j %31
OpBranch %20
%22 = OpLabel
OpBranch %13
%13 = OpLabel
%32 = OpLoad %int %i
%33 = OpIAdd %int %32 %int_1
OpStore %i %33
OpBranch %10
%12 = OpLabel
OpReturn
OpFunctionEnd
)";


  const std::string after_hoist = R"()";

  SinglePassRunAndCheck<opt::LICMPass>(before_hoist, after_hoist, true);
}

}  // namespace





