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

#ifdef SPIRV_EFFCEE
#include "effcee/effcee.h"
#endif

#include "../pass_fixture.h"
#include "opt/ir_builder.h"
#include "opt/loop_descriptor.h"
#include "opt/loop_peeling.h"

namespace {

using namespace spvtools;

using PeelingTest = PassTest<::testing::Test>;

/*
Generated from the following GLSL + --eliminate-local-multi-store

#version 330 core
void main() {
  int a = 0;
  for(int i = 0; i < 10; ++i) {
    if (i < 3) {
      a += 2;
    }
  }
}
*/
TEST_F(PeelingTest, SimplePeeling) {
  const std::string text = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %main "main"
               OpExecutionMode %main OriginLowerLeft
               OpSource GLSL 330
               OpName %main "main"
               OpName %a "a"
               OpName %i "i"
       %void = OpTypeVoid
          %3 = OpTypeFunction %void
        %int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
      %int_0 = OpConstant %int 0
     %int_10 = OpConstant %int 10
       %bool = OpTypeBool
      %int_3 = OpConstant %int 3
      %int_2 = OpConstant %int 2
      %int_1 = OpConstant %int 1
       %main = OpFunction %void None %3
          %5 = OpLabel
          %a = OpVariable %_ptr_Function_int Function
          %i = OpVariable %_ptr_Function_int Function
               OpStore %a %int_0
               OpStore %i %int_0
               OpBranch %11
         %11 = OpLabel
         %31 = OpPhi %int %int_0 %5 %33 %14
         %32 = OpPhi %int %int_0 %5 %30 %14
               OpLoopMerge %13 %14 None
               OpBranch %15
         %15 = OpLabel
         %19 = OpSLessThan %bool %32 %int_10
               OpBranchConditional %19 %12 %13
         %12 = OpLabel
         %22 = OpSLessThan %bool %32 %int_3
               OpSelectionMerge %24 None
               OpBranchConditional %22 %23 %24
         %23 = OpLabel
         %27 = OpIAdd %int %31 %int_2
               OpStore %a %27
               OpBranch %24
         %24 = OpLabel
         %33 = OpPhi %int %31 %12 %27 %23
               OpBranch %14
         %14 = OpLabel
         %30 = OpIAdd %int %32 %int_1
               OpStore %i %30
               OpBranch %11
         %13 = OpLabel
               OpReturn
               OpFunctionEnd
  )";

  // Peel before.
  {
    SCOPED_TRACE("Peel before");
    const std::string check = R"(
; CHECK: [[CST_TEN:%\w+]] = OpConstant {{%\w+}} 10
; CHECK: [[CST_TWO:%\w+]] = OpConstant {{%\w+}} 2
; CHECK:      OpFunction
; CHECK-NEXT: [[ENTRY:%\w+]] = OpLabel
; CHECK: [[MIN_LOOP_COUNT:%\w+]] = OpSLessThan {{%\w+}} [[CST_TWO]] [[CST_TEN]]
; CHECK-NEXT: [[LOOP_COUNT:%\w+]] = OpSelect {{%\w+}} [[MIN_LOOP_COUNT]] [[CST_TWO]] [[CST_TEN]]
; CHECK:      [[BEFORE_LOOP:%\w+]] = OpLabel
; CHECK-NEXT: [[DUMMY_IT:%\w+]] = OpPhi {{%\w+}} {{%\w+}} [[ENTRY]] [[DUMMY_IT_1:%\w+]] [[BE:%\w+]]
; CHECK-NEXT: [[i:%\w+]] = OpPhi {{%\w+}} {{%\w+}} [[ENTRY]] [[I_1:%\w+]] [[BE]]
; CHECK-NEXT: OpLoopMerge [[AFTER_LOOP_PREHEADER:%\w+]] [[BE]] None
; CHECK:      [[COND_BLOCK:%\w+]] = OpLabel
; CHECK-NEXT: OpSLessThan
; CHECK-NEXT: [[EXIT_COND:%\w+]] = OpSLessThan {{%\w+}} [[DUMMY_IT]]
; CHECK-NEXT: OpBranchConditional [[EXIT_COND]] {{%\w+}} [[AFTER_LOOP_PREHEADER]]
; CHECK:      [[I_1]] = OpIAdd {{%\w+}} [[i]]
; CHECK-NEXT: [[DUMMY_IT_1]] = OpIAdd {{%\w+}} [[DUMMY_IT]]
; CHECK-NEXT: OpBranch [[BEFORE_LOOP]]
; 
; CHECK: [[AFTER_LOOP_PREHEADER]] = OpLabel
; CHECK-NEXT: OpSelectionMerge [[IF_MERGE:%\w+]]
; CHECK-NEXT: OpBranchConditional [[MIN_LOOP_COUNT]] [[AFTER_LOOP:%\w+]] [[IF_MERGE]]
; 
; CHECK:      [[AFTER_LOOP]] = OpLabel
; CHECK-NEXT: OpPhi {{%\w+}} {{%\w+}} {{%\w+}} [[i]] [[AFTER_LOOP_PREHEADER]]
; CHECK-NEXT: OpLoopMerge
)";
    SCOPED_TRACE("Run pass");
    SinglePassRunAndMatch<opt::LoopPeelingPass>(check + text, true);
  }
}

}  // namespace
