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
%void = OpTypeVoid
%4 = OpTypeFunction %void
%int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
%int_1 = OpConstant %int 1
%int_0 = OpConstant %int 0
%int_10 = OpConstant %int 10
%bool = OpTypeBool
%int_2 = OpConstant %int 2
%main = OpFunction %void None %4
%12 = OpLabel
OpBranch %13
%13 = OpLabel
%14 = OpPhi %int %int_1 %12 %15 %16
%17 = OpPhi %int %int_1 %12 %18 %16
%19 = OpPhi %int %int_0 %12 %20 %16
OpLoopMerge %21 %16 None
OpBranch %22
%22 = OpLabel
%23 = OpSLessThan %bool %19 %int_10
OpBranchConditional %23 %24 %21
%24 = OpLabel
%25 = OpIEqual %bool %14 %int_1
OpSelectionMerge %26 None
OpBranchConditional %25 %27 %28
%27 = OpLabel
OpBranch %26
%28 = OpLabel
OpBranch %26
%26 = OpLabel
%15 = OpPhi %int %int_2 %27 %14 %28
%18 = OpPhi %int %17 %27 %int_1 %28
OpBranch %16
%16 = OpLabel
%20 = OpIAdd %int %19 %int_1
OpBranch %13
%21 = OpLabel
OpReturn
OpFunctionEnd
)";

  const std::string after_hoist = R"(
)";

  SinglePassRunAndCheck<opt::LICMPass>(before_hoist, after_hoist, true);
}

}  // namespace
