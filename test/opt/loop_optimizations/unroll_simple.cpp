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

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "../assembly_builder.h"
#include "../function_utils.h"
#include "../pass_fixture.h"
#include "../pass_utils.h"
#include "opt/loop_unroller.h"
#include "opt/pass.h"

namespace {

using namespace spvtools;
using ::testing::UnorderedElementsAre;

using PassClassTest = PassTest<::testing::Test>;

/*
Generated from the following GLSL
#version 330 core
layout(location = 0) out vec4 c;
void main() {
  float x[10];
  for (int i = 0; i < 10; ++i) {
    x[i] = 1.0f;
  }
}
*/
TEST_F(PassClassTest, BasicVisitFromEntryPoint) {
  // clang-format off
  // With opt::LocalMultiStoreElimPass
  const std::string text = R"(
            OpCapability Shader
            %1 = OpExtInstImport "GLSL.std.450"
            OpMemoryModel Logical GLSL450
            OpEntryPoint Fragment %2 "main" %3
            OpExecutionMode %2 OriginUpperLeft
            OpSource GLSL 330
            OpName %2 "main"
            OpName %5 "x"
            OpName %3 "c"
            OpDecorate %3 Location 0
            %6 = OpTypeVoid
            %7 = OpTypeFunction %6
            %8 = OpTypeInt 32 1
            %9 = OpTypePointer Function %8
            %10 = OpConstant %8 0
            %11 = OpConstant %8 10
            %12 = OpTypeBool
            %13 = OpTypeFloat 32
            %14 = OpTypeInt 32 0
            %15 = OpConstant %14 10
            %16 = OpTypeArray %13 %15
            %17 = OpTypePointer Function %16
            %18 = OpConstant %13 1
            %19 = OpTypePointer Function %13
            %20 = OpConstant %8 1
            %21 = OpTypeVector %13 4
            %22 = OpTypePointer Output %21
            %3 = OpVariable %22 Output
            %2 = OpFunction %6 None %7
            %23 = OpLabel
            %5 = OpVariable %17 Function
            OpBranch %24
            %24 = OpLabel
            %35 = OpPhi %8 %10 %23 %34 %26
            OpLoopMerge %25 %26 Unroll
            OpBranch %27
            %27 = OpLabel
            %29 = OpSLessThan %12 %35 %11
            OpBranchConditional %29 %30 %25
            %30 = OpLabel
            %32 = OpAccessChain %19 %5 %35
            OpStore %32 %18
            OpBranch %26
            %26 = OpLabel
            %34 = OpIAdd %8 %35 %20
            OpBranch %24
            %25 = OpLabel
            OpReturn
            OpFunctionEnd
  )";

  const std::string output = 
R"(OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %2 "main" %3
OpExecutionMode %2 OriginUpperLeft
OpSource GLSL 330
OpName %2 "main"
OpName %4 "x"
OpName %3 "c"
OpDecorate %3 Location 0
%5 = OpTypeVoid
%6 = OpTypeFunction %5
%7 = OpTypeInt 32 1
%8 = OpTypePointer Function %7
%9 = OpConstant %7 0
%10 = OpConstant %7 10
%11 = OpTypeBool
%12 = OpTypeFloat 32
%13 = OpTypeInt 32 0
%14 = OpConstant %13 10
%15 = OpTypeArray %12 %14
%16 = OpTypePointer Function %15
%17 = OpConstant %12 1
%18 = OpTypePointer Function %12
%19 = OpConstant %7 1
%20 = OpTypeVector %12 4
%21 = OpTypePointer Output %20
%3 = OpVariable %21 Output
%2 = OpFunction %5 None %6
%22 = OpLabel
%4 = OpVariable %16 Function
OpBranch %23
%23 = OpLabel
%24 = OpPhi %7 %9 %22 %103 %102
OpBranch %28
%28 = OpLabel
%29 = OpSLessThan %11 %24 %10
OpBranch %30
%30 = OpLabel
%31 = OpAccessChain %18 %4 %24
OpStore %31 %17
OpBranch %26
%26 = OpLabel
%25 = OpIAdd %7 %24 %19
OpBranch %32
%32 = OpLabel
%33 = OpPhi %7 %9 %22 %39 %38
OpBranch %34
%34 = OpLabel
%35 = OpSLessThan %11 %25 %10
OpBranch %36
%36 = OpLabel
%37 = OpAccessChain %18 %4 %25
OpStore %37 %17
OpBranch %38
%38 = OpLabel
%39 = OpIAdd %7 %25 %19
OpBranch %40
%40 = OpLabel
%41 = OpPhi %7 %9 %22 %47 %46
OpBranch %42
%42 = OpLabel
%43 = OpSLessThan %11 %39 %10
OpBranch %44
%44 = OpLabel
%45 = OpAccessChain %18 %4 %39
OpStore %45 %17
OpBranch %46
%46 = OpLabel
%47 = OpIAdd %7 %39 %19
OpBranch %48
%48 = OpLabel
%49 = OpPhi %7 %9 %22 %55 %54
OpBranch %50
%50 = OpLabel
%51 = OpSLessThan %11 %47 %10
OpBranch %52
%52 = OpLabel
%53 = OpAccessChain %18 %4 %47
OpStore %53 %17
OpBranch %54
%54 = OpLabel
%55 = OpIAdd %7 %47 %19
OpBranch %56
%56 = OpLabel
%57 = OpPhi %7 %9 %22 %63 %62
OpBranch %58
%58 = OpLabel
%59 = OpSLessThan %11 %55 %10
OpBranch %60
%60 = OpLabel
%61 = OpAccessChain %18 %4 %55
OpStore %61 %17
OpBranch %62
%62 = OpLabel
%63 = OpIAdd %7 %55 %19
OpBranch %64
%64 = OpLabel
%65 = OpPhi %7 %9 %22 %71 %70
OpBranch %66
%66 = OpLabel
%67 = OpSLessThan %11 %63 %10
OpBranch %68
%68 = OpLabel
%69 = OpAccessChain %18 %4 %63
OpStore %69 %17
OpBranch %70
%70 = OpLabel
%71 = OpIAdd %7 %63 %19
OpBranch %72
%72 = OpLabel
%73 = OpPhi %7 %9 %22 %79 %78
OpBranch %74
%74 = OpLabel
%75 = OpSLessThan %11 %71 %10
OpBranch %76
%76 = OpLabel
%77 = OpAccessChain %18 %4 %71
OpStore %77 %17
OpBranch %78
%78 = OpLabel
%79 = OpIAdd %7 %71 %19
OpBranch %80
%80 = OpLabel
%81 = OpPhi %7 %9 %22 %87 %86
OpBranch %82
%82 = OpLabel
%83 = OpSLessThan %11 %79 %10
OpBranch %84
%84 = OpLabel
%85 = OpAccessChain %18 %4 %79
OpStore %85 %17
OpBranch %86
%86 = OpLabel
%87 = OpIAdd %7 %79 %19
OpBranch %88
%88 = OpLabel
%89 = OpPhi %7 %9 %22 %95 %94
OpBranch %90
%90 = OpLabel
%91 = OpSLessThan %11 %87 %10
OpBranch %92
%92 = OpLabel
%93 = OpAccessChain %18 %4 %87
OpStore %93 %17
OpBranch %94
%94 = OpLabel
%95 = OpIAdd %7 %87 %19
OpBranch %96
%96 = OpLabel
%97 = OpPhi %7 %9 %22 %103 %102
OpBranch %98
%98 = OpLabel
%99 = OpSLessThan %11 %95 %10
OpBranch %100
%100 = OpLabel
%101 = OpAccessChain %18 %4 %95
OpStore %101 %17
OpBranch %102
%102 = OpLabel
%103 = OpIAdd %7 %95 %19
OpBranch %27
%27 = OpLabel
OpReturn
OpFunctionEnd
)";
  // clang-format on
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << text << std::endl;

  opt::LoopUnroller loop_unroller;
  SetDisassembleOptions(SPV_BINARY_TO_TEXT_OPTION_NO_HEADER);
  SinglePassRunAndCheck<opt::LoopUnroller>(text, output, false);
}

}  // namespace
