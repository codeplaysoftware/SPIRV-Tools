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
  Generated from the following GLSL
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
OpName %a "a"
OpName %b "b"
OpName %hoist "hoist"
OpName %c "c"
OpName %i "i"
OpName %in_val "in_val"
OpDecorate %c Location 0
OpDecorate %in_val Location 1
%void = OpTypeVoid
%10 = OpTypeFunction %void
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
%20 = OpConstantComposite %v4float %float_0 %float_0 %float_0 %float_0
%_ptr_Input_v4float = OpTypePointer Input %v4float
%in_val = OpVariable %_ptr_Input_v4float Input
%uint = OpTypeInt 32 0
%uint_0 = OpConstant %uint 0
%_ptr_Input_float = OpTypePointer Input %float
%uint_1 = OpConstant %uint 1
%bool = OpTypeBool
%main = OpFunction %void None %10
%27 = OpLabel
%a = OpVariable %_ptr_Function_int Function
%b = OpVariable %_ptr_Function_int Function
%hoist = OpVariable %_ptr_Function_int Function
%i = OpVariable %_ptr_Function_int Function
OpStore %a %int_1
OpStore %b %int_2
OpStore %hoist %int_0
OpStore %c %20
%28 = OpAccessChain %_ptr_Input_float %in_val %uint_0
%29 = OpLoad %float %28
%30 = OpConvertFToS %int %29
OpStore %i %30
OpBranch %31
%31 = OpLabel
OpLoopMerge %32 %33 None
OpBranch %34
%34 = OpLabel
%35 = OpLoad %int %i
%36 = OpAccessChain %_ptr_Input_float %in_val %uint_1
%37 = OpLoad %float %36
%38 = OpConvertFToS %int %37
%39 = OpSLessThan %bool %35 %38
OpBranchConditional %39 %40 %32
%40 = OpLabel
%41 = OpLoad %int %a
%42 = OpLoad %int %b
%43 = OpIAdd %int %41 %42
OpStore %hoist %43
%44 = OpLoad %int %i
%45 = OpConvertSToF %float %44
%46 = OpLoad %int %i
%47 = OpConvertSToF %float %46
%48 = OpLoad %int %i
%49 = OpConvertSToF %float %48
%50 = OpLoad %int %i
%51 = OpConvertSToF %float %50
%52 = OpCompositeConstruct %v4float %45 %47 %49 %51
OpStore %c %52
OpBranch %33
%33 = OpLabel
%53 = OpLoad %int %i
%54 = OpIAdd %int %53 %int_1
OpStore %i %54
OpBranch %31
%32 = OpLabel
OpReturn
OpFunctionEnd
)";


const std::string end = R"(OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %c %in_val
OpExecutionMode %main OriginUpperLeft
OpSource GLSL 440
OpName %main "main"
OpName %a "a"
OpName %b "b"
OpName %hoist "hoist"
OpName %c "c"
OpName %i "i"
OpName %in_val "in_val"
OpDecorate %c Location 0
OpDecorate %in_val Location 1
%void = OpTypeVoid
%10 = OpTypeFunction %void
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
%20 = OpConstantComposite %v4float %float_0 %float_0 %float_0 %float_0
%_ptr_Input_v4float = OpTypePointer Input %v4float
%in_val = OpVariable %_ptr_Input_v4float Input
%uint = OpTypeInt 32 0
%uint_0 = OpConstant %uint 0
%_ptr_Input_float = OpTypePointer Input %float
%uint_1 = OpConstant %uint 1
%bool = OpTypeBool
%main = OpFunction %void None %10
%27 = OpLabel
%a = OpVariable %_ptr_Function_int Function
%b = OpVariable %_ptr_Function_int Function
%hoist = OpVariable %_ptr_Function_int Function
%i = OpVariable %_ptr_Function_int Function
OpStore %a %int_1
OpStore %b %int_2
OpStore %hoist %int_0
OpStore %c %20
%28 = OpAccessChain %_ptr_Input_float %in_val %uint_0
%29 = OpLoad %float %28
%30 = OpConvertFToS %int %29
OpStore %i %30
%41 = OpLoad %int %a
%42 = OpLoad %int %b
%43 = OpIAdd %int %41 %42
OpStore %hoist %43
OpBranch %31
%31 = OpLabel
OpLoopMerge %32 %33 None
OpBranch %34
%34 = OpLabel
%35 = OpLoad %int %i
%36 = OpAccessChain %_ptr_Input_float %in_val %uint_1
%37 = OpLoad %float %36
%38 = OpConvertFToS %int %37
%39 = OpSLessThan %bool %35 %38
OpBranchConditional %39 %40 %32
%40 = OpLabel
%44 = OpLoad %int %i
%45 = OpConvertSToF %float %44
%46 = OpLoad %int %i
%47 = OpConvertSToF %float %46
%48 = OpLoad %int %i
%49 = OpConvertSToF %float %48
%50 = OpLoad %int %i
%51 = OpConvertSToF %float %50
%52 = OpCompositeConstruct %v4float %45 %47 %49 %51
OpStore %c %52
OpBranch %33
%33 = OpLabel
%53 = OpLoad %int %i
%54 = OpIAdd %int %53 %int_1
OpStore %i %54
OpBranch %31
%32 = OpLabel
OpReturn
OpFunctionEnd
)";

  SinglePassRunAndCheck<opt::LICMPass>(start, end, true);
}

}  // namespace
