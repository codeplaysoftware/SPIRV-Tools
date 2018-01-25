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
    // won't hoist the if/else because a is assigned to twice
    if (a == 1) {
      a = 2;
    } else {
      a = 3;
    }
  }
}
*/
TEST_F(PassClassTest, IfElseNoHoist) {
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
%int_2 = OpConstant %int 2
%int_3 = OpConstant %int 3
%main = OpFunction %void None %4
%13 = OpLabel
OpBranch %14
%14 = OpLabel
%15 = OpPhi %int %int_1 %13 %16 %17
%18 = OpPhi %int %int_0 %13 %19 %17
OpLoopMerge %20 %17 None
OpBranch %21
%21 = OpLabel
%22 = OpSLessThan %bool %18 %int_10
OpBranchConditional %22 %23 %20
%23 = OpLabel
%24 = OpIEqual %bool %15 %int_1
OpSelectionMerge %25 None
OpBranchConditional %24 %26 %27
%26 = OpLabel
OpBranch %25
%27 = OpLabel
OpBranch %25
%25 = OpLabel
%16 = OpPhi %int %int_2 %26 %int_3 %27
OpBranch %17
%17 = OpLabel
%19 = OpIAdd %int %18 %int_1
OpBranch %14
%20 = OpLabel
OpReturn
OpFunctionEnd
)";

  const std::string after_hoist = R"(
)";

  SinglePassRunAndCheck<opt::LICMPass>(before_hoist, after_hoist, true);
}

}  // namespace
