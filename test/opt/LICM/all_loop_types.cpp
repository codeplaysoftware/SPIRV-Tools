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
%void = OpTypeVoid
%4 = OpTypeFunction %void
%int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
%int_0 = OpConstant %int 0
%int_10 = OpConstant %int 10
%bool = OpTypeBool
%int_1 = OpConstant %int 1
%main = OpFunction %void None %4
%11 = OpLabel
OpBranch %12
%12 = OpLabel
%13 = OpPhi %int %int_0 %11 %14 %15
OpLoopMerge %16 %15 None
OpBranch %17
%17 = OpLabel
%18 = OpSLessThan %bool %13 %int_10
OpBranchConditional %18 %19 %16
%19 = OpLabel
OpBranch %15
%15 = OpLabel
%14 = OpIAdd %int %13 %int_1
OpBranch %12
%16 = OpLabel
OpBranch %20
%20 = OpLabel
%21 = OpPhi %int %int_0 %16 %22 %23
OpLoopMerge %24 %23 None
OpBranch %25
%25 = OpLabel
%26 = OpSLessThan %bool %21 %int_10
OpBranchConditional %26 %27 %24
%27 = OpLabel
%22 = OpIAdd %int %21 %int_1
OpBranch %23
%23 = OpLabel
OpBranch %20
%24 = OpLabel
OpBranch %28
%28 = OpLabel
%29 = OpPhi %int %int_0 %24 %30 %31
OpLoopMerge %32 %31 None
OpBranch %33
%33 = OpLabel
%30 = OpIAdd %int %29 %int_1
OpBranch %31
%31 = OpLabel
%34 = OpSLessThan %bool %30 %int_10
OpBranchConditional %34 %28 %32
%32 = OpLabel
OpBranch %35
%35 = OpLabel
%36 = OpPhi %int %int_0 %32 %37 %38
%39 = OpPhi %int %int_0 %32 %40 %38
%41 = OpPhi %int %int_0 %32 %42 %38
%43 = OpPhi %int %int_0 %32 %44 %38
OpLoopMerge %45 %38 None
OpBranch %46
%46 = OpLabel
%47 = OpSLessThan %bool %39 %int_10
OpBranchConditional %47 %48 %45
%48 = OpLabel
OpBranch %49
%49 = OpLabel
%37 = OpPhi %int %36 %48 %int_1 %50
%42 = OpPhi %int %41 %48 %51 %50
%44 = OpPhi %int %43 %48 %52 %50
OpLoopMerge %53 %50 None
OpBranch %54
%54 = OpLabel
%55 = OpSLessThan %bool %42 %int_10
OpBranchConditional %55 %56 %53
%56 = OpLabel
OpBranch %57
%57 = OpLabel
%58 = OpPhi %int %37 %56 %int_1 %59
%60 = OpPhi %int %44 %56 %52 %59
OpLoopMerge %61 %59 None
OpBranch %62
%62 = OpLabel
%52 = OpIAdd %int %60 %int_1
OpBranch %59
%59 = OpLabel
%63 = OpSLessThan %bool %52 %int_10
OpBranchConditional %63 %57 %61
%61 = OpLabel
%51 = OpIAdd %int %42 %int_1
OpBranch %50
%50 = OpLabel
OpBranch %49
%53 = OpLabel
OpBranch %38
%38 = OpLabel
%40 = OpIAdd %int %39 %int_1
OpBranch %35
%45 = OpLabel
OpReturn
OpFunctionEnd
)";

  const std::string after_hoist = R"(
)";

  SinglePassRunAndCheck<opt::LICMPass>(before_hoist, after_hoist, true);
}

}  // namespace
