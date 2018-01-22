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
  int i_1 = 0;
  for (i_1 = 0; i_1 < 10; i_1++) {
  }
  int i_2 = 0;
  while (i_2 < 10) {
    i_2++;
  }
  int i_3 = 0;
  do {
    i_3++;
  } while (i_3 < 10);
  int hoist = 0;
  int i_4 = 0;
  int i_5 = 0;
  int i_6 = 0;
  for (i_4 = 0; i_4 < 10; i_4++) {
    while (i_5 < 10) {
      do {
        hoist = 1;
        i_6++;
      } while (i_6 < 10);
      i_5++;
    }
  }
}
*/
TEST_F(PassClassTest, AllLoopTypes) {
  const std::string before_hoist = R"(OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main"
OpExecutionMode %main OriginUpperLeft
OpSource GLSL 440
OpName %main "main"
OpName %i_1 "i_1"
OpName %i_2 "i_2"
OpName %i_3 "i_3"
OpName %hoist "hoist"
OpName %i_4 "i_4"
OpName %i_5 "i_5"
OpName %i_6 "i_6"
%void = OpTypeVoid
%11 = OpTypeFunction %void
%int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
%int_0 = OpConstant %int 0
%int_10 = OpConstant %int 10
%bool = OpTypeBool
%int_1 = OpConstant %int 1
%main = OpFunction %void None %11
%18 = OpLabel
%i_1 = OpVariable %_ptr_Function_int Function
%i_2 = OpVariable %_ptr_Function_int Function
%i_3 = OpVariable %_ptr_Function_int Function
%hoist = OpVariable %_ptr_Function_int Function
%i_4 = OpVariable %_ptr_Function_int Function
%i_5 = OpVariable %_ptr_Function_int Function
%i_6 = OpVariable %_ptr_Function_int Function
OpStore %i_1 %int_0
OpStore %i_1 %int_0
OpBranch %19
%19 = OpLabel
OpLoopMerge %20 %21 None
OpBranch %22
%22 = OpLabel
%23 = OpLoad %int %i_1
%24 = OpSLessThan %bool %23 %int_10
OpBranchConditional %24 %25 %20
%25 = OpLabel
OpBranch %21
%21 = OpLabel
%26 = OpLoad %int %i_1
%27 = OpIAdd %int %26 %int_1
OpStore %i_1 %27
OpBranch %19
%20 = OpLabel
OpStore %i_2 %int_0
OpBranch %28
%28 = OpLabel
OpLoopMerge %29 %30 None
OpBranch %31
%31 = OpLabel
%32 = OpLoad %int %i_2
%33 = OpSLessThan %bool %32 %int_10
OpBranchConditional %33 %34 %29
%34 = OpLabel
%35 = OpLoad %int %i_2
%36 = OpIAdd %int %35 %int_1
OpStore %i_2 %36
OpBranch %30
%30 = OpLabel
OpBranch %28
%29 = OpLabel
OpStore %i_3 %int_0
OpBranch %37
%37 = OpLabel
OpLoopMerge %38 %39 None
OpBranch %40
%40 = OpLabel
%41 = OpLoad %int %i_3
%42 = OpIAdd %int %41 %int_1
OpStore %i_3 %42
OpBranch %39
%39 = OpLabel
%43 = OpLoad %int %i_3
%44 = OpSLessThan %bool %43 %int_10
OpBranchConditional %44 %37 %38
%38 = OpLabel
OpStore %hoist %int_0
OpStore %i_4 %int_0
OpStore %i_5 %int_0
OpStore %i_6 %int_0
OpStore %i_4 %int_0
OpBranch %45
%45 = OpLabel
OpLoopMerge %46 %47 None
OpBranch %48
%48 = OpLabel
%49 = OpLoad %int %i_4
%50 = OpSLessThan %bool %49 %int_10
OpBranchConditional %50 %51 %46
%51 = OpLabel
OpBranch %52
%52 = OpLabel
OpLoopMerge %53 %54 None
OpBranch %55
%55 = OpLabel
%56 = OpLoad %int %i_5
%57 = OpSLessThan %bool %56 %int_10
OpBranchConditional %57 %58 %53
%58 = OpLabel
OpBranch %59
%59 = OpLabel
OpLoopMerge %60 %61 None
OpBranch %62
%62 = OpLabel
OpStore %hoist %int_1
%63 = OpLoad %int %i_6
%64 = OpIAdd %int %63 %int_1
OpStore %i_6 %64
OpBranch %61
%61 = OpLabel
%65 = OpLoad %int %i_6
%66 = OpSLessThan %bool %65 %int_10
OpBranchConditional %66 %59 %60
%60 = OpLabel
%67 = OpLoad %int %i_5
%68 = OpIAdd %int %67 %int_1
OpStore %i_5 %68
OpBranch %54
%54 = OpLabel
OpBranch %52
%53 = OpLabel
OpBranch %47
%47 = OpLabel
%69 = OpLoad %int %i_4
%70 = OpIAdd %int %69 %int_1
OpStore %i_4 %70
OpBranch %45
%46 = OpLabel
OpReturn
OpFunctionEnd
)";

  const std::string after_hoist = R"(
)";

  SinglePassRunAndCheck<opt::LICMPass>(before_hoist, after_hoist, true);
}

}  // namespace
