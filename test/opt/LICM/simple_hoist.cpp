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
TEST_F(PassClassTest, SimpleHoist) {
  const std::string start = R"(
         OpCapability Shader
    %1 = OpExtInstImport "GLSL.std.450"
         OpMemoryModel Logical GLSL450
         OpEntryPoint Fragment %4 "main" %17 %22
         OpExecutionMode %4 OriginUpperLeft
         OpSource GLSL 440
         OpName %4 "main"
         OpName %8 "a"
         OpName %10 "b"
         OpName %12 "hoist"
         OpName %17 "c"
         OpName %20 "i"
         OpName %22 "in_val"
         OpDecorate %17 Location 0
         OpDecorate %22 Location 1
    %2 = OpTypeVoid
    %3 = OpTypeFunction %2
    %6 = OpTypeInt 32 1
    %7 = OpTypePointer Function %6
    %9 = OpConstant %6 1
   %11 = OpConstant %6 2
   %13 = OpConstant %6 0
   %14 = OpTypeFloat 32
   %15 = OpTypeVector %14 4
   %16 = OpTypePointer Output %15
   %17 = OpVariable %16 Output
   %18 = OpConstant %14 0
   %19 = OpConstantComposite %15 %18 %18 %18 %18
   %21 = OpTypePointer Input %15
   %22 = OpVariable %21 Input
   %23 = OpTypeInt 32 0
   %24 = OpConstant %23 0
   %25 = OpTypePointer Input %14
   %35 = OpConstant %23 1
   %39 = OpTypeBool
    %4 = OpFunction %2 None %3
    %5 = OpLabel
    %8 = OpVariable %7 Function
   %10 = OpVariable %7 Function
   %12 = OpVariable %7 Function
   %20 = OpVariable %7 Function
         OpStore %8 %9
         OpStore %10 %11
         OpStore %12 %13
         OpStore %17 %19
   %26 = OpAccessChain %25 %22 %24
   %27 = OpLoad %14 %26
   %28 = OpConvertFToS %6 %27
         OpStore %20 %28
         OpBranch %29
   %29 = OpLabel
         OpLoopMerge %31 %32 None
         OpBranch %33
   %33 = OpLabel
   %34 = OpLoad %6 %20
   %36 = OpAccessChain %25 %22 %35
   %37 = OpLoad %14 %36
   %38 = OpConvertFToS %6 %37
   %40 = OpSLessThan %39 %34 %38
         OpBranchConditional %40 %30 %31
   %30 = OpLabel
   %41 = OpLoad %6 %8
   %42 = OpLoad %6 %10
   %43 = OpIAdd %6 %41 %42
         OpStore %12 %43
   %44 = OpLoad %6 %20
   %45 = OpConvertSToF %14 %44
   %46 = OpLoad %6 %20
   %47 = OpConvertSToF %14 %46
   %48 = OpLoad %6 %20
   %49 = OpConvertSToF %14 %48
   %50 = OpLoad %6 %20
   %51 = OpConvertSToF %14 %50
   %52 = OpCompositeConstruct %15 %45 %47 %49 %51
         OpStore %17 %52
         OpBranch %32
   %32 = OpLabel
   %53 = OpLoad %6 %20
   %54 = OpIAdd %6 %53 %9
         OpStore %20 %54
         OpBranch %29
   %31 = OpLabel
         OpReturn
         OpFunctionEnd
)";


const std::string end = R"(
)";
  // clang-format on
 // std::unique_ptr<ir::IRContext> context =
 //     BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, start,
 //                 SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
 // ir::Module* module = context->module();
 // EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
 //                            << start << std::endl;
 // 
 // const ir::Function* f = spvtest::GetFunction(module, 4);
 // LoopDescriptor loopDescriptor{f};
 // //ir::CFG cfg(module);
 // if (loopDescriptor.NumLoops() > 0) {
 //   std::cout << "boop";
 // }
//
  SinglePassRunAndCheck<opt::LICMPass>(start, end, true);

}

}  // namespace
