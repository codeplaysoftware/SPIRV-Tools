// Copyright (c) 2018 Google LLC.
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

#ifdef SPIRV_EFFCEE
#include "effcee/effcee.h"
#endif

#include "../assembly_builder.h"
#include "../function_utils.h"
#include "../pass_fixture.h"

#include "opt/build_module.h"
#include "opt/loop_descriptor.h"
#include "opt/loop_utils.h"
#include "opt/pass.h"

namespace {

using namespace spvtools;

#ifdef SPIRV_EFFCEE

using UnswitchTest = PassTest<::testing::Test>;

/*
Generated from the following GLSL + --eliminate-local-multi-store

#version 330 core
in vec4 c;
void main() {
  int i = 0;
  int j = 0;
  bool cond = c[0] == 0;
  for (; i < 10; i++, j++) {
    if (cond) {
      i++;
    }
    else {
      j++;
    }
  }
}
*/
TEST_F(UnswitchTest, SimpleUnswitch) {
  const std::string text = R"(
; CHECK OpLoopMerge [[merge:%\w+]] %19 None
; CHECK [[merge]] = OpLabel
; CHECK-NEXT [[phi:%\w+]] = OpPhi {{%\w+}} %30 %20
; CHECK-NEXT %27 = OpINotEqual {{%\w+}} [[phi]] %9
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %main "main" %c
               OpExecutionMode %main OriginUpperLeft
               OpSource GLSL 330
               OpName %main "main"
               OpName %c "c"
               OpDecorate %c Location 0
       %void = OpTypeVoid
          %3 = OpTypeFunction %void
        %int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
      %int_0 = OpConstant %int 0
       %bool = OpTypeBool
%_ptr_Function_bool = OpTypePointer Function %bool
      %float = OpTypeFloat 32
    %v4float = OpTypeVector %float 4
%_ptr_Input_v4float = OpTypePointer Input %v4float
          %c = OpVariable %_ptr_Input_v4float Input
       %uint = OpTypeInt 32 0
     %uint_0 = OpConstant %uint 0
%_ptr_Input_float = OpTypePointer Input %float
    %float_0 = OpConstant %float 0
     %int_10 = OpConstant %int 10
      %int_1 = OpConstant %int 1
       %main = OpFunction %void None %3
          %5 = OpLabel
         %21 = OpAccessChain %_ptr_Input_float %c %uint_0
         %22 = OpLoad %float %21
         %24 = OpFOrdEqual %bool %22 %float_0
               OpBranch %25
         %25 = OpLabel
         %46 = OpPhi %int %int_0 %5 %43 %28
         %47 = OpPhi %int %int_0 %5 %45 %28
               OpLoopMerge %27 %28 None
               OpBranch %29
         %29 = OpLabel
         %32 = OpSLessThan %bool %46 %int_10
               OpBranchConditional %32 %26 %27
         %26 = OpLabel
               OpSelectionMerge %35 None
               OpBranchConditional %24 %34 %39
         %34 = OpLabel
         %38 = OpIAdd %int %46 %int_1
               OpBranch %35
         %39 = OpLabel
         %41 = OpIAdd %int %47 %int_1
               OpBranch %35
         %35 = OpLabel
         %48 = OpPhi %int %38 %34 %46 %39
         %49 = OpPhi %int %47 %34 %41 %39
               OpBranch %28
         %28 = OpLabel
         %43 = OpIAdd %int %48 %int_1
         %45 = OpIAdd %int %49 %int_1
               OpBranch %25
         %27 = OpLabel
               OpReturn
               OpFunctionEnd
  )";

  SinglePassRunAndMatch<opt::LoopUnswitchPass>(text, true);
}

#endif  // SPIRV_EFFCEE

/*
Generated from the following GLSL + --eliminate-local-multi-store

#version 330 core
in vec4 c;
void main() {
  int i = 0;
  bool cond = c[0] == 0;
  for (; i < 10; i++) {
    if (cond) {
      i++;
    }
    else {
      return;
    }
  }
}
*/
TEST_F(UnswitchTest, UnswitchExit) {
  const std::string text = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main" %16
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 330
               OpName %4 "main"
               OpName %16 "c"
               OpDecorate %16 Location 0
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %6 = OpTypeInt 32 1
          %7 = OpTypePointer Function %6
          %9 = OpConstant %6 0
         %10 = OpTypeBool
         %11 = OpTypePointer Function %10
         %13 = OpTypeFloat 32
         %14 = OpTypeVector %13 4
         %15 = OpTypePointer Input %14
         %16 = OpVariable %15 Input
         %17 = OpTypeInt 32 0
         %18 = OpConstant %17 0
         %19 = OpTypePointer Input %13
         %22 = OpConstant %13 0
         %30 = OpConstant %6 10
         %36 = OpConstant %6 1
          %4 = OpFunction %2 None %3
          %5 = OpLabel
         %20 = OpAccessChain %19 %16 %18
         %21 = OpLoad %13 %20
         %23 = OpFOrdEqual %10 %21 %22
               OpBranch %24
         %24 = OpLabel
         %42 = OpPhi %6 %9 %5 %41 %27
               OpLoopMerge %26 %27 None
               OpBranch %28
         %28 = OpLabel
         %31 = OpSLessThan %10 %42 %30
               OpBranchConditional %31 %25 %26
         %25 = OpLabel
               OpSelectionMerge %34 None
               OpBranchConditional %23 %33 %38
         %33 = OpLabel
         %37 = OpIAdd %6 %42 %36
               OpBranch %34
         %38 = OpLabel
               OpReturn
         %34 = OpLabel
               OpBranch %27
         %27 = OpLabel
         %41 = OpIAdd %6 %37 %36
               OpBranch %24
         %26 = OpLabel
               OpReturn
               OpFunctionEnd
  )";

  SinglePassRunAndMatch<opt::LoopUnswitchPass>(text, true);
}

/*
Generated from the following GLSL + --eliminate-local-multi-store

#version 330 core
in vec4 c;
void main() {
  int i = 0;
  bool cond = c[0] == 0;
  for (; i < 10; i++) {
    if (cond) {
      return;
    }
    else {
      i++;
    }
  }
}
*/
TEST_F(UnswitchTest, UnswitchExit2) {
  const std::string text = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main" %16
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 330
               OpName %4 "main"
               OpName %16 "c"
               OpDecorate %16 Location 0
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %6 = OpTypeInt 32 1
          %7 = OpTypePointer Function %6
          %9 = OpConstant %6 0
         %10 = OpTypeBool
         %11 = OpTypePointer Function %10
         %13 = OpTypeFloat 32
         %14 = OpTypeVector %13 4
         %15 = OpTypePointer Input %14
         %16 = OpVariable %15 Input
         %17 = OpTypeInt 32 0
         %18 = OpConstant %17 0
         %19 = OpTypePointer Input %13
         %22 = OpConstant %13 0
         %30 = OpConstant %6 10
         %38 = OpConstant %6 1
          %4 = OpFunction %2 None %3
          %5 = OpLabel
         %20 = OpAccessChain %19 %16 %18
         %21 = OpLoad %13 %20
         %23 = OpFOrdEqual %10 %21 %22
               OpBranch %24
         %24 = OpLabel
         %42 = OpPhi %6 %9 %5 %41 %27
               OpLoopMerge %26 %27 None
               OpBranch %28
         %28 = OpLabel
         %31 = OpSLessThan %10 %42 %30
               OpBranchConditional %31 %25 %26
         %25 = OpLabel
               OpSelectionMerge %34 None
               OpBranchConditional %23 %33 %36
         %33 = OpLabel
               OpReturn
         %36 = OpLabel
         %39 = OpIAdd %6 %42 %38
               OpBranch %34
         %34 = OpLabel
               OpBranch %27
         %27 = OpLabel
         %41 = OpIAdd %6 %39 %38
               OpBranch %24
         %26 = OpLabel
               OpReturn
               OpFunctionEnd
  )";

  SinglePassRunAndMatch<opt::LoopUnswitchPass>(text, true);
}

/*
Generated from the following GLSL + --eliminate-local-multi-store

#version 330 core
in vec4 c;
void main() {
  int i = 0;
  bool cond = c[0] == 0;
  for (; i < 10; i++) {
    if (cond) {
      i++;
    }
    else {
      break;
    }
  }
}
*/
TEST_F(UnswitchTest, UnswitchKillLoop) {
  const std::string text = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main" %16
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 330
               OpName %4 "main"
               OpName %16 "c"
               OpDecorate %16 Location 0
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %6 = OpTypeInt 32 1
          %7 = OpTypePointer Function %6
          %9 = OpConstant %6 0
         %10 = OpTypeBool
         %11 = OpTypePointer Function %10
         %13 = OpTypeFloat 32
         %14 = OpTypeVector %13 4
         %15 = OpTypePointer Input %14
         %16 = OpVariable %15 Input
         %17 = OpTypeInt 32 0
         %18 = OpConstant %17 0
         %19 = OpTypePointer Input %13
         %22 = OpConstant %13 0
         %30 = OpConstant %6 10
         %36 = OpConstant %6 1
          %4 = OpFunction %2 None %3
          %5 = OpLabel
         %20 = OpAccessChain %19 %16 %18
         %21 = OpLoad %13 %20
         %23 = OpFOrdEqual %10 %21 %22
               OpBranch %24
         %24 = OpLabel
         %42 = OpPhi %6 %9 %5 %41 %27
               OpLoopMerge %26 %27 None
               OpBranch %28
         %28 = OpLabel
         %31 = OpSLessThan %10 %42 %30
               OpBranchConditional %31 %25 %26
         %25 = OpLabel
               OpSelectionMerge %34 None
               OpBranchConditional %23 %33 %38
         %33 = OpLabel
         %37 = OpIAdd %6 %42 %36
               OpBranch %34
         %38 = OpLabel
               OpBranch %26
         %34 = OpLabel
               OpBranch %27
         %27 = OpLabel
         %41 = OpIAdd %6 %37 %36
               OpBranch %24
         %26 = OpLabel
               OpReturn
               OpFunctionEnd
  )";

  SinglePassRunAndMatch<opt::LoopUnswitchPass>(text, true);
}

/*
Generated from the following GLSL + --eliminate-local-multi-store

#version 330 core
in vec4 c;
void main() {
  int i = 0;
  int cond = int(c[0]);
  for (; i < 10; i++) {
    switch (cond) {
      case 0:
        return;
      case 1:
        discard;
      case 2:
        break;
      default:
        break;
    }
  }
  bool cond2 = i == 9;
}
*/
TEST_F(UnswitchTest, UnswitchSwitch) {
  const std::string text = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main" %14
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 330
               OpName %4 "main"
               OpName %14 "c"
               OpDecorate %14 Location 0
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %6 = OpTypeInt 32 1
          %7 = OpTypePointer Function %6
          %9 = OpConstant %6 0
         %11 = OpTypeFloat 32
         %12 = OpTypeVector %11 4
         %13 = OpTypePointer Input %12
         %14 = OpVariable %13 Input
         %15 = OpTypeInt 32 0
         %16 = OpConstant %15 0
         %17 = OpTypePointer Input %11
         %27 = OpConstant %6 10
         %28 = OpTypeBool
         %42 = OpConstant %6 1
         %44 = OpTypePointer Function %28
          %4 = OpFunction %2 None %3
          %5 = OpLabel
         %18 = OpAccessChain %17 %14 %16
         %19 = OpLoad %11 %18
         %20 = OpConvertFToS %6 %19
               OpBranch %21
         %21 = OpLabel
         %49 = OpPhi %6 %9 %5 %43 %24
               OpLoopMerge %23 %24 None
               OpBranch %25
         %25 = OpLabel
         %29 = OpSLessThan %28 %49 %27
               OpBranchConditional %29 %22 %23
         %22 = OpLabel
               OpSelectionMerge %35 None
               OpSwitch %20 %34 0 %31 1 %32 2 %33
         %34 = OpLabel
               OpBranch %35
         %31 = OpLabel
               OpReturn
         %32 = OpLabel
               OpKill
         %33 = OpLabel
               OpBranch %35
         %35 = OpLabel
               OpBranch %24
         %24 = OpLabel
         %43 = OpIAdd %6 %49 %42
               OpBranch %21
         %23 = OpLabel
               OpReturn
               OpFunctionEnd
  )";

  SinglePassRunAndMatch<opt::LoopUnswitchPass>(text, true);
}

/*
Generated from the following GLSL + --eliminate-local-multi-store

#version 440 core
layout(location = 0)in vec4 c;
void main() {
  int i = 0;
  int j = 0;
  int k = 0;
  bool cond = c[0] == 0;
  for (; i < 10; i++) {
    for (; j < 10; j++) {
      if (cond) {
        i++;
      } else {
        j++;
      }
    }
  }
  for (; k < 10; k++) {
    if (cond) {
      k++;
    }
  }
}
*/
TEST_F(UnswitchTest, UnSwitchNested) {
  const std::string text = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main" %18
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 440
               OpName %4 "main"
               OpName %18 "c"
               OpDecorate %18 Location 0
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %6 = OpTypeInt 32 1
          %7 = OpTypePointer Function %6
          %9 = OpConstant %6 0
         %12 = OpTypeBool
         %13 = OpTypePointer Function %12
         %15 = OpTypeFloat 32
         %16 = OpTypeVector %15 4
         %17 = OpTypePointer Input %16
         %18 = OpVariable %17 Input
         %19 = OpTypeInt 32 0
         %20 = OpConstant %19 0
         %21 = OpTypePointer Input %15
         %24 = OpConstant %15 0
         %32 = OpConstant %6 10
         %45 = OpConstant %6 1
          %4 = OpFunction %2 None %3
          %5 = OpLabel
         %22 = OpAccessChain %21 %18 %20
         %23 = OpLoad %15 %22
         %25 = OpFOrdEqual %12 %23 %24
               OpBranch %26
         %26 = OpLabel
         %68 = OpPhi %6 %9 %5 %53 %29
         %69 = OpPhi %6 %9 %5 %71 %29
               OpLoopMerge %28 %29 None
               OpBranch %30
         %30 = OpLabel
         %33 = OpSLessThan %12 %68 %32
               OpBranchConditional %33 %27 %28
         %27 = OpLabel
               OpBranch %34
         %34 = OpLabel
         %70 = OpPhi %6 %68 %27 %72 %37
         %71 = OpPhi %6 %69 %27 %51 %37
               OpLoopMerge %36 %37 None
               OpBranch %38
         %38 = OpLabel
         %40 = OpSLessThan %12 %71 %32
               OpBranchConditional %40 %35 %36
         %35 = OpLabel
               OpSelectionMerge %43 None
               OpBranchConditional %25 %42 %47
         %42 = OpLabel
         %46 = OpIAdd %6 %70 %45
               OpBranch %43
         %47 = OpLabel
         %49 = OpIAdd %6 %71 %45
               OpBranch %43
         %43 = OpLabel
         %72 = OpPhi %6 %46 %42 %70 %47
         %73 = OpPhi %6 %71 %42 %49 %47
               OpBranch %37
         %37 = OpLabel
         %51 = OpIAdd %6 %73 %45
               OpBranch %34
         %36 = OpLabel
               OpBranch %29
         %29 = OpLabel
         %53 = OpIAdd %6 %70 %45
               OpBranch %26
         %28 = OpLabel
               OpBranch %54
         %54 = OpLabel
         %74 = OpPhi %6 %9 %28 %67 %57
               OpLoopMerge %56 %57 None
               OpBranch %58
         %58 = OpLabel
         %60 = OpSLessThan %12 %74 %32
               OpBranchConditional %60 %55 %56
         %55 = OpLabel
               OpSelectionMerge %63 None
               OpBranchConditional %25 %62 %63
         %62 = OpLabel
         %65 = OpIAdd %6 %74 %45
               OpBranch %63
         %63 = OpLabel
         %75 = OpPhi %6 %74 %55 %65 %62
               OpBranch %57
         %57 = OpLabel
         %67 = OpIAdd %6 %75 %45
               OpBranch %54
         %56 = OpLabel
               OpReturn
               OpFunctionEnd
)";

  SinglePassRunAndMatch<opt::LoopUnswitchPass>(text, true);
}

}  // namespace
