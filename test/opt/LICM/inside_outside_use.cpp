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

#include <memory>
#include <string>
#include <vector>

#include "../assembly_builder.h"
#include "../function_utils.h"
#include "../pass_fixture.h"
#include "../pass_utils.h"
#include "opt/dominator_analysis.h"
#include "opt/pass.h"

namespace {

using namespace spvtools;
using ::testing::UnorderedElementsAre;

using PassClassTest = PassTest<::testing::Test>;

/*
  Generated from the following GLSL fragment shader
--eliminate-local-multi-store has also been run on the spv binary
#version 440 core
layout(location = 0) out vec4 c;
layout(location = 1) in vec4 in_val;
void main(){
  int a = 1;
  int b = 2;
  int hoist = 0;
  c = vec4(0,0,0,0);
  for (int i = int(in_val.x); i < int(in_val.y); i++) {
    // invariant
    hoist = a + b;
    // don't hoist c
    c = vec4(i,i,i,i);
  }
  int x = hoist;
}
*/
TEST_F(PassClassTest, InsideOutsideUse) {
  const std::string before_hoist = R"(OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %c %in_val
OpExecutionMode %main OriginUpperLeft
OpSource GLSL 440
OpName %main "main"
OpName %c "c"
OpName %in_val "in_val"
OpDecorate %c Location 0
OpDecorate %in_val Location 1
%void = OpTypeVoid
%6 = OpTypeFunction %void
%int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
%int_1 = OpConstant %int 1
%float = OpTypeFloat 32
%v4float = OpTypeVector %float 4
%_ptr_Output_v4float = OpTypePointer Output %v4float
%c = OpVariable %_ptr_Output_v4float Output
%float_0 = OpConstant %float 0
%14 = OpConstantComposite %v4float %float_0 %float_0 %float_0 %float_0
%_ptr_Input_v4float = OpTypePointer Input %v4float
%in_val = OpVariable %_ptr_Input_v4float Input
%uint = OpTypeInt 32 0
%uint_0 = OpConstant %uint 0
%_ptr_Input_float = OpTypePointer Input %float
%uint_1 = OpConstant %uint 1
%bool = OpTypeBool
%main = OpFunction %void None %6
%21 = OpLabel
OpStore %c %14
%22 = OpAccessChain %_ptr_Input_float %in_val %uint_0
%23 = OpLoad %float %22
%24 = OpConvertFToS %int %23
OpBranch %25
%25 = OpLabel
%26 = OpPhi %int %24 %21 %27 %28
OpLoopMerge %29 %28 None
OpBranch %30
%30 = OpLabel
%31 = OpAccessChain %_ptr_Input_float %in_val %uint_1
%32 = OpLoad %float %31
%33 = OpConvertFToS %int %32
%34 = OpSLessThan %bool %26 %33
OpBranchConditional %34 %35 %29
%35 = OpLabel
%36 = OpConvertSToF %float %26
%37 = OpConvertSToF %float %26
%38 = OpConvertSToF %float %26
%39 = OpConvertSToF %float %26
%40 = OpCompositeConstruct %v4float %36 %37 %38 %39
OpStore %c %40
OpBranch %28
%28 = OpLabel
%27 = OpIAdd %int %26 %int_1
OpBranch %25
%29 = OpLabel
OpReturn
OpFunctionEnd
)";

  const std::string after_hoist = R"(
)";

  SinglePassRunAndCheck<opt::LICMPass>(before_hoist, after_hoist, true);
}

}  // namespace
