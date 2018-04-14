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

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "../assembly_builder.h"
#include "../function_utils.h"
#include "../pass_fixture.h"
#include "../pass_utils.h"
#include "opt/loop_fission.h"
#include "opt/loop_unroller.h"
#include "opt/loop_utils.h"
#include "opt/pass.h"
namespace {

using namespace spvtools;
using ::testing::UnorderedElementsAre;

using FissionClassTest = PassTest<::testing::Test>;

/*
Generated from the following GLSL

#version 430

void main(void) {
    float A[10];
    float B[10];
    for (int i = 0; i < 10; i++) {
        A[i] = B[i];
        B[i] = A[i];
    }
}

Result should be equivalent to:

void main(void) {
    float A[10];
    float B[10];
    for (int i = 0; i < 10; i++) {
        A[i] = B[i];
    }

    for (int i = 0; i < 10; i++) {
        B[i] = A[i];
    }
}
*/
TEST_F(FissionClassTest, SimpleFission) {
  // clang-format off
  // With opt::LocalMultiStoreElimPass
  const std::string source = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main"
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 430
               OpName %2 "main"
               OpName %3 "i"
               OpName %4 "A"
               OpName %5 "B"
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
         %18 = OpTypePointer Function %13
         %19 = OpConstant %8 1
          %2 = OpFunction %6 None %7
         %20 = OpLabel
          %3 = OpVariable %9 Function
          %4 = OpVariable %17 Function
          %5 = OpVariable %17 Function
               OpBranch %21
         %21 = OpLabel
         %22 = OpPhi %8 %10 %20 %23 %24
               OpLoopMerge %25 %24 None
               OpBranch %26
         %26 = OpLabel
         %27 = OpSLessThan %12 %22 %11
               OpBranchConditional %27 %28 %25
         %28 = OpLabel
         %29 = OpAccessChain %18 %5 %22
         %30 = OpLoad %13 %29
         %31 = OpAccessChain %18 %4 %22
               OpStore %31 %30
         %32 = OpAccessChain %18 %4 %22
         %33 = OpLoad %13 %32
         %34 = OpAccessChain %18 %5 %22
               OpStore %34 %33
               OpBranch %24
         %24 = OpLabel
         %23 = OpIAdd %8 %22 %19
               OpBranch %21
         %25 = OpLabel
               OpReturn
               OpFunctionEnd
    )";

const std::string expected = R"(OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %2 "main"
OpExecutionMode %2 OriginUpperLeft
OpSource GLSL 430
OpName %2 "main"
OpName %3 "i"
OpName %4 "A"
OpName %5 "B"
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
%18 = OpTypePointer Function %13
%19 = OpConstant %8 1
%2 = OpFunction %6 None %7
%20 = OpLabel
%3 = OpVariable %9 Function
%4 = OpVariable %17 Function
%5 = OpVariable %17 Function
OpBranch %35
%35 = OpLabel
%36 = OpPhi %8 %10 %20 %47 %46
OpLoopMerge %48 %46 None
OpBranch %37
%37 = OpLabel
%38 = OpSLessThan %12 %36 %11
OpBranchConditional %38 %39 %48
%39 = OpLabel
%40 = OpAccessChain %18 %5 %36
%41 = OpLoad %13 %40
%42 = OpAccessChain %18 %4 %36
OpStore %42 %41
OpBranch %46
%46 = OpLabel
%47 = OpIAdd %8 %36 %19
OpBranch %35
%48 = OpLabel
OpBranch %21
%21 = OpLabel
%22 = OpPhi %8 %10 %48 %23 %24
OpLoopMerge %25 %24 None
OpBranch %26
%26 = OpLabel
%27 = OpSLessThan %12 %22 %11
OpBranchConditional %27 %28 %25
%28 = OpLabel
%32 = OpAccessChain %18 %4 %22
%33 = OpLoad %13 %32
%34 = OpAccessChain %18 %5 %22
OpStore %34 %33
OpBranch %24
%24 = OpLabel
%23 = OpIAdd %8 %22 %19
OpBranch %21
%25 = OpLabel
OpReturn
OpFunctionEnd
)";
  // clang-format on
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, source,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << source << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 2);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  EXPECT_EQ(ld.NumLoops(), 1u);

  SetDisassembleOptions(SPV_BINARY_TO_TEXT_OPTION_NO_HEADER);
  SinglePassRunAndCheck<opt::LoopFissionPass>(source, expected, true);
}

/*
Generated from the following GLSL

#version 430

void main(void) {
    float A[10];
    float B[10];
    for (int i = 0; i < 10; i++) {
        A[i] = B[i];
        B[i] = A[i+1];
    }
}

This loop should not be split, as the i+1 dependence would be broken by
splitting the loop.
*/

TEST_F(FissionClassTest, FissionInterdependency) {
  // clang-format off
  // With opt::LocalMultiStoreElimPass
  const std::string source = R"(OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %2 "main"
OpExecutionMode %2 OriginUpperLeft
OpSource GLSL 430
OpName %2 "main"
OpName %3 "i"
OpName %4 "A"
OpName %5 "B"
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
%18 = OpTypePointer Function %13
%19 = OpConstant %8 1
%2 = OpFunction %6 None %7
%20 = OpLabel
%3 = OpVariable %9 Function
%4 = OpVariable %17 Function
%5 = OpVariable %17 Function
OpBranch %21
%21 = OpLabel
%22 = OpPhi %8 %10 %20 %23 %24
OpLoopMerge %25 %24 None
OpBranch %26
%26 = OpLabel
%27 = OpSLessThan %12 %22 %11
OpBranchConditional %27 %28 %25
%28 = OpLabel
%29 = OpAccessChain %18 %5 %22
%30 = OpLoad %13 %29
%31 = OpAccessChain %18 %4 %22
OpStore %31 %30
%32 = OpIAdd %8 %22 %19
%33 = OpAccessChain %18 %4 %32
%34 = OpLoad %13 %33
%35 = OpAccessChain %18 %5 %22
OpStore %35 %34
OpBranch %24
%24 = OpLabel
%23 = OpIAdd %8 %22 %19
OpBranch %21
%25 = OpLabel
OpReturn
OpFunctionEnd
)";
  // clang-format on
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, source,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for ushader:\n"
                             << source << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 2);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  EXPECT_EQ(ld.NumLoops(), 1u);

  SetDisassembleOptions(SPV_BINARY_TO_TEXT_OPTION_NO_HEADER);
  SinglePassRunAndCheck<opt::LoopFissionPass>(source, source, true);
}

/*
Generated from the following GLSL

#version 430

void main(void) {
    float A[10];
    float B[10];
    for (int i = 0; i < 10; i++) {
        A[i] = B[i];
        B[i+1] = A[i];
    }
}


This should be split as the load B[i] is dependent on the store B[i+1]
*/
TEST_F(FissionClassTest, FissionInterdependency2) {
  // clang-format off
  // With opt::LocalMultiStoreElimPass
const std::string source = R"(OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %2 "main"
OpExecutionMode %2 OriginUpperLeft
OpSource GLSL 430
OpName %2 "main"
OpName %3 "i"
OpName %4 "A"
OpName %5 "B"
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
%18 = OpTypePointer Function %13
%19 = OpConstant %8 1
%2 = OpFunction %6 None %7
%20 = OpLabel
%3 = OpVariable %9 Function
%4 = OpVariable %17 Function
%5 = OpVariable %17 Function
OpBranch %21
%21 = OpLabel
%22 = OpPhi %8 %10 %20 %23 %24
OpLoopMerge %25 %24 None
OpBranch %26
%26 = OpLabel
%27 = OpSLessThan %12 %22 %11
OpBranchConditional %27 %28 %25
%28 = OpLabel
%29 = OpAccessChain %18 %5 %22
%30 = OpLoad %13 %29
%31 = OpAccessChain %18 %4 %22
OpStore %31 %30
%32 = OpIAdd %8 %22 %19
%33 = OpAccessChain %18 %4 %22
%34 = OpLoad %13 %33
%35 = OpAccessChain %18 %5 %32
OpStore %35 %34
OpBranch %24
%24 = OpLabel
%23 = OpIAdd %8 %22 %19
OpBranch %21
%25 = OpLabel
OpReturn
OpFunctionEnd
)";
  // clang-format on
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, source,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for ushader:\n"
                             << source << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 2);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  EXPECT_EQ(ld.NumLoops(), 1u);

  SetDisassembleOptions(SPV_BINARY_TO_TEXT_OPTION_NO_HEADER);

  SetDisassembleOptions(SPV_BINARY_TO_TEXT_OPTION_NO_HEADER);
  SinglePassRunAndCheck<opt::LoopFissionPass>(source, source, true);
}

TEST_F(FissionClassTest, SimpleDataDependency4) {
  // clang-format off
  // With opt::LocalMultiStoreElimPass
  const std::string text = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main"
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 430
               OpName %2 "main"
               OpName %3 "i"
               OpName %4 "A"
               OpName %5 "B"
               OpName %6 "C"
               OpName %7 "D"
          %8 = OpTypeVoid
          %9 = OpTypeFunction %8
         %10 = OpTypeInt 32 1
         %11 = OpTypePointer Function %10
         %12 = OpConstant %10 0
         %13 = OpConstant %10 10
         %14 = OpTypeBool
         %15 = OpTypeFloat 32
         %16 = OpTypeInt 32 0
         %17 = OpConstant %16 10
         %18 = OpTypeArray %15 %17
         %19 = OpTypePointer Function %18
         %20 = OpTypePointer Function %15
         %21 = OpConstant %10 1
          %2 = OpFunction %8 None %9
         %22 = OpLabel
          %3 = OpVariable %11 Function
          %4 = OpVariable %19 Function
          %5 = OpVariable %19 Function
          %6 = OpVariable %19 Function
          %7 = OpVariable %19 Function
               OpBranch %23
         %23 = OpLabel
         %24 = OpPhi %10 %12 %22 %25 %26
               OpLoopMerge %27 %26 None
               OpBranch %28
         %28 = OpLabel
         %29 = OpSLessThan %14 %24 %13
               OpBranchConditional %29 %30 %27
         %30 = OpLabel
         %31 = OpAccessChain %20 %5 %24
         %32 = OpLoad %15 %31
         %33 = OpAccessChain %20 %4 %24
               OpStore %33 %32
         %34 = OpAccessChain %20 %4 %24
         %35 = OpLoad %15 %34
         %36 = OpAccessChain %20 %5 %24
               OpStore %36 %35
         %37 = OpAccessChain %20 %7 %24
         %38 = OpLoad %15 %37
         %39 = OpAccessChain %20 %6 %24
               OpStore %39 %38
         %40 = OpAccessChain %20 %6 %24
         %41 = OpLoad %15 %40
         %42 = OpAccessChain %20 %7 %24
               OpStore %42 %41
               OpBranch %26
         %26 = OpLabel
         %25 = OpIAdd %10 %24 %21
               OpBranch %23
         %27 = OpLabel
               OpReturn
               OpFunctionEnd
  )";
  // clang-format on
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for ushader:\n"
                             << text << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 2);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  EXPECT_EQ(ld.NumLoops(), 1u);

  SetDisassembleOptions(SPV_BINARY_TO_TEXT_OPTION_NO_HEADER);

  std::cout << std::get<0>(
      SinglePassRunAndDisassemble<opt::LoopFissionPass>(text, false, true));
}

TEST_F(FissionClassTest, FissionWithAccumulator) {
  // clang-format off
  // With opt::LocalMultiStoreElimPass
  const std::string text = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main"
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 430
               OpName %2 "main"
               OpName %3 "accumulator"
               OpName %4 "i"
               OpName %5 "A"
               OpName %6 "B"
          %7 = OpTypeVoid
          %8 = OpTypeFunction %7
          %9 = OpTypeInt 32 1
         %10 = OpTypePointer Function %9
         %11 = OpConstant %9 0
         %12 = OpConstant %9 10
         %13 = OpTypeBool
         %14 = OpTypeFloat 32
         %15 = OpTypeInt 32 0
         %16 = OpConstant %15 10
         %17 = OpTypeArray %14 %16
         %18 = OpTypePointer Function %17
         %19 = OpTypePointer Function %14
         %20 = OpConstant %9 1
          %2 = OpFunction %7 None %8
         %21 = OpLabel
          %3 = OpVariable %10 Function
          %4 = OpVariable %10 Function
          %5 = OpVariable %18 Function
          %6 = OpVariable %18 Function
               OpStore %3 %11
               OpBranch %22
         %22 = OpLabel
         %23 = OpPhi %9 %11 %21 %24 %25
         %26 = OpPhi %9 %11 %21 %27 %25
               OpLoopMerge %28 %25 None
               OpBranch %29
         %29 = OpLabel
         %30 = OpSLessThan %13 %26 %12
               OpBranchConditional %30 %31 %28
         %31 = OpLabel
         %32 = OpAccessChain %19 %6 %26
         %33 = OpLoad %14 %32
         %34 = OpAccessChain %19 %5 %26
               OpStore %34 %33
         %35 = OpAccessChain %19 %5 %26
         %36 = OpLoad %14 %35
         %37 = OpAccessChain %19 %6 %26
               OpStore %37 %36
         %24 = OpIAdd %9 %23 %20
               OpStore %3 %24
               OpBranch %25
         %25 = OpLabel
         %27 = OpIAdd %9 %26 %20
               OpBranch %22
         %28 = OpLabel
               OpReturn
               OpFunctionEnd
  )";
  // clang-format on
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for ushader:\n"
                             << text << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 2);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  EXPECT_EQ(ld.NumLoops(), 1u);

  SetDisassembleOptions(SPV_BINARY_TO_TEXT_OPTION_NO_HEADER);

  std::cout << std::get<0>(
      SinglePassRunAndDisassemble<opt::LoopFissionPass>(text, false, true));
}

TEST_F(FissionClassTest, FissionWithPhisUsedOutwithLoop) {
  // clang-format off
  // With opt::LocalMultiStoreElimPass
  const std::string text = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main" %3 %4
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 430
               OpName %2 "main"
               OpName %5 "accumulator"
               OpName %6 "accumulator_2"
               OpName %7 "i"
               OpName %3 "x"
               OpName %4 "y"
               OpDecorate %3 Location 1
               OpDecorate %4 Location 2
          %8 = OpTypeVoid
          %9 = OpTypeFunction %8
         %10 = OpTypeInt 32 1
         %11 = OpTypePointer Function %10
         %12 = OpConstant %10 0
         %13 = OpConstant %10 10
         %14 = OpTypeBool
         %15 = OpConstant %10 1
         %16 = OpTypePointer Output %10
          %3 = OpVariable %16 Output
          %4 = OpVariable %16 Output
          %2 = OpFunction %8 None %9
         %17 = OpLabel
          %5 = OpVariable %11 Function
          %6 = OpVariable %11 Function
          %7 = OpVariable %11 Function
               OpBranch %18
         %18 = OpLabel
         %19 = OpPhi %10 %12 %17 %20 %21
         %22 = OpPhi %10 %12 %17 %23 %21
         %24 = OpPhi %10 %12 %17 %25 %21
               OpLoopMerge %26 %21 None
               OpBranch %27
         %27 = OpLabel
         %28 = OpSLessThan %14 %24 %13
               OpBranchConditional %28 %29 %26
         %29 = OpLabel
         %23 = OpIAdd %10 %22 %15
         %20 = OpIAdd %10 %19 %15
               OpBranch %21
         %21 = OpLabel
         %25 = OpIAdd %10 %24 %15
               OpBranch %18
         %26 = OpLabel
               OpStore %3 %22
               OpStore %4 %19
               OpReturn
               OpFunctionEnd
  )";
  // clang-format on
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for ushader:\n"
                             << text << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 2);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  EXPECT_EQ(ld.NumLoops(), 1u);

  SetDisassembleOptions(SPV_BINARY_TO_TEXT_OPTION_NO_HEADER);

  std::cout << std::get<0>(
      SinglePassRunAndDisassemble<opt::LoopFissionPass>(text, false, true));
}

/*
#version 430
void main(void) {
    float A[10][10];
    float B[10][10];
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            A[i][j] = B[i][j];
            B[i][j] = A[i][j];
        }
    }
}
*/
TEST_F(FissionClassTest, FissionNested) {
  // clang-format off
  // With opt::LocalMultiStoreElimPass
  const std::string text = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main"
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 430
               OpName %2 "main"
               OpName %3 "i"
               OpName %4 "j"
               OpName %5 "A"
               OpName %6 "B"
          %7 = OpTypeVoid
          %8 = OpTypeFunction %7
          %9 = OpTypeInt 32 1
         %10 = OpTypePointer Function %9
         %11 = OpConstant %9 0
         %12 = OpConstant %9 10
         %13 = OpTypeBool
         %14 = OpTypeFloat 32
         %15 = OpTypeInt 32 0
         %16 = OpConstant %15 10
         %17 = OpTypeArray %14 %16
         %18 = OpTypeArray %17 %16
         %19 = OpTypePointer Function %18
         %20 = OpTypePointer Function %14
         %21 = OpConstant %9 1
          %2 = OpFunction %7 None %8
         %22 = OpLabel
          %3 = OpVariable %10 Function
          %4 = OpVariable %10 Function
          %5 = OpVariable %19 Function
          %6 = OpVariable %19 Function
               OpStore %3 %11
               OpBranch %23
         %23 = OpLabel
         %24 = OpPhi %9 %11 %22 %25 %26
               OpLoopMerge %27 %26 None
               OpBranch %28
         %28 = OpLabel
         %29 = OpSLessThan %13 %24 %12
               OpBranchConditional %29 %30 %27
         %30 = OpLabel
               OpStore %4 %11
               OpBranch %31
         %31 = OpLabel
         %32 = OpPhi %9 %11 %30 %33 %34
               OpLoopMerge %35 %34 None
               OpBranch %36
         %36 = OpLabel
         %37 = OpSLessThan %13 %32 %12
               OpBranchConditional %37 %38 %35
         %38 = OpLabel
         %39 = OpAccessChain %20 %6 %24 %32
         %40 = OpLoad %14 %39
         %41 = OpAccessChain %20 %5 %24 %32
               OpStore %41 %40
         %42 = OpAccessChain %20 %5 %24 %32
         %43 = OpLoad %14 %42
         %44 = OpAccessChain %20 %6 %24 %32
               OpStore %44 %43
               OpBranch %34
         %34 = OpLabel
         %33 = OpIAdd %9 %32 %21
               OpStore %4 %33
               OpBranch %31
         %35 = OpLabel
               OpBranch %26
         %26 = OpLabel
         %25 = OpIAdd %9 %24 %21
               OpStore %3 %25
               OpBranch %23
         %27 = OpLabel
               OpReturn
               OpFunctionEnd
  )";
  // clang-format on
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for ushader:\n"
                             << text << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 2);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  EXPECT_EQ(ld.NumLoops(), 2u);

  SetDisassembleOptions(SPV_BINARY_TO_TEXT_OPTION_NO_HEADER);

  std::cout << std::get<0>(
      SinglePassRunAndDisassemble<opt::LoopFissionPass>(text, false, true));
}

TEST_F(FissionClassTest, FissionLoad) {
  // clang-format off
  // With opt::LocalMultiStoreElimPass
  const std::string text = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main"
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 430
               OpName %2 "main"
               OpName %3 "i"
               OpName %4 "c"
               OpName %5 "C"
               OpName %6 "A"
               OpName %7 "B"
          %8 = OpTypeVoid
          %9 = OpTypeFunction %8
         %10 = OpTypeInt 32 1
         %11 = OpTypePointer Function %10
         %12 = OpConstant %10 0
         %13 = OpConstant %10 10
         %14 = OpTypeBool
         %15 = OpTypeFloat 32
         %16 = OpTypePointer Function %15
         %17 = OpTypeInt 32 0
         %18 = OpConstant %17 1
         %19 = OpTypeArray %15 %18
         %20 = OpTypePointer Function %19
         %21 = OpConstant %17 10
         %22 = OpTypeArray %15 %21
         %23 = OpTypePointer Function %22
         %24 = OpConstant %10 1
          %2 = OpFunction %8 None %9
         %25 = OpLabel
          %3 = OpVariable %11 Function
          %4 = OpVariable %16 Function
          %5 = OpVariable %20 Function
          %6 = OpVariable %23 Function
          %7 = OpVariable %23 Function
               OpBranch %26
         %26 = OpLabel
         %27 = OpPhi %10 %12 %25 %28 %29
               OpLoopMerge %30 %29 None
               OpBranch %31
         %31 = OpLabel
         %32 = OpSLessThan %14 %27 %13
               OpBranchConditional %32 %33 %30
         %33 = OpLabel
         %34 = OpAccessChain %16 %5 %12
         %35 = OpLoad %15 %34
               OpStore %4 %35
         %36 = OpAccessChain %16 %7 %27
         %37 = OpLoad %15 %36
         %38 = OpAccessChain %16 %6 %27
               OpStore %38 %37
         %39 = OpAccessChain %16 %6 %27
         %40 = OpLoad %15 %39
         %41 = OpFAdd %15 %40 %35
         %42 = OpAccessChain %16 %7 %27
               OpStore %42 %41
               OpBranch %29
         %29 = OpLabel
         %28 = OpIAdd %10 %27 %24
               OpBranch %26
         %30 = OpLabel
               OpReturn
               OpFunctionEnd
)";
  // clang-format on
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for ushader:\n"
                             << text << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 2);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  EXPECT_EQ(ld.NumLoops(), 1u);

  SetDisassembleOptions(SPV_BINARY_TO_TEXT_OPTION_NO_HEADER);

  std::cout << std::get<0>(
      SinglePassRunAndDisassemble<opt::LoopFissionPass>(text, false, true));
}

}  // namespace
