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
#include <iostream>

#include "../assembly_builder.h"
#include "../function_utils.h"
#include "../pass_fixture.h"
#include "../pass_utils.h"
#include "opt/pass.h"
#include "opt/loop_descriptor.h"
#include "opt/licm_pass.h"

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
}
*/
TEST_F(PassClassTest, HoistNoHoist) {
  const std::string start = R"(OpCapability Shader
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
%int_2 = OpConstant %int 2
%int_0 = OpConstant %int 0
%float = OpTypeFloat 32
%v4float = OpTypeVector %float 4
%_ptr_Output_v4float = OpTypePointer Output %v4float
%c = OpVariable %_ptr_Output_v4float Output
%float_0 = OpConstant %float 0
%16 = OpConstantComposite %v4float %float_0 %float_0 %float_0 %float_0
%_ptr_Input_v4float = OpTypePointer Input %v4float
%in_val = OpVariable %_ptr_Input_v4float Input
%uint = OpTypeInt 32 0
%uint_0 = OpConstant %uint 0
%_ptr_Input_float = OpTypePointer Input %float
%uint_1 = OpConstant %uint 1
%bool = OpTypeBool
%main = OpFunction %void None %6
%23 = OpLabel
OpStore %c %16
%24 = OpAccessChain %_ptr_Input_float %in_val %uint_0
%25 = OpLoad %float %24
%26 = OpConvertFToS %int %25
OpBranch %27
%27 = OpLabel
%28 = OpPhi %int %int_0 %23 %29 %30
%31 = OpPhi %int %26 %23 %32 %30
OpLoopMerge %33 %30 None
OpBranch %34
%34 = OpLabel
%35 = OpAccessChain %_ptr_Input_float %in_val %uint_1
%36 = OpLoad %float %35
%37 = OpConvertFToS %int %36
%38 = OpSLessThan %bool %31 %37
OpBranchConditional %38 %39 %33
%39 = OpLabel
%29 = OpIAdd %int %int_1 %int_2
%40 = OpConvertSToF %float %31
%41 = OpConvertSToF %float %31
%42 = OpConvertSToF %float %31
%43 = OpConvertSToF %float %31
%44 = OpCompositeConstruct %v4float %40 %41 %42 %43
OpStore %c %44
OpBranch %30
%30 = OpLabel
%32 = OpIAdd %int %31 %int_1
OpBranch %27
%33 = OpLabel
OpReturn
OpFunctionEnd
)";


const std::string end = R"(
)";

  SinglePassRunAndCheck<opt::LICMPass>(start, end, true);
}

}  // namespace
