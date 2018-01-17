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
OpName %a "a"
OpName %b "b"
OpName %hoist "hoist"
OpName %c "c"
OpName %i "i"
OpName %in_val "in_val"
OpName %x "x"
OpDecorate %c Location 0
OpDecorate %in_val Location 1
%void = OpTypeVoid
%11 = OpTypeFunction %void
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
%21 = OpConstantComposite %v4float %float_0 %float_0 %float_0 %float_0
%_ptr_Input_v4float = OpTypePointer Input %v4float
%in_val = OpVariable %_ptr_Input_v4float Input
%uint = OpTypeInt 32 0
%uint_0 = OpConstant %uint 0
%_ptr_Input_float = OpTypePointer Input %float
%uint_1 = OpConstant %uint 1
%bool = OpTypeBool
%main = OpFunction %void None %11
%28 = OpLabel
%a = OpVariable %_ptr_Function_int Function
%b = OpVariable %_ptr_Function_int Function
%hoist = OpVariable %_ptr_Function_int Function
%i = OpVariable %_ptr_Function_int Function
%x = OpVariable %_ptr_Function_int Function
OpStore %a %int_1
OpStore %b %int_2
OpStore %hoist %int_0
OpStore %c %21
%29 = OpAccessChain %_ptr_Input_float %in_val %uint_0
%30 = OpLoad %float %29
%31 = OpConvertFToS %int %30
OpStore %i %31
OpBranch %32
%32 = OpLabel
OpLoopMerge %33 %34 None
OpBranch %35
%35 = OpLabel
%36 = OpLoad %int %i
%37 = OpAccessChain %_ptr_Input_float %in_val %uint_1
%38 = OpLoad %float %37
%39 = OpConvertFToS %int %38
%40 = OpSLessThan %bool %36 %39
OpBranchConditional %40 %41 %33
%41 = OpLabel
%42 = OpLoad %int %a
%43 = OpLoad %int %b
%44 = OpIAdd %int %42 %43
OpStore %hoist %44
%45 = OpLoad %int %i
%46 = OpConvertSToF %float %45
%47 = OpLoad %int %i
%48 = OpConvertSToF %float %47
%49 = OpLoad %int %i
%50 = OpConvertSToF %float %49
%51 = OpLoad %int %i
%52 = OpConvertSToF %float %51
%53 = OpCompositeConstruct %v4float %46 %48 %50 %52
OpStore %c %53
OpBranch %34
%34 = OpLabel
%54 = OpLoad %int %i
%55 = OpIAdd %int %54 %int_1
OpStore %i %55
OpBranch %32
%33 = OpLabel
%56 = OpLoad %int %hoist
OpStore %x %56
OpReturn
OpFunctionEnd
)";

  const std::string after_hoist = R"(OpCapability Shader
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
OpName %x "x"
OpDecorate %c Location 0
OpDecorate %in_val Location 1
%void = OpTypeVoid
%11 = OpTypeFunction %void
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
%21 = OpConstantComposite %v4float %float_0 %float_0 %float_0 %float_0
%_ptr_Input_v4float = OpTypePointer Input %v4float
%in_val = OpVariable %_ptr_Input_v4float Input
%uint = OpTypeInt 32 0
%uint_0 = OpConstant %uint 0
%_ptr_Input_float = OpTypePointer Input %float
%uint_1 = OpConstant %uint 1
%bool = OpTypeBool
%main = OpFunction %void None %11
%28 = OpLabel
%a = OpVariable %_ptr_Function_int Function
%b = OpVariable %_ptr_Function_int Function
%hoist = OpVariable %_ptr_Function_int Function
%i = OpVariable %_ptr_Function_int Function
%x = OpVariable %_ptr_Function_int Function
OpStore %a %int_1
OpStore %b %int_2
OpStore %hoist %int_0
OpStore %c %21
%29 = OpAccessChain %_ptr_Input_float %in_val %uint_0
%30 = OpLoad %float %29
%31 = OpConvertFToS %int %30
OpStore %i %31
%42 = OpLoad %int %a
%43 = OpLoad %int %b
%44 = OpIAdd %int %42 %43
OpStore %hoist %44
OpBranch %32
%32 = OpLabel
OpLoopMerge %33 %34 None
OpBranch %35
%35 = OpLabel
%36 = OpLoad %int %i
%37 = OpAccessChain %_ptr_Input_float %in_val %uint_1
%38 = OpLoad %float %37
%39 = OpConvertFToS %int %38
%40 = OpSLessThan %bool %36 %39
OpBranchConditional %40 %41 %33
%41 = OpLabel
%45 = OpLoad %int %i
%46 = OpConvertSToF %float %45
%47 = OpLoad %int %i
%48 = OpConvertSToF %float %47
%49 = OpLoad %int %i
%50 = OpConvertSToF %float %49
%51 = OpLoad %int %i
%52 = OpConvertSToF %float %51
%53 = OpCompositeConstruct %v4float %46 %48 %50 %52
OpStore %c %53
OpBranch %34
%34 = OpLabel
%54 = OpLoad %int %i
%55 = OpIAdd %int %54 %int_1
OpStore %i %55
OpBranch %32
%33 = OpLabel
%56 = OpLoad %int %hoist
OpStore %x %56
OpReturn
OpFunctionEnd
)";

  SinglePassRunAndCheck<opt::LICMPass>(before_hoist, after_hoist, true);
}

}  // namespace
