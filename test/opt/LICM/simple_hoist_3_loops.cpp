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
  for (int i = 0; i < 10; i++) {
    // invariant
    hoist = a + b;
  }
  int c = 1;
  int d = 2;
  int hoist2 = 0;
  for (int i = 0; i < 10; i++) {
    // invariant
    hoist2 = c + d;
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
OpName %i_0 "i"
OpName %c "c"
OpName %d "d"
OpName %hoist2 "hoist2"
OpName %i_1 "i"
%void = OpTypeVoid
%13 = OpTypeFunction %void
%int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
%int_1 = OpConstant %int 1
%int_2 = OpConstant %int 2
%int_0 = OpConstant %int 0
%int_10 = OpConstant %int 10
%bool = OpTypeBool
%main = OpFunction %void None %13
%21 = OpLabel
%a = OpVariable %_ptr_Function_int Function
%b = OpVariable %_ptr_Function_int Function
%hoist = OpVariable %_ptr_Function_int Function
%i = OpVariable %_ptr_Function_int Function
%i_0 = OpVariable %_ptr_Function_int Function
%c = OpVariable %_ptr_Function_int Function
%d = OpVariable %_ptr_Function_int Function
%hoist2 = OpVariable %_ptr_Function_int Function
%i_1 = OpVariable %_ptr_Function_int Function
OpStore %a %int_1
OpStore %b %int_2
OpStore %hoist %int_0
OpStore %i %int_0
OpBranch %22
%22 = OpLabel
OpLoopMerge %23 %24 None
OpBranch %25
%25 = OpLabel
%26 = OpLoad %int %i
%27 = OpSLessThan %bool %26 %int_10
OpBranchConditional %27 %28 %23
%28 = OpLabel
%29 = OpLoad %int %a
%30 = OpLoad %int %b
%31 = OpIAdd %int %29 %30
OpStore %hoist %31
OpBranch %24
%24 = OpLabel
%32 = OpLoad %int %i
%33 = OpIAdd %int %32 %int_1
OpStore %i %33
OpBranch %22
%23 = OpLabel
OpStore %i_0 %int_0
OpBranch %34
%34 = OpLabel
OpLoopMerge %35 %36 None
OpBranch %37
%37 = OpLabel
%38 = OpLoad %int %i_0
%39 = OpSLessThan %bool %38 %int_10
OpBranchConditional %39 %40 %35
%40 = OpLabel
%41 = OpLoad %int %a
%42 = OpLoad %int %b
%43 = OpIAdd %int %41 %42
OpStore %hoist %43
OpBranch %36
%36 = OpLabel
%44 = OpLoad %int %i_0
%45 = OpIAdd %int %44 %int_1
OpStore %i_0 %45
OpBranch %34
%35 = OpLabel
OpStore %c %int_1
OpStore %d %int_2
OpStore %hoist2 %int_0
OpStore %i_1 %int_0
OpBranch %46
%46 = OpLabel
OpLoopMerge %47 %48 None
OpBranch %49
%49 = OpLabel
%50 = OpLoad %int %i_1
%51 = OpSLessThan %bool %50 %int_10
OpBranchConditional %51 %52 %47
%52 = OpLabel
%53 = OpLoad %int %c
%54 = OpLoad %int %d
%55 = OpIAdd %int %53 %54
OpStore %hoist2 %55
OpBranch %48
%48 = OpLabel
%56 = OpLoad %int %i_1
%57 = OpIAdd %int %56 %int_1
OpStore %i_1 %57
OpBranch %46
%47 = OpLabel
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
OpName %i_0 "i"
OpName %c "c"
OpName %d "d"
OpName %hoist2 "hoist2"
OpName %i_1 "i"
%void = OpTypeVoid
%13 = OpTypeFunction %void
%int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
%int_1 = OpConstant %int 1
%int_2 = OpConstant %int 2
%int_0 = OpConstant %int 0
%int_10 = OpConstant %int 10
%bool = OpTypeBool
%main = OpFunction %void None %13
%21 = OpLabel
%a = OpVariable %_ptr_Function_int Function
%b = OpVariable %_ptr_Function_int Function
%hoist = OpVariable %_ptr_Function_int Function
%i = OpVariable %_ptr_Function_int Function
%i_0 = OpVariable %_ptr_Function_int Function
%c = OpVariable %_ptr_Function_int Function
%d = OpVariable %_ptr_Function_int Function
%hoist2 = OpVariable %_ptr_Function_int Function
%i_1 = OpVariable %_ptr_Function_int Function
OpStore %a %int_1
OpStore %b %int_2
OpStore %hoist %int_0
OpStore %i %int_0
%29 = OpLoad %int %a
%30 = OpLoad %int %b
%31 = OpIAdd %int %29 %30
OpStore %hoist %31
OpBranch %22
%22 = OpLabel
OpLoopMerge %23 %24 None
OpBranch %25
%25 = OpLabel
%26 = OpLoad %int %i
%27 = OpSLessThan %bool %26 %int_10
OpBranchConditional %27 %28 %23
%28 = OpLabel
OpBranch %24
%24 = OpLabel
%32 = OpLoad %int %i
%33 = OpIAdd %int %32 %int_1
OpStore %i %33
OpBranch %22
%23 = OpLabel
OpStore %i_0 %int_0
%41 = OpLoad %int %a
%42 = OpLoad %int %b
%43 = OpIAdd %int %41 %42
OpStore %hoist %43
OpBranch %34
%34 = OpLabel
OpLoopMerge %35 %36 None
OpBranch %37
%37 = OpLabel
%38 = OpLoad %int %i_0
%39 = OpSLessThan %bool %38 %int_10
OpBranchConditional %39 %40 %35
%40 = OpLabel
OpBranch %36
%36 = OpLabel
%44 = OpLoad %int %i_0
%45 = OpIAdd %int %44 %int_1
OpStore %i_0 %45
OpBranch %34
%35 = OpLabel
OpStore %c %int_1
OpStore %d %int_2
OpStore %hoist2 %int_0
OpStore %i_1 %int_0
%53 = OpLoad %int %c
%54 = OpLoad %int %d
%55 = OpIAdd %int %53 %54
OpStore %hoist2 %55
OpBranch %46
%46 = OpLabel
OpLoopMerge %47 %48 None
OpBranch %49
%49 = OpLabel
%50 = OpLoad %int %i_1
%51 = OpSLessThan %bool %50 %int_10
OpBranchConditional %51 %52 %47
%52 = OpLabel
OpBranch %48
%48 = OpLabel
%56 = OpLoad %int %i_1
%57 = OpIAdd %int %56 %int_1
OpStore %i_1 %57
OpBranch %46
%47 = OpLabel
OpReturn
OpFunctionEnd
)";

  SinglePassRunAndCheck<opt::LICMPass>(before_hoist, after_hoist, true);
}

}  // namespace
