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
  const std::string before_hoist = R"(
    OpCapability Shader
    %1 = OpExtInstImport "GLSL.std.450"
         OpMemoryModel Logical GLSL450
         OpEntryPoint Fragment %4 "main"
         OpExecutionMode %4 OriginUpperLeft
         OpSource GLSL 440
         OpName %4 "main"
         OpName %8 "a"
         OpName %10 "b"
         OpName %12 "hoist"
         OpName %14 "i"
    %2 = OpTypeVoid
    %3 = OpTypeFunction %2
    %6 = OpTypeInt 32 1
    %7 = OpTypePointer Function %6
    %9 = OpConstant %6 1
   %11 = OpConstant %6 2
   %13 = OpConstant %6 0
   %21 = OpConstant %6 10
   %22 = OpTypeBool
    %4 = OpFunction %2 None %3
    %5 = OpLabel
    %8 = OpVariable %7 Function
   %10 = OpVariable %7 Function
   %12 = OpVariable %7 Function
   %14 = OpVariable %7 Function
         OpStore %8 %9
         OpStore %10 %11
         OpStore %12 %13
         OpStore %14 %13
         OpBranch %15
   %15 = OpLabel
         OpLoopMerge %17 %18 None
         OpBranch %19
   %19 = OpLabel
   %20 = OpLoad %6 %14
   %23 = OpSLessThan %22 %20 %21
         OpBranchConditional %23 %16 %17
   %16 = OpLabel
   %24 = OpLoad %6 %8
   %25 = OpLoad %6 %10
   %26 = OpIAdd %6 %24 %25
         OpStore %12 %26
         OpBranch %18
   %18 = OpLabel
   %27 = OpLoad %6 %14
   %28 = OpIAdd %6 %27 %9
         OpStore %14 %28
         OpBranch %15
   %17 = OpLabel
         OpReturn
         OpFunctionEnd
  )";

  const std::string after_hoist = R"(
    OpCapability Shader
    %1 = OpExtInstImport "GLSL.std.450"
         OpMemoryModel Logical GLSL450
         OpEntryPoint Fragment %4 "main"
         OpExecutionMode %4 OriginUpperLeft
         OpSource GLSL 440
         OpName %4 "main"
         OpName %8 "a"
         OpName %10 "b"
         OpName %12 "hoist"
         OpName %17 "i"
    %2 = OpTypeVoid
    %3 = OpTypeFunction %2
    %6 = OpTypeInt 32 1
    %7 = OpTypePointer Function %6
    %9 = OpConstant %6 1
   %11 = OpConstant %6 2
   %13 = OpConstant %6 0
   %24 = OpConstant %6 10
   %25 = OpTypeBool
    %4 = OpFunction %2 None %3
    %5 = OpLabel
    %8 = OpVariable %7 Function
   %10 = OpVariable %7 Function
   %12 = OpVariable %7 Function
   %17 = OpVariable %7 Function
         OpStore %8 %9
         OpStore %10 %11
         OpStore %12 %13
   %14 = OpLoad %6 %8
   %15 = OpLoad %6 %10
   %16 = OpIAdd %6 %14 %15
         OpStore %12 %16
         OpStore %17 %13
         OpBranch %18
   %18 = OpLabel
         OpLoopMerge %20 %21 None
         OpBranch %22
   %22 = OpLabel
   %23 = OpLoad %6 %17
   %26 = OpSLessThan %25 %23 %24
         OpBranchConditional %26 %19 %20
   %19 = OpLabel
         OpBranch %21
   %21 = OpLabel
   %27 = OpLoad %6 %17
   %28 = OpIAdd %6 %27 %9
         OpStore %17 %28
         OpBranch %18
   %20 = OpLabel
         OpReturn
         OpFunctionEnd
)";

  SinglePassRunAndCheck<opt::LICMPass>(before_hoist, after_hoist, true);
}

}  // namespace
