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
  Generated from the following GLSL fragment shader
--eliminate-local-multi-store has also been run on the spv binary
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
OpName %c "c"
OpName %in_val "in_val"
OpName %d "d"
OpDecorate %c Location 0
OpDecorate %in_val Location 2
OpDecorate %d Location 1
%void = OpTypeVoid
%7 = OpTypeFunction %void
%int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
%int_1 = OpConstant %int 1
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
%d = OpVariable %_ptr_Output_v4float Output
%uint_2 = OpConstant %uint 2
%uint_3 = OpConstant %uint 3
%main = OpFunction %void None %7
%25 = OpLabel
OpStore %c %16
%26 = OpAccessChain %_ptr_Input_float %in_val %uint_0
%27 = OpLoad %float %26
%28 = OpConvertFToS %int %27
OpBranch %29
%29 = OpLabel
%30 = OpPhi %int %int_0 %25 %int_0 %31
%32 = OpPhi %int %28 %25 %33 %31
OpLoopMerge %34 %31 None
OpBranch %35
%35 = OpLabel
%36 = OpAccessChain %_ptr_Input_float %in_val %uint_1
%37 = OpLoad %float %36
%38 = OpConvertFToS %int %37
%39 = OpSLessThan %bool %32 %38
OpBranchConditional %39 %40 %34
%40 = OpLabel
%41 = OpConvertSToF %float %32
%42 = OpConvertSToF %float %32
%43 = OpConvertSToF %float %32
%44 = OpConvertSToF %float %32
%45 = OpCompositeConstruct %v4float %41 %42 %43 %44
OpStore %c %45
%46 = OpConvertSToF %float %int_0
%47 = OpConvertSToF %float %32
%48 = OpAccessChain %_ptr_Input_float %in_val %uint_2
%49 = OpLoad %float %48
%50 = OpAccessChain %_ptr_Input_float %in_val %uint_3
%51 = OpLoad %float %50
%52 = OpCompositeConstruct %v4float %46 %47 %49 %51
OpStore %d %52
OpBranch %31
%31 = OpLabel
%33 = OpIAdd %int %32 %int_1
OpBranch %29
%34 = OpLabel
OpReturn
OpFunctionEnd
)";

  const std::string after_hoist = R"(
)";

  SinglePassRunAndCheck<opt::LICMPass>(before_hoist, after_hoist, false);
}

}  // namespace
