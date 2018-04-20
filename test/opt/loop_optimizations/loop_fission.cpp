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

  // Check that the loop will NOT be split when provided with a pass-through
  // register pressure functor which just returns false.
  SinglePassRunAndCheck<opt::LoopFissionPass>(
      source, source, true,
      [](const opt::RegisterLiveness::RegionRegisterLiveness&) {
        return false;
      });
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


This should not be split as the load B[i] is dependent on the store B[i+1]
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
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << source << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 2);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  EXPECT_EQ(ld.NumLoops(), 1u);

  SetDisassembleOptions(SPV_BINARY_TO_TEXT_OPTION_NO_HEADER);
  SinglePassRunAndCheck<opt::LoopFissionPass>(source, source, true);
}

/*
#version 430
void main(void) {
    float A[10];
    float B[10];
    float C[10]
    float D[10]
    for (int i = 0; i < 10; i++) {
        A[i] = B[i];
        B[i] = A[i];
        C[i] = D[i];
        D[i] = C[i];
    }
}

This should be split into the equivalent of:

    for (int i = 0; i < 10; i++) {
        A[i] = B[i];
        B[i] = A[i];
    }
    for (int i = 0; i < 10; i++) {
        C[i] = D[i];
        D[i] = C[i];
    }

We then check that the loop is broken into four for loops like so, if the pass
is run twice:
    for (int i = 0; i < 10; i++)
        A[i] = B[i];
    for (int i = 0; i < 10; i++)
        B[i] = A[i];
    for (int i = 0; i < 10; i++)
        C[i] = D[i];
    for (int i = 0; i < 10; i++)
        D[i] = C[i];

*/

TEST_F(FissionClassTest, FissionMultipleLoadStores) {
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
OpBranch %43
%43 = OpLabel
%44 = OpPhi %10 %12 %22 %61 %60
OpLoopMerge %62 %60 None
OpBranch %45
%45 = OpLabel
%46 = OpSLessThan %14 %44 %13
OpBranchConditional %46 %47 %62
%47 = OpLabel
%48 = OpAccessChain %20 %5 %44
%49 = OpLoad %15 %48
%50 = OpAccessChain %20 %4 %44
OpStore %50 %49
%51 = OpAccessChain %20 %4 %44
%52 = OpLoad %15 %51
%53 = OpAccessChain %20 %5 %44
OpStore %53 %52
OpBranch %60
%60 = OpLabel
%61 = OpIAdd %10 %44 %21
OpBranch %43
%62 = OpLabel
OpBranch %23
%23 = OpLabel
%24 = OpPhi %10 %12 %62 %25 %26
OpLoopMerge %27 %26 None
OpBranch %28
%28 = OpLabel
%29 = OpSLessThan %14 %24 %13
OpBranchConditional %29 %30 %27
%30 = OpLabel
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


const std::string expected_multiple_passes = R"(OpCapability Shader
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
OpBranch %63
%63 = OpLabel
%64 = OpPhi %10 %12 %22 %75 %74
OpLoopMerge %76 %74 None
OpBranch %65
%65 = OpLabel
%66 = OpSLessThan %14 %64 %13
OpBranchConditional %66 %67 %76
%67 = OpLabel
%68 = OpAccessChain %20 %5 %64
%69 = OpLoad %15 %68
%70 = OpAccessChain %20 %4 %64
OpStore %70 %69
OpBranch %74
%74 = OpLabel
%75 = OpIAdd %10 %64 %21
OpBranch %63
%76 = OpLabel
OpBranch %43
%43 = OpLabel
%44 = OpPhi %10 %12 %76 %61 %60
OpLoopMerge %62 %60 None
OpBranch %45
%45 = OpLabel
%46 = OpSLessThan %14 %44 %13
OpBranchConditional %46 %47 %62
%47 = OpLabel
%51 = OpAccessChain %20 %4 %44
%52 = OpLoad %15 %51
%53 = OpAccessChain %20 %5 %44
OpStore %53 %52
OpBranch %60
%60 = OpLabel
%61 = OpIAdd %10 %44 %21
OpBranch %43
%62 = OpLabel
OpBranch %77
%77 = OpLabel
%78 = OpPhi %10 %12 %62 %89 %88
OpLoopMerge %90 %88 None
OpBranch %79
%79 = OpLabel
%80 = OpSLessThan %14 %78 %13
OpBranchConditional %80 %81 %90
%81 = OpLabel
%82 = OpAccessChain %20 %7 %78
%83 = OpLoad %15 %82
%84 = OpAccessChain %20 %6 %78
OpStore %84 %83
OpBranch %88
%88 = OpLabel
%89 = OpIAdd %10 %78 %21
OpBranch %77
%90 = OpLabel
OpBranch %23
%23 = OpLabel
%24 = OpPhi %10 %12 %90 %25 %26
OpLoopMerge %27 %26 None
OpBranch %28
%28 = OpLabel
%29 = OpSLessThan %14 %24 %13
OpBranchConditional %29 %30 %27
%30 = OpLabel
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

  // By passing 1 as argument we are using the constructor which makes the
  // critera to split the loop be if the registers in the loop exceede 1. By
  // using this constructor we are also enabling multiple passes (disabled by
  // default).
  SinglePassRunAndCheck<opt::LoopFissionPass>(source, expected_multiple_passes,
                                              true, 1);
  /*  std::cout << std::get<0>(
        SinglePassRunAndDisassemble<opt::LoopFissionPass>(source, false, true,
     1));*/
}

/*
#version 430
void main(void) {
    int accumulator = 0;
    float X[10];
    float Y[10];

    for (int i = 0; i < 10; i++) {
        X[i] = Y[i];
        Y[i] = X[i];
        accumulator += i;
    }
}

This should be split into the equivalent of:

#version 430
void main(void) {
    int accumulator = 0;
    float X[10];
    float Y[10];

    for (int i = 0; i < 10; i++) {
        X[i] = Y[i];
    }
    for (int i = 0; i < 10; i++) {
        Y[i] = X[i];
        accumulator += i;
    }
}
*/
TEST_F(FissionClassTest, FissionWithAccumulator) {
  // clang-format off
  // With opt::LocalMultiStoreElimPass
  const std::string source = R"(OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main"
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 430
               OpName %2 "main"
               OpName %3 "accumulator"
               OpName %4 "i"
               OpName %5 "X"
               OpName %6 "Y"
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
         %24 = OpIAdd %9 %23 %26
               OpBranch %25
         %25 = OpLabel
         %27 = OpIAdd %9 %26 %20
               OpBranch %22
         %28 = OpLabel
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
OpName %3 "accumulator"
OpName %4 "i"
OpName %5 "X"
OpName %6 "Y"
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
OpBranch %38
%38 = OpLabel
%40 = OpPhi %9 %11 %21 %52 %51
OpLoopMerge %53 %51 None
OpBranch %41
%41 = OpLabel
%42 = OpSLessThan %13 %40 %12
OpBranchConditional %42 %43 %53
%43 = OpLabel
%44 = OpAccessChain %19 %6 %40
%45 = OpLoad %14 %44
%46 = OpAccessChain %19 %5 %40
OpStore %46 %45
OpBranch %51
%51 = OpLabel
%52 = OpIAdd %9 %40 %20
OpBranch %38
%53 = OpLabel
OpBranch %22
%22 = OpLabel
%23 = OpPhi %9 %11 %53 %24 %25
%26 = OpPhi %9 %11 %53 %27 %25
OpLoopMerge %28 %25 None
OpBranch %29
%29 = OpLabel
%30 = OpSLessThan %13 %26 %12
OpBranchConditional %30 %31 %28
%31 = OpLabel
%35 = OpAccessChain %19 %5 %26
%36 = OpLoad %14 %35
%37 = OpAccessChain %19 %6 %26
OpStore %37 %36
%24 = OpIAdd %9 %23 %26
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
Generated from the following glsl:

#version 430
layout(location=0) out float x;
layout(location=1) out float y;

void main(void) {
    float accumulator_1 = 0;
    float accumulator_2 = 0;
    for (int i = 0; i < 10; i++) {
        accumulator_1 += i;
        accumulator_2 += i;
    }

    x = accumulator_1;
    y = accumulator_2;
}

Should be split into equivalent of:

void main(void) {
    float accumulator_1 = 0;
    float accumulator_2 = 0;
    for (int i = 0; i < 10; i++) {
        accumulator_1 += i;
    }

    for (int i = 0; i < 10; i++) {
        accumulator_2 += i;
    }
    x = accumulator_1;
    y = accumulator_2;
}

*/
TEST_F(FissionClassTest, FissionWithPhisUsedOutwithLoop) {
  // clang-format off
  // With opt::LocalMultiStoreElimPass
  const std::string source = R"(OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main" %3 %4
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 430
               OpName %2 "main"
               OpName %5 "accumulator_1"
               OpName %6 "accumulator_2"
               OpName %7 "i"
               OpName %3 "x"
               OpName %4 "y"
               OpDecorate %3 Location 0
               OpDecorate %4 Location 1
          %8 = OpTypeVoid
          %9 = OpTypeFunction %8
         %10 = OpTypeFloat 32
         %11 = OpTypePointer Function %10
         %12 = OpConstant %10 0
         %13 = OpTypeInt 32 1
         %14 = OpTypePointer Function %13
         %15 = OpConstant %13 0
         %16 = OpConstant %13 10
         %17 = OpTypeBool
         %18 = OpConstant %13 1
         %19 = OpTypePointer Output %10
          %3 = OpVariable %19 Output
          %4 = OpVariable %19 Output
          %2 = OpFunction %8 None %9
         %20 = OpLabel
          %5 = OpVariable %11 Function
          %6 = OpVariable %11 Function
          %7 = OpVariable %14 Function
               OpBranch %21
         %21 = OpLabel
         %22 = OpPhi %10 %12 %20 %23 %24
         %25 = OpPhi %10 %12 %20 %26 %24
         %27 = OpPhi %13 %15 %20 %28 %24
               OpLoopMerge %29 %24 None
               OpBranch %30
         %30 = OpLabel
         %31 = OpSLessThan %17 %27 %16
               OpBranchConditional %31 %32 %29
         %32 = OpLabel
         %33 = OpConvertSToF %10 %27
         %26 = OpFAdd %10 %25 %33
         %34 = OpConvertSToF %10 %27
         %23 = OpFAdd %10 %22 %34
               OpBranch %24
         %24 = OpLabel
         %28 = OpIAdd %13 %27 %18
               OpStore %7 %28
               OpBranch %21
         %29 = OpLabel
               OpStore %3 %25
               OpStore %4 %22
               OpReturn
               OpFunctionEnd
  )";

  const std::string expected = R"(OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %2 "main" %3 %4
OpExecutionMode %2 OriginUpperLeft
OpSource GLSL 430
OpName %2 "main"
OpName %5 "accumulator_1"
OpName %6 "accumulator_2"
OpName %7 "i"
OpName %3 "x"
OpName %4 "y"
OpDecorate %3 Location 0
OpDecorate %4 Location 1
%8 = OpTypeVoid
%9 = OpTypeFunction %8
%10 = OpTypeFloat 32
%11 = OpTypePointer Function %10
%12 = OpConstant %10 0
%13 = OpTypeInt 32 1
%14 = OpTypePointer Function %13
%15 = OpConstant %13 0
%16 = OpConstant %13 10
%17 = OpTypeBool
%18 = OpConstant %13 1
%19 = OpTypePointer Output %10
%3 = OpVariable %19 Output
%4 = OpVariable %19 Output
%2 = OpFunction %8 None %9
%20 = OpLabel
%5 = OpVariable %11 Function
%6 = OpVariable %11 Function
%7 = OpVariable %14 Function
OpBranch %35
%35 = OpLabel
%37 = OpPhi %10 %12 %20 %43 %46
%38 = OpPhi %13 %15 %20 %47 %46
OpLoopMerge %48 %46 None
OpBranch %39
%39 = OpLabel
%40 = OpSLessThan %17 %38 %16
OpBranchConditional %40 %41 %48
%41 = OpLabel
%42 = OpConvertSToF %10 %38
%43 = OpFAdd %10 %37 %42
OpBranch %46
%46 = OpLabel
%47 = OpIAdd %13 %38 %18
OpStore %7 %47
OpBranch %35
%48 = OpLabel
OpBranch %21
%21 = OpLabel
%22 = OpPhi %10 %12 %48 %23 %24
%27 = OpPhi %13 %15 %48 %28 %24
OpLoopMerge %29 %24 None
OpBranch %30
%30 = OpLabel
%31 = OpSLessThan %17 %27 %16
OpBranchConditional %31 %32 %29
%32 = OpLabel
%34 = OpConvertSToF %10 %27
%23 = OpFAdd %10 %22 %34
OpBranch %24
%24 = OpLabel
%28 = OpIAdd %13 %27 %18
OpStore %7 %28
OpBranch %21
%29 = OpLabel
OpStore %3 %37
OpStore %4 %22
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

Should be split into equivalent of:

#version 430
void main(void) {
    float A[10][10];
    float B[10][10];
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            A[i][j] = B[i][j];
        }
        for (int j = 0; j < 10; j++) {
            B[i][j] = A[i][j];
        }
    }
}


*/
TEST_F(FissionClassTest, FissionNested) {
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

  const std::string expected = R"(OpCapability Shader
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
OpBranch %45
%45 = OpLabel
%46 = OpPhi %9 %11 %30 %57 %56
OpLoopMerge %58 %56 None
OpBranch %47
%47 = OpLabel
%48 = OpSLessThan %13 %46 %12
OpBranchConditional %48 %49 %58
%49 = OpLabel
%50 = OpAccessChain %20 %6 %24 %46
%51 = OpLoad %14 %50
%52 = OpAccessChain %20 %5 %24 %46
OpStore %52 %51
OpBranch %56
%56 = OpLabel
%57 = OpIAdd %9 %46 %21
OpStore %4 %57
OpBranch %45
%58 = OpLabel
OpBranch %31
%31 = OpLabel
%32 = OpPhi %9 %11 %58 %33 %34
OpLoopMerge %35 %34 None
OpBranch %36
%36 = OpLabel
%37 = OpSLessThan %13 %32 %12
OpBranchConditional %37 %38 %35
%38 = OpLabel
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
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, source,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << source << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 2);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  EXPECT_EQ(ld.NumLoops(), 2u);

  SetDisassembleOptions(SPV_BINARY_TO_TEXT_OPTION_NO_HEADER);
  SinglePassRunAndCheck<opt::LoopFissionPass>(source, expected, true);
}

/*
#version 430
void main(void) {
    int accumulator = 0;
    float A[10];
    float B[10];
    float C[10];

    for (int i = 0; i < 10; i++) {
        int c = C[i];
        A[i] = B[i];
        B[i] = A[i] + c;
    }
}

This loop should not be split as we would have to break the order of the loads
to do so. It would be grouped into two sets:

1
 int c = C[i];
 B[i] = A[i] + c;

2
 A[i] = B[i];

To keep the load C[i] in the same order we would need to put B[i] ahead of that
*/
TEST_F(FissionClassTest, FissionLoad) {
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
%18 = OpConstant %17 10
%19 = OpTypeArray %15 %18
%20 = OpTypePointer Function %19
%21 = OpConstant %10 1
%2 = OpFunction %8 None %9
%22 = OpLabel
%3 = OpVariable %11 Function
%4 = OpVariable %16 Function
%5 = OpVariable %20 Function
%6 = OpVariable %20 Function
%7 = OpVariable %20 Function
OpBranch %23
%23 = OpLabel
%24 = OpPhi %10 %12 %22 %25 %26
OpLoopMerge %27 %26 None
OpBranch %28
%28 = OpLabel
%29 = OpSLessThan %14 %24 %13
OpBranchConditional %29 %30 %27
%30 = OpLabel
%31 = OpAccessChain %16 %5 %24
%32 = OpLoad %15 %31
OpStore %4 %32
%33 = OpAccessChain %16 %7 %24
%34 = OpLoad %15 %33
%35 = OpAccessChain %16 %6 %24
OpStore %35 %34
%36 = OpAccessChain %16 %6 %24
%37 = OpLoad %15 %36
%38 = OpFAdd %15 %37 %32
%39 = OpAccessChain %16 %7 %24
OpStore %39 %38
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
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, source,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << source << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 2);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  EXPECT_EQ(ld.NumLoops(), 1u);

  SetDisassembleOptions(SPV_BINARY_TO_TEXT_OPTION_NO_HEADER);
  SinglePassRunAndCheck<opt::LoopFissionPass>(source, source, true);
}

/*
#version 430
layout(location=0) flat in int condition;
void main(void) {
    float A[10];
    float B[10];

    for (int i = 0; i < 10; i++) {
        if (condition == 1)
            A[i] = B[i];
        else
            B[i] = A[i];
    }
}


When this is split we leave the condition check and control flow inplace and
leave its removal for dead code elimination.

#version 430
layout(location=0) flat in int condition;
void main(void) {
    float A[10];
    float B[10];

    for (int i = 0; i < 10; i++) {
        if (condition == 1)
            A[i] = B[i];
        else
            ;
    }
      for (int i = 0; i < 10; i++) {
        if (condition == 1)
            ;
        else
            B[i] = A[i];
    }
}


*/
TEST_F(FissionClassTest, FissionControlFlow) {
  // clang-format off
  // With opt::LocalMultiStoreElimPass
  const std::string source = R"(
              OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main" %3
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 430
               OpName %2 "main"
               OpName %4 "i"
               OpName %3 "condition"
               OpName %5 "A"
               OpName %6 "B"
               OpDecorate %3 Flat
               OpDecorate %3 Location 0
          %7 = OpTypeVoid
          %8 = OpTypeFunction %7
          %9 = OpTypeInt 32 1
         %10 = OpTypePointer Function %9
         %11 = OpConstant %9 0
         %12 = OpConstant %9 10
         %13 = OpTypeBool
         %14 = OpTypePointer Input %9
          %3 = OpVariable %14 Input
         %15 = OpConstant %9 1
         %16 = OpTypeFloat 32
         %17 = OpTypeInt 32 0
         %18 = OpConstant %17 10
         %19 = OpTypeArray %16 %18
         %20 = OpTypePointer Function %19
         %21 = OpTypePointer Function %16
          %2 = OpFunction %7 None %8
         %22 = OpLabel
          %4 = OpVariable %10 Function
          %5 = OpVariable %20 Function
          %6 = OpVariable %20 Function
         %31 = OpLoad %9 %3
               OpStore %4 %11
               OpBranch %23
         %23 = OpLabel
         %24 = OpPhi %9 %11 %22 %25 %26
               OpLoopMerge %27 %26 None
               OpBranch %28
         %28 = OpLabel
         %29 = OpSLessThan %13 %24 %12
               OpBranchConditional %29 %30 %27
         %30 = OpLabel
         %32 = OpIEqual %13 %31 %15
               OpSelectionMerge %33 None
               OpBranchConditional %32 %34 %35
         %34 = OpLabel
         %36 = OpAccessChain %21 %6 %24
         %37 = OpLoad %16 %36
         %38 = OpAccessChain %21 %5 %24
               OpStore %38 %37
               OpBranch %33
         %35 = OpLabel
         %39 = OpAccessChain %21 %5 %24
         %40 = OpLoad %16 %39
         %41 = OpAccessChain %21 %6 %24
               OpStore %41 %40
               OpBranch %33
         %33 = OpLabel
               OpBranch %26
         %26 = OpLabel
         %25 = OpIAdd %9 %24 %15
               OpStore %4 %25
               OpBranch %23
         %27 = OpLabel
               OpReturn
               OpFunctionEnd
  )";

  const std::string expected = R"(OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %2 "main" %3
OpExecutionMode %2 OriginUpperLeft
OpSource GLSL 430
OpName %2 "main"
OpName %4 "i"
OpName %3 "condition"
OpName %5 "A"
OpName %6 "B"
OpDecorate %3 Flat
OpDecorate %3 Location 0
%7 = OpTypeVoid
%8 = OpTypeFunction %7
%9 = OpTypeInt 32 1
%10 = OpTypePointer Function %9
%11 = OpConstant %9 0
%12 = OpConstant %9 10
%13 = OpTypeBool
%14 = OpTypePointer Input %9
%3 = OpVariable %14 Input
%15 = OpConstant %9 1
%16 = OpTypeFloat 32
%17 = OpTypeInt 32 0
%18 = OpConstant %17 10
%19 = OpTypeArray %16 %18
%20 = OpTypePointer Function %19
%21 = OpTypePointer Function %16
%2 = OpFunction %7 None %8
%22 = OpLabel
%4 = OpVariable %10 Function
%5 = OpVariable %20 Function
%6 = OpVariable %20 Function
%23 = OpLoad %9 %3
OpStore %4 %11
OpBranch %42
%42 = OpLabel
%43 = OpPhi %9 %11 %22 %58 %57
OpLoopMerge %59 %57 None
OpBranch %44
%44 = OpLabel
%45 = OpSLessThan %13 %43 %12
OpBranchConditional %45 %46 %59
%46 = OpLabel
%47 = OpIEqual %13 %23 %15
OpSelectionMerge %56 None
OpBranchConditional %47 %52 %48
%48 = OpLabel
OpBranch %56
%52 = OpLabel
%53 = OpAccessChain %21 %6 %43
%54 = OpLoad %16 %53
%55 = OpAccessChain %21 %5 %43
OpStore %55 %54
OpBranch %56
%56 = OpLabel
OpBranch %57
%57 = OpLabel
%58 = OpIAdd %9 %43 %15
OpStore %4 %58
OpBranch %42
%59 = OpLabel
OpBranch %24
%24 = OpLabel
%25 = OpPhi %9 %11 %59 %26 %27
OpLoopMerge %28 %27 None
OpBranch %29
%29 = OpLabel
%30 = OpSLessThan %13 %25 %12
OpBranchConditional %30 %31 %28
%31 = OpLabel
%32 = OpIEqual %13 %23 %15
OpSelectionMerge %33 None
OpBranchConditional %32 %34 %35
%34 = OpLabel
OpBranch %33
%35 = OpLabel
%39 = OpAccessChain %21 %5 %25
%40 = OpLoad %16 %39
%41 = OpAccessChain %21 %6 %25
OpStore %41 %40
OpBranch %33
%33 = OpLabel
OpBranch %27
%27 = OpLabel
%26 = OpIAdd %9 %25 %15
OpStore %4 %26
OpBranch %24
%28 = OpLabel
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
#version 430
layout(location=0) flat in int condition;
void main(void) {
    float A[10];
    float B[10];

    for (int i = 0; i < 10; i++) {
        if (condition == 1)
            A[i] = B[i];
        else
            B[i] = A[i];
    }
}


*/
TEST_F(FissionClassTest, FissionControlFlow2) {
  // clang-format off
  // With opt::LocalMultiStoreElimPass
  const std::string source = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main" %3
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 430
               OpName %2 "main"
               OpName %4 "i"
               OpName %3 "condition"
               OpName %5 "A"
               OpName %6 "B"
               OpDecorate %3 Flat
               OpDecorate %3 Location 0
          %7 = OpTypeVoid
          %8 = OpTypeFunction %7
          %9 = OpTypeInt 32 1
         %10 = OpTypePointer Function %9
         %11 = OpConstant %9 0
         %12 = OpConstant %9 10
         %13 = OpTypeBool
         %14 = OpTypePointer Input %9
          %3 = OpVariable %14 Input
         %15 = OpConstant %9 1
         %16 = OpTypeFloat 32
         %17 = OpTypeInt 32 0
         %18 = OpConstant %17 10
         %19 = OpTypeArray %16 %18
         %20 = OpTypePointer Function %19
         %21 = OpTypePointer Function %16
         %22 = OpConstant %9 2
         %23 = OpConstant %16 2
          %2 = OpFunction %7 None %8
         %24 = OpLabel
          %4 = OpVariable %10 Function
          %5 = OpVariable %20 Function
          %6 = OpVariable %20 Function
         %33 = OpLoad %9 %3
               OpBranch %25
         %25 = OpLabel
         %26 = OpPhi %9 %11 %24 %27 %28
               OpLoopMerge %29 %28 None
               OpBranch %30
         %30 = OpLabel
         %31 = OpSLessThan %13 %26 %12
               OpBranchConditional %31 %32 %29
         %32 = OpLabel
         %34 = OpIEqual %13 %33 %15
               OpSelectionMerge %35 None
               OpBranchConditional %34 %36 %37
         %36 = OpLabel
         %38 = OpAccessChain %21 %6 %26
         %39 = OpLoad %16 %38
         %40 = OpAccessChain %21 %5 %26
               OpStore %40 %39
               OpBranch %35
         %37 = OpLabel
         %42 = OpIEqual %13 %33 %22
               OpSelectionMerge %43 None
               OpBranchConditional %42 %44 %45
         %44 = OpLabel
         %46 = OpAccessChain %21 %5 %26
         %47 = OpLoad %16 %46
         %48 = OpAccessChain %21 %6 %26
               OpStore %48 %47
               OpBranch %43
         %45 = OpLabel
         %49 = OpAccessChain %21 %6 %26
         %50 = OpLoad %16 %49
         %51 = OpFMul %16 %50 %23
         %52 = OpAccessChain %21 %5 %26
               OpStore %52 %51
               OpBranch %43
         %43 = OpLabel
               OpBranch %35
         %35 = OpLabel
               OpBranch %28
         %28 = OpLabel
         %27 = OpIAdd %9 %26 %15
               OpBranch %25
         %29 = OpLabel
               OpReturn
               OpFunctionEnd
  )";

  const std::string expected = R"(OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %2 "main" %3
OpExecutionMode %2 OriginUpperLeft
OpSource GLSL 430
OpName %2 "main"
OpName %4 "i"
OpName %3 "condition"
OpName %5 "A"
OpName %6 "B"
OpDecorate %3 Flat
OpDecorate %3 Location 0
%7 = OpTypeVoid
%8 = OpTypeFunction %7
%9 = OpTypeInt 32 1
%10 = OpTypePointer Function %9
%11 = OpConstant %9 0
%12 = OpConstant %9 10
%13 = OpTypeBool
%14 = OpTypePointer Input %9
%3 = OpVariable %14 Input
%15 = OpConstant %9 1
%16 = OpTypeFloat 32
%17 = OpTypeInt 32 0
%18 = OpConstant %17 10
%19 = OpTypeArray %16 %18
%20 = OpTypePointer Function %19
%21 = OpTypePointer Function %16
%22 = OpConstant %9 2
%23 = OpConstant %16 2
%2 = OpFunction %7 None %8
%24 = OpLabel
%4 = OpVariable %10 Function
%5 = OpVariable %20 Function
%6 = OpVariable %20 Function
%25 = OpLoad %9 %3
OpBranch %52
%52 = OpLabel
%53 = OpPhi %9 %11 %24 %76 %75
OpLoopMerge %77 %75 None
OpBranch %54
%54 = OpLabel
%55 = OpSLessThan %13 %53 %12
OpBranchConditional %55 %56 %77
%56 = OpLabel
%57 = OpIEqual %13 %25 %15
OpSelectionMerge %74 None
OpBranchConditional %57 %70 %58
%58 = OpLabel
%59 = OpIEqual %13 %25 %22
OpSelectionMerge %69 None
OpBranchConditional %59 %65 %60
%60 = OpLabel
OpBranch %69
%65 = OpLabel
%66 = OpAccessChain %21 %5 %53
%67 = OpLoad %16 %66
%68 = OpAccessChain %21 %6 %53
OpStore %68 %67
OpBranch %69
%69 = OpLabel
OpBranch %74
%70 = OpLabel
OpBranch %74
%74 = OpLabel
OpBranch %75
%75 = OpLabel
%76 = OpIAdd %9 %53 %15
OpBranch %52
%77 = OpLabel
OpBranch %26
%26 = OpLabel
%27 = OpPhi %9 %11 %77 %28 %29
OpLoopMerge %30 %29 None
OpBranch %31
%31 = OpLabel
%32 = OpSLessThan %13 %27 %12
OpBranchConditional %32 %33 %30
%33 = OpLabel
%34 = OpIEqual %13 %25 %15
OpSelectionMerge %35 None
OpBranchConditional %34 %36 %37
%36 = OpLabel
%38 = OpAccessChain %21 %6 %27
%39 = OpLoad %16 %38
%40 = OpAccessChain %21 %5 %27
OpStore %40 %39
OpBranch %35
%37 = OpLabel
%41 = OpIEqual %13 %25 %22
OpSelectionMerge %42 None
OpBranchConditional %41 %43 %44
%43 = OpLabel
OpBranch %42
%44 = OpLabel
%48 = OpAccessChain %21 %6 %27
%49 = OpLoad %16 %48
%50 = OpFMul %16 %49 %23
%51 = OpAccessChain %21 %5 %27
OpStore %51 %50
OpBranch %42
%42 = OpLabel
OpBranch %35
%35 = OpLabel
OpBranch %29
%29 = OpLabel
%28 = OpIAdd %9 %27 %15
OpBranch %26
%30 = OpLabel
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
#version 430
layout(location=0) flat in int condition;
void main(void) {
    float A[10];
    float B[10];
    for (int i = 0; i < 10; i++) {
      B[i] = A[i];
      memoryBarrier();
      A[i] = B[i];
    }
}

This should not be split due to the memory barrier.
*/
TEST_F(FissionClassTest, FissionBarrier) {
  // clang-format off
  // With opt::LocalMultiStoreElimPass
const std::string source = R"(OpCapability Shader
%1 = OpExtInstImport "GLSL.std.450"
OpMemoryModel Logical GLSL450
OpEntryPoint Fragment %2 "main" %3
OpExecutionMode %2 OriginUpperLeft
OpSource GLSL 430
OpName %2 "main"
OpName %4 "i"
OpName %5 "B"
OpName %6 "A"
OpName %3 "condition"
OpDecorate %3 Flat
OpDecorate %3 Location 0
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
%20 = OpConstant %15 1
%21 = OpConstant %15 4048
%22 = OpConstant %9 1
%23 = OpTypePointer Input %9
%3 = OpVariable %23 Input
%2 = OpFunction %7 None %8
%24 = OpLabel
%4 = OpVariable %10 Function
%5 = OpVariable %18 Function
%6 = OpVariable %18 Function
OpStore %4 %11
OpBranch %25
%25 = OpLabel
%26 = OpPhi %9 %11 %24 %27 %28
OpLoopMerge %29 %28 None
OpBranch %30
%30 = OpLabel
%31 = OpSLessThan %13 %26 %12
OpBranchConditional %31 %32 %29
%32 = OpLabel
%33 = OpAccessChain %19 %6 %26
%34 = OpLoad %14 %33
%35 = OpAccessChain %19 %5 %26
OpStore %35 %34
OpMemoryBarrier %20 %21
%36 = OpAccessChain %19 %5 %26
%37 = OpLoad %14 %36
%38 = OpAccessChain %19 %6 %26
OpStore %38 %37
OpBranch %28
%28 = OpLabel
%27 = OpIAdd %9 %26 %22
OpStore %4 %27
OpBranch %25
%29 = OpLabel
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
  SinglePassRunAndCheck<opt::LoopFissionPass>(source, source, true);
}

}  // namespace
