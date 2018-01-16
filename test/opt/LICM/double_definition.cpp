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
layout(location = 1) out vec4 d;
layout(location = 2) in vec4 in_val;
void main(){
  int a = 1;
  int b = 2;
  int hoist = 0;
  c = vec4(0,0,0,0);
  for (int i = int(in_val.x); i < int(in_val.y); i++) {
    // hoist is not invariant, due to double definition
    hoist = a + b;
    c = vec4(i,i,i,i);
    hoist = 0;
    d = vec4(hoist, i, in_val.z, in_val.w);
  }
}
*/
TEST_F(PassClassTest, DoubleDefinition) {
  const std::string before_hoist = R"(OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %main "main" %c %in_val %d
OpExecutionMode %main OriginUpperLeft
OpSource GLSL 440
OpName %main "main"
OpName %a "a"
OpName %b "b"
OpName %hoist "hoist"
OpName %c "c"
OpName %i "i"
OpName %in_val "in_val"
OpName %d "d"
OpDecorate %c Location 0
OpDecorate %in_val Location 2
OpDecorate %d Location 1
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
%d = OpVariable %_ptr_Output_v4float Output
%uint_2 = OpConstant %uint 2
%uint_3 = OpConstant %uint 3
%main = OpFunction %void None %11
%30 = OpLabel
%a = OpVariable %_ptr_Function_int Function
%b = OpVariable %_ptr_Function_int Function
%hoist = OpVariable %_ptr_Function_int Function
%i = OpVariable %_ptr_Function_int Function
OpStore %a %int_1
OpStore %b %int_2
OpStore %hoist %int_0
OpStore %c %21
%31 = OpAccessChain %_ptr_Input_float %in_val %uint_0
%32 = OpLoad %float %31
%33 = OpConvertFToS %int %32
OpStore %i %33
OpBranch %34
%34 = OpLabel
OpLoopMerge %35 %36 None
OpBranch %37
%37 = OpLabel
%38 = OpLoad %int %i
%39 = OpAccessChain %_ptr_Input_float %in_val %uint_1
%40 = OpLoad %float %39
%41 = OpConvertFToS %int %40
%42 = OpSLessThan %bool %38 %41
OpBranchConditional %42 %43 %35
%43 = OpLabel
%44 = OpLoad %int %a
%45 = OpLoad %int %b
%46 = OpIAdd %int %44 %45
OpStore %hoist %46
%47 = OpLoad %int %i
%48 = OpConvertSToF %float %47
%49 = OpLoad %int %i
%50 = OpConvertSToF %float %49
%51 = OpLoad %int %i
%52 = OpConvertSToF %float %51
%53 = OpLoad %int %i
%54 = OpConvertSToF %float %53
%55 = OpCompositeConstruct %v4float %48 %50 %52 %54
OpStore %c %55
OpStore %hoist %int_0
%56 = OpLoad %int %hoist
%57 = OpConvertSToF %float %56
%58 = OpLoad %int %i
%59 = OpConvertSToF %float %58
%60 = OpAccessChain %_ptr_Input_float %in_val %uint_2
%61 = OpLoad %float %60
%62 = OpAccessChain %_ptr_Input_float %in_val %uint_3
%63 = OpLoad %float %62
%64 = OpCompositeConstruct %v4float %57 %59 %61 %63
OpStore %d %64
OpBranch %36
%36 = OpLabel
%65 = OpLoad %int %i
%66 = OpIAdd %int %65 %int_1
OpStore %i %66
OpBranch %34
%35 = OpLabel
OpReturn
OpFunctionEnd
)";

  const std::string after_hoist = R"(
)";

  SinglePassRunAndCheck<opt::LICMPass>(before_hoist, after_hoist, true);
}

}  // namespace
