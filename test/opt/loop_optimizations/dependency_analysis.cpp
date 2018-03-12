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
#include <unordered_set>
#include <vector>

#include "../assembly_builder.h"
#include "../function_utils.h"
#include "../pass_fixture.h"
#include "../pass_utils.h"

#include "opt/iterator.h"
#include "opt/loop_dependence.h"
#include "opt/loop_descriptor.h"
#include "opt/pass.h"
#include "opt/tree_iterator.h"

namespace {

using namespace spvtools;
using DependencyAnalysis = ::testing::Test;

/*
  Generated from the following GLSL fragment shader
  with --eliminate-local-multi-store
#version 440 core
void main(){
  int[10] arr;
  int[10] arr2;
  int a = 2;
  for (int i = 0; i < 10; i++) {
    arr[a] = arr[3];
    arr[a*2] = arr[a+3];
    arr[6] = arr2[6];
    arr[a+5] = arr2[7];
  }
}
*/
TEST(DependencyAnalysis, ZIV) {
  const std::string text = R"(               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main"
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 440
               OpName %4 "main"
               OpName %25 "arr"
               OpName %39 "arr2"
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %6 = OpTypeInt 32 1
          %7 = OpTypePointer Function %6
          %9 = OpConstant %6 2
         %11 = OpConstant %6 0
         %18 = OpConstant %6 10
         %19 = OpTypeBool
         %21 = OpTypeInt 32 0
         %22 = OpConstant %21 10
         %23 = OpTypeArray %6 %22
         %24 = OpTypePointer Function %23
         %27 = OpConstant %6 3
         %38 = OpConstant %6 6
         %44 = OpConstant %6 5
         %46 = OpConstant %6 7
         %51 = OpConstant %6 1
          %4 = OpFunction %2 None %3
          %5 = OpLabel
         %25 = OpVariable %24 Function
         %39 = OpVariable %24 Function
               OpBranch %12
         %12 = OpLabel
         %53 = OpPhi %6 %11 %5 %52 %15
               OpLoopMerge %14 %15 None
               OpBranch %16
         %16 = OpLabel
         %20 = OpSLessThan %19 %53 %18
               OpBranchConditional %20 %13 %14
         %13 = OpLabel
         %28 = OpAccessChain %7 %25 %27
         %29 = OpLoad %6 %28
         %30 = OpAccessChain %7 %25 %9
               OpStore %30 %29
         %32 = OpIMul %6 %9 %9
         %34 = OpIAdd %6 %9 %27
         %35 = OpAccessChain %7 %25 %34
         %36 = OpLoad %6 %35
         %37 = OpAccessChain %7 %25 %32
               OpStore %37 %36
         %40 = OpAccessChain %7 %39 %38
         %41 = OpLoad %6 %40
         %42 = OpAccessChain %7 %25 %38
               OpStore %42 %41
         %45 = OpIAdd %6 %9 %44
         %47 = OpAccessChain %7 %39 %46
         %48 = OpLoad %6 %47
         %49 = OpAccessChain %7 %25 %45
               OpStore %49 %48
               OpBranch %15
         %15 = OpLabel
         %52 = OpIAdd %6 %53 %51
               OpBranch %12
         %14 = OpLabel
               OpReturn
               OpFunctionEnd
)";
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << text << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 4);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

  const ir::Instruction* store[4];
  int stores_found = 0;
  for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 13)) {
    if (inst.opcode() == SpvOp::SpvOpStore) {
      store[stores_found] = &inst;
      ++stores_found;
    }
  }

  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(store[i]);
  }

  opt::DVEntry dv_entry{};

  // 29 -> 30 tests looking through constants
  EXPECT_TRUE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(29),
                                     store[0], &dv_entry));

  // 36 -> 37 tests looking through additions
  EXPECT_TRUE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(36),
                                     store[1], &dv_entry));

  // 41 -> 42 tests looking at same index across two different arrays
  EXPECT_TRUE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(41),
                                     store[2], &dv_entry));

  // 48 -> 49 tests looking through additions for same index in two different
  // arrays
  EXPECT_TRUE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(48),
                                     store[3], &dv_entry));
}

/*
  Generated from the following GLSL fragment shader
  with --eliminate-local-multi-store
#version 440 core
layout(location = 0) in vec4 c;
void main(){
  int[10] arr;
  int[10] arr2;
  int[10] arr3;
  int[10] arr4;
  int[10] arr5;
  int N = int(c.x);
  for (int i = 0; i < N; i++) {
    arr[2*N] = arr[N];
    arr2[2*N+1] = arr2[N];
    arr3[2*N] = arr3[N-1];
    arr4[N] = arr5[N];
  }
}
*/
TEST(DependencyAnalysis, SymbolicZIV) {
  const std::string text = R"(               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main" %12
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 440
               OpName %4 "main"
               OpName %12 "c"
               OpName %33 "arr"
               OpName %41 "arr2"
               OpName %50 "arr3"
               OpName %58 "arr4"
               OpName %60 "arr5"
               OpDecorate %12 Location 0
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %6 = OpTypeInt 32 1
          %7 = OpTypePointer Function %6
          %9 = OpTypeFloat 32
         %10 = OpTypeVector %9 4
         %11 = OpTypePointer Input %10
         %12 = OpVariable %11 Input
         %13 = OpTypeInt 32 0
         %14 = OpConstant %13 0
         %15 = OpTypePointer Input %9
         %20 = OpConstant %6 0
         %28 = OpTypeBool
         %30 = OpConstant %13 10
         %31 = OpTypeArray %6 %30
         %32 = OpTypePointer Function %31
         %34 = OpConstant %6 2
         %44 = OpConstant %6 1
          %4 = OpFunction %2 None %3
          %5 = OpLabel
         %33 = OpVariable %32 Function
         %41 = OpVariable %32 Function
         %50 = OpVariable %32 Function
         %58 = OpVariable %32 Function
         %60 = OpVariable %32 Function
         %16 = OpAccessChain %15 %12 %14
         %17 = OpLoad %9 %16
         %18 = OpConvertFToS %6 %17
               OpBranch %21
         %21 = OpLabel
         %67 = OpPhi %6 %20 %5 %66 %24
               OpLoopMerge %23 %24 None
               OpBranch %25
         %25 = OpLabel
         %29 = OpSLessThan %28 %67 %18
               OpBranchConditional %29 %22 %23
         %22 = OpLabel
         %36 = OpIMul %6 %34 %18
         %38 = OpAccessChain %7 %33 %18
         %39 = OpLoad %6 %38
         %40 = OpAccessChain %7 %33 %36
               OpStore %40 %39
         %43 = OpIMul %6 %34 %18
         %45 = OpIAdd %6 %43 %44
         %47 = OpAccessChain %7 %41 %18
         %48 = OpLoad %6 %47
         %49 = OpAccessChain %7 %41 %45
               OpStore %49 %48
         %52 = OpIMul %6 %34 %18
         %54 = OpISub %6 %18 %44
         %55 = OpAccessChain %7 %50 %54
         %56 = OpLoad %6 %55
         %57 = OpAccessChain %7 %50 %52
               OpStore %57 %56
         %62 = OpAccessChain %7 %60 %18
         %63 = OpLoad %6 %62
         %64 = OpAccessChain %7 %58 %18
               OpStore %64 %63
               OpBranch %24
         %24 = OpLabel
         %66 = OpIAdd %6 %67 %44
               OpBranch %21
         %23 = OpLabel
               OpReturn
               OpFunctionEnd
)";
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << text << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 4);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

  const ir::Instruction* store[4];
  int stores_found = 0;
  for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 22)) {
    if (inst.opcode() == SpvOp::SpvOpStore) {
      store[stores_found] = &inst;
      ++stores_found;
    }
  }

  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(store[i]);
  }

  opt::DVEntry dv_entry{};

  // independent due to loop bounds (won't enter if N <= 0)
  // 39 -> 40 tests looking through symbols and multiplicaiton
  EXPECT_TRUE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(40),
                                     store[0], &dv_entry));

  // 48 -> 49 tests looking through symbols and multiplication + addition
  EXPECT_TRUE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(49),
                                     store[1], &dv_entry));

  // 56 -> 57 tests looking through symbols and arithmetic on load and store
  EXPECT_TRUE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(57),
                                     store[2], &dv_entry));

  // independent as different arrays
  // 63 -> 64 tests looking through symbols and load/store from/to different
  // arrays
  EXPECT_TRUE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(64),
                                     store[3], &dv_entry));
}

/*
  Generated from the following GLSL fragment shader
  with --eliminate-local-multi-store
#version 440 core
void main(){
  int[10] arr;
  int[11] arr2;
  int[20] arr3;
  int[20] arr4;
  int a = 2;
  for (int i = 0; i < 10; i++) {
    arr[i] = arr[i];
    arr2[i] = arr2[i+1];
    arr3[i] = arr3[i-1];
    arr4[2*i] = arr4[i];
  }
}
*/
TEST(DependencyAnalysis, SIV) {
  const std::string text = R"(               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main"
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 440
               OpName %4 "main"
               OpName %25 "arr"
               OpName %34 "arr2"
               OpName %45 "arr3"
               OpName %52 "arr4"
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %6 = OpTypeInt 32 1
          %7 = OpTypePointer Function %6
          %9 = OpConstant %6 2
         %11 = OpConstant %6 0
         %18 = OpConstant %6 10
         %19 = OpTypeBool
         %21 = OpTypeInt 32 0
         %22 = OpConstant %21 10
         %23 = OpTypeArray %6 %22
         %24 = OpTypePointer Function %23
         %31 = OpConstant %21 11
         %32 = OpTypeArray %6 %31
         %33 = OpTypePointer Function %32
         %37 = OpConstant %6 1
         %42 = OpConstant %21 20
         %43 = OpTypeArray %6 %42
         %44 = OpTypePointer Function %43
          %4 = OpFunction %2 None %3
          %5 = OpLabel
         %25 = OpVariable %24 Function
         %34 = OpVariable %33 Function
         %45 = OpVariable %44 Function
         %52 = OpVariable %44 Function
               OpBranch %12
         %12 = OpLabel
         %61 = OpPhi %6 %11 %5 %60 %15
               OpLoopMerge %14 %15 None
               OpBranch %16
         %16 = OpLabel
         %20 = OpSLessThan %19 %61 %18
               OpBranchConditional %20 %13 %14
         %13 = OpLabel
         %28 = OpAccessChain %7 %25 %61
         %29 = OpLoad %6 %28
         %30 = OpAccessChain %7 %25 %61
               OpStore %30 %29
         %38 = OpIAdd %6 %61 %37
         %39 = OpAccessChain %7 %34 %38
         %40 = OpLoad %6 %39
         %41 = OpAccessChain %7 %34 %61
               OpStore %41 %40
         %48 = OpISub %6 %61 %37
         %49 = OpAccessChain %7 %45 %48
         %50 = OpLoad %6 %49
         %51 = OpAccessChain %7 %45 %61
               OpStore %51 %50
         %54 = OpIMul %6 %9 %61
         %56 = OpAccessChain %7 %52 %61
         %57 = OpLoad %6 %56
         %58 = OpAccessChain %7 %52 %54
               OpStore %58 %57
               OpBranch %15
         %15 = OpLabel
         %60 = OpIAdd %6 %61 %37
               OpBranch %12
         %14 = OpLabel
               OpReturn
               OpFunctionEnd
)";
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << text << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 4);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

  const ir::Instruction* store[4];
  int stores_found = 0;
  for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 13)) {
    if (inst.opcode() == SpvOp::SpvOpStore) {
      store[stores_found] = &inst;
      ++stores_found;
    }
  }

  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(store[i]);
  }

  // = dependence
  // 29 -> 30 tests looking at SIV in same array
  {
    opt::DVEntry dv_entry{};
    EXPECT_FALSE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(29),
                                        store[0], &dv_entry));
    EXPECT_TRUE(dv_entry.direction == opt::DVDirections::EQ);
    EXPECT_TRUE(dv_entry.distance == 0);
  }

  // < -1 dependence
  // 40 -> 41 tests looking at SIV in same array with addition
  {
    opt::DVEntry dv_entry{};
    EXPECT_FALSE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(40),
                                        store[1], &dv_entry));
    EXPECT_TRUE(dv_entry.direction == opt::DVDirections::LT);
    EXPECT_TRUE(dv_entry.distance == -1);
  }

  // > 1 dependence
  // 50 -> 51 tests looking at SIV in same array with subtraction
  {
    opt::DVEntry dv_entry{};
    EXPECT_FALSE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(50),
                                        store[2], &dv_entry));
    EXPECT_TRUE(dv_entry.direction == opt::DVDirections::GT);
    EXPECT_TRUE(dv_entry.distance == 1);
  }

  // =,> dependence
  // 57 -> 58 tests looking at SIV in same array with multiplication
  {
    opt::DVEntry dv_entry{};
    EXPECT_FALSE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(57),
                                        store[3], &dv_entry));
    EXPECT_TRUE(dv_entry.direction == opt::DVDirections::GE);
  }
}

/*
  Generated from the following GLSL fragment shader
  with --eliminate-local-multi-store
#version 440 core
layout(location = 0) in vec4 c;
void main(){
  int[13] arr;
  int[15] arr2;
  int[18] arr3;
  int[18] arr4;
  int N = int(c.x);
  int C = 2;
  int a = 2;
  for (int i = 0; i < N; i++) {
    arr[i+2*N] = arr[i+N];
    arr2[i+2*N] = arr2[i+N] + C;
    arr3[2*i+2*N+1] = arr3[2*i+N+1];
    arr4[a*i+2*N+1] = arr4[a*i+N+1];
  }
}
*/
TEST(DependencyAnalysis, SymbolicSIV) {
  const std::string text = R"(               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main" %12
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 440
               OpName %4 "main"
               OpName %12 "c"
               OpName %36 "arr"
               OpName %50 "arr2"
               OpName %66 "arr3"
               OpName %82 "arr4"
               OpDecorate %12 Location 0
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %6 = OpTypeInt 32 1
          %7 = OpTypePointer Function %6
          %9 = OpTypeFloat 32
         %10 = OpTypeVector %9 4
         %11 = OpTypePointer Input %10
         %12 = OpVariable %11 Input
         %13 = OpTypeInt 32 0
         %14 = OpConstant %13 0
         %15 = OpTypePointer Input %9
         %20 = OpConstant %6 2
         %23 = OpConstant %6 0
         %31 = OpTypeBool
         %33 = OpConstant %13 13
         %34 = OpTypeArray %6 %33
         %35 = OpTypePointer Function %34
         %47 = OpConstant %13 15
         %48 = OpTypeArray %6 %47
         %49 = OpTypePointer Function %48
         %63 = OpConstant %13 18
         %64 = OpTypeArray %6 %63
         %65 = OpTypePointer Function %64
         %72 = OpConstant %6 1
          %4 = OpFunction %2 None %3
          %5 = OpLabel
         %36 = OpVariable %35 Function
         %50 = OpVariable %49 Function
         %66 = OpVariable %65 Function
         %82 = OpVariable %65 Function
         %16 = OpAccessChain %15 %12 %14
         %17 = OpLoad %9 %16
         %18 = OpConvertFToS %6 %17
               OpBranch %24
         %24 = OpLabel
        %101 = OpPhi %6 %23 %5 %100 %27
               OpLoopMerge %26 %27 None
               OpBranch %28
         %28 = OpLabel
         %32 = OpSLessThan %31 %101 %18
               OpBranchConditional %32 %25 %26
         %25 = OpLabel
         %39 = OpIMul %6 %20 %18
         %40 = OpIAdd %6 %101 %39
         %43 = OpIAdd %6 %101 %18
         %44 = OpAccessChain %7 %36 %43
         %45 = OpLoad %6 %44
         %46 = OpAccessChain %7 %36 %40
               OpStore %46 %45
         %53 = OpIMul %6 %20 %18
         %54 = OpIAdd %6 %101 %53
         %57 = OpIAdd %6 %101 %18
         %58 = OpAccessChain %7 %50 %57
         %59 = OpLoad %6 %58
         %61 = OpIAdd %6 %59 %20
         %62 = OpAccessChain %7 %50 %54
               OpStore %62 %61
         %68 = OpIMul %6 %20 %101
         %70 = OpIMul %6 %20 %18
         %71 = OpIAdd %6 %68 %70
         %73 = OpIAdd %6 %71 %72
         %75 = OpIMul %6 %20 %101
         %77 = OpIAdd %6 %75 %18
         %78 = OpIAdd %6 %77 %72
         %79 = OpAccessChain %7 %66 %78
         %80 = OpLoad %6 %79
         %81 = OpAccessChain %7 %66 %73
               OpStore %81 %80
         %85 = OpIMul %6 %20 %101
         %87 = OpIMul %6 %20 %18
         %88 = OpIAdd %6 %85 %87
         %89 = OpIAdd %6 %88 %72
         %92 = OpIMul %6 %20 %101
         %94 = OpIAdd %6 %92 %18
         %95 = OpIAdd %6 %94 %72
         %96 = OpAccessChain %7 %82 %95
         %97 = OpLoad %6 %96
         %98 = OpAccessChain %7 %82 %89
               OpStore %98 %97
               OpBranch %27
         %27 = OpLabel
        %100 = OpIAdd %6 %101 %72
               OpBranch %24
         %26 = OpLabel
               OpReturn
               OpFunctionEnd
)";
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << text << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 4);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

  const ir::Instruction* store[4];
  int stores_found = 0;
  for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 25)) {
    if (inst.opcode() == SpvOp::SpvOpStore) {
      store[stores_found] = &inst;
      ++stores_found;
    }
  }

  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(store[i]);
  }

  // independent due to loop bounds (won't enter when N <= 0)
  // 45 -> 46 tests looking through SIV and symbols with multiplication
  {
    opt::DVEntry dv_entry{};
    EXPECT_TRUE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(45),
                                       store[0], &dv_entry));
  }

  // 59 -> 62 tests looking through SIV and symbols with multiplication and + C
  {
    opt::DVEntry dv_entry{};
    EXPECT_TRUE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(59),
                                       store[1], &dv_entry));
  }

  // 80 -> 81 tests looking through arithmetic on SIV and symbols
  {
    opt::DVEntry dv_entry{};
    EXPECT_TRUE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(80),
                                       store[2], &dv_entry));
  }

  // 97 -> 98 tests looking through symbol arithmetic on SIV and symbols
  {
    opt::DVEntry dv_entry{};
    EXPECT_TRUE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(97),
                                       store[3], &dv_entry));
  }
}

/*
  Generated from the following GLSL fragment shader
  with --eliminate-local-multi-store
#version 440 core
void a() {
  int[6] arr;
  int N = 5;
  for (int i = 1; i < N; i++) {
    arr[i] = arr[N-i];
  }
}
void b() {
  int[6] arr;
  int N = 5;
  for (int i = 1; i < N; i++) {
    arr[N-i] = arr[i];
  }
}
void c() {
  int[11] arr;
  int N = 10;
  for (int i = 1; i < N; i++) {
    arr[i] = arr[N-i+1];
  }
}
void d() {
  int[11] arr;
  int N = 10;
  for (int i = 1; i < N; i++) {
    arr[N-i+1] = arr[i];
  }
}
void main(){
  a();
  b();
  c();
  d();
}
*/
TEST(DependencyAnalysis, Crossing) {
  const std::string text = R"(               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main"
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 440
               OpName %4 "main"
               OpName %6 "a("
               OpName %8 "b("
               OpName %10 "c("
               OpName %12 "d("
               OpName %33 "arr"
               OpName %53 "arr"
               OpName %77 "arr"
               OpName %98 "arr"
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
         %14 = OpTypeInt 32 1
         %15 = OpTypePointer Function %14
         %17 = OpConstant %14 5
         %19 = OpConstant %14 1
         %27 = OpTypeBool
         %29 = OpTypeInt 32 0
         %30 = OpConstant %29 6
         %31 = OpTypeArray %14 %30
         %32 = OpTypePointer Function %31
         %64 = OpConstant %14 10
         %74 = OpConstant %29 11
         %75 = OpTypeArray %14 %74
         %76 = OpTypePointer Function %75
          %4 = OpFunction %2 None %3
          %5 = OpLabel
        %109 = OpFunctionCall %2 %6
        %110 = OpFunctionCall %2 %8
        %111 = OpFunctionCall %2 %10
        %112 = OpFunctionCall %2 %12
               OpReturn
               OpFunctionEnd
          %6 = OpFunction %2 None %3
          %7 = OpLabel
         %33 = OpVariable %32 Function
               OpBranch %20
         %20 = OpLabel
        %113 = OpPhi %14 %19 %7 %42 %23
               OpLoopMerge %22 %23 None
               OpBranch %24
         %24 = OpLabel
         %28 = OpSLessThan %27 %113 %17
               OpBranchConditional %28 %21 %22
         %21 = OpLabel
         %37 = OpISub %14 %17 %113
         %38 = OpAccessChain %15 %33 %37
         %39 = OpLoad %14 %38
         %40 = OpAccessChain %15 %33 %113
               OpStore %40 %39
               OpBranch %23
         %23 = OpLabel
         %42 = OpIAdd %14 %113 %19
               OpBranch %20
         %22 = OpLabel
               OpReturn
               OpFunctionEnd
          %8 = OpFunction %2 None %3
          %9 = OpLabel
         %53 = OpVariable %32 Function
               OpBranch %45
         %45 = OpLabel
        %114 = OpPhi %14 %19 %9 %62 %48
               OpLoopMerge %47 %48 None
               OpBranch %49
         %49 = OpLabel
         %52 = OpSLessThan %27 %114 %17
               OpBranchConditional %52 %46 %47
         %46 = OpLabel
         %56 = OpISub %14 %17 %114
         %58 = OpAccessChain %15 %53 %114
         %59 = OpLoad %14 %58
         %60 = OpAccessChain %15 %53 %56
               OpStore %60 %59
               OpBranch %48
         %48 = OpLabel
         %62 = OpIAdd %14 %114 %19
               OpBranch %45
         %47 = OpLabel
               OpReturn
               OpFunctionEnd
         %10 = OpFunction %2 None %3
         %11 = OpLabel
         %77 = OpVariable %76 Function
               OpBranch %66
         %66 = OpLabel
        %115 = OpPhi %14 %19 %11 %87 %69
               OpLoopMerge %68 %69 None
               OpBranch %70
         %70 = OpLabel
         %73 = OpSLessThan %27 %115 %64
               OpBranchConditional %73 %67 %68
         %67 = OpLabel
         %81 = OpISub %14 %64 %115
         %82 = OpIAdd %14 %81 %19
         %83 = OpAccessChain %15 %77 %82
         %84 = OpLoad %14 %83
         %85 = OpAccessChain %15 %77 %115
               OpStore %85 %84
               OpBranch %69
         %69 = OpLabel
         %87 = OpIAdd %14 %115 %19
               OpBranch %66
         %68 = OpLabel
               OpReturn
               OpFunctionEnd
         %12 = OpFunction %2 None %3
         %13 = OpLabel
         %98 = OpVariable %76 Function
               OpBranch %90
         %90 = OpLabel
        %116 = OpPhi %14 %19 %13 %108 %93
               OpLoopMerge %92 %93 None
               OpBranch %94
         %94 = OpLabel
         %97 = OpSLessThan %27 %116 %64
               OpBranchConditional %97 %91 %92
         %91 = OpLabel
        %101 = OpISub %14 %64 %116
        %102 = OpIAdd %14 %101 %19
        %104 = OpAccessChain %15 %98 %116
        %105 = OpLoad %14 %104
        %106 = OpAccessChain %15 %98 %102
               OpStore %106 %105
               OpBranch %93
         %93 = OpLabel
        %108 = OpIAdd %14 %116 %19
               OpBranch %90
         %92 = OpLabel
               OpReturn
               OpFunctionEnd
)";
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << text << std::endl;

  // First two tests can be split into two loops
  // Tests even crossing subscripts from low to high indexes
  // 39 -> 40
  {
    const ir::Function* f = spvtest::GetFunction(module, 6);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    const ir::Instruction* store;
    for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 21)) {
      if (inst.opcode() == SpvOp::SpvOpStore) {
        store = &inst;
      }
    }
    opt::DVEntry dv_entry{};
    EXPECT_FALSE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(39),
                                        store, &dv_entry));
  }

  // Tests even crossing subscripts from high to low indexes
  // 59 -> 60
  {
    const ir::Function* f = spvtest::GetFunction(module, 8);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    const ir::Instruction* store;
    for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 46)) {
      if (inst.opcode() == SpvOp::SpvOpStore) {
        store = &inst;
      }
    }
    opt::DVEntry dv_entry{};
    EXPECT_FALSE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(59),
                                        store, &dv_entry));
  }

  // Next two tests can have an end peeled, then be split
  // Tests uneven crossing subscripts from low to high indexes
  // 84 -> 85
  {
    const ir::Function* f = spvtest::GetFunction(module, 10);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    const ir::Instruction* store;
    for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 67)) {
      if (inst.opcode() == SpvOp::SpvOpStore) {
        store = &inst;
      }
    }
    opt::DVEntry dv_entry{};
    EXPECT_FALSE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(84),
                                        store, &dv_entry));
  }

  // Tests uneven crossing subscripts from high to low indexes
  // 105 -> 106
  {
    const ir::Function* f = spvtest::GetFunction(module, 12);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    const ir::Instruction* store;
    for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 91)) {
      if (inst.opcode() == SpvOp::SpvOpStore) {
        store = &inst;
      }
    }
    opt::DVEntry dv_entry{};
    EXPECT_FALSE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(105),
                                        store, &dv_entry));
  }
}

/*
  Generated from the following GLSL fragment shader
  with --eliminate-local-multi-store
#version 440 core
void main(){
  int[10] arr;
  int[10] arr2;
  int[10] arr3;
  int[10] arr4;
  for (int i = 0; i < 10; i++) {
    arr[0] = arr[i];
    arr2[i] = arr2[0];
    arr3[9] = arr3[i];
    arr4[i] = arr4[9];
  }
}
*/
TEST(DependencyAnalysis, WeakZeroSIV) {
  const std::string text = R"(               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main"
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 440
               OpName %4 "main"
               OpName %23 "arr"
               OpName %28 "arr2"
               OpName %33 "arr3"
               OpName %39 "arr4"
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %6 = OpTypeInt 32 1
          %7 = OpTypePointer Function %6
          %9 = OpConstant %6 0
         %16 = OpConstant %6 10
         %17 = OpTypeBool
         %19 = OpTypeInt 32 0
         %20 = OpConstant %19 10
         %21 = OpTypeArray %6 %20
         %22 = OpTypePointer Function %21
         %34 = OpConstant %6 9
         %45 = OpConstant %6 1
          %4 = OpFunction %2 None %3
          %5 = OpLabel
         %23 = OpVariable %22 Function
         %28 = OpVariable %22 Function
         %33 = OpVariable %22 Function
         %39 = OpVariable %22 Function
               OpBranch %10
         %10 = OpLabel
         %47 = OpPhi %6 %9 %5 %46 %13
               OpLoopMerge %12 %13 None
               OpBranch %14
         %14 = OpLabel
         %18 = OpSLessThan %17 %47 %16
               OpBranchConditional %18 %11 %12
         %11 = OpLabel
         %25 = OpAccessChain %7 %23 %47
         %26 = OpLoad %6 %25
         %27 = OpAccessChain %7 %23 %9
               OpStore %27 %26
         %30 = OpAccessChain %7 %28 %9
         %31 = OpLoad %6 %30
         %32 = OpAccessChain %7 %28 %47
               OpStore %32 %31
         %36 = OpAccessChain %7 %33 %47
         %37 = OpLoad %6 %36
         %38 = OpAccessChain %7 %33 %34
               OpStore %38 %37
         %41 = OpAccessChain %7 %39 %34
         %42 = OpLoad %6 %41
         %43 = OpAccessChain %7 %39 %47
               OpStore %43 %42
               OpBranch %13
         %13 = OpLabel
         %46 = OpIAdd %6 %47 %45
               OpBranch %10
         %12 = OpLabel
               OpReturn
               OpFunctionEnd
)";

  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << text << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 4);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

  const ir::Instruction* store[4];
  int stores_found = 0;
  for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 11)) {
    if (inst.opcode() == SpvOp::SpvOpStore) {
      store[stores_found] = &inst;
      ++stores_found;
    }
  }

  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(store[i]);
  }

  // Tests identifying peel first with weak zero with destination as zero index.
  // 26 -> 27
  {
    opt::DVEntry dv_entry{};
    EXPECT_FALSE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(26),
                                        store[0], &dv_entry));
    EXPECT_TRUE(dv_entry.peel_first);
  }

  // Tests identifying peel first with weak zero with source as zero index.
  // 31 -> 32
  {
    opt::DVEntry dv_entry{};
    EXPECT_FALSE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(31),
                                        store[1], &dv_entry));
    EXPECT_TRUE(dv_entry.peel_first);
  }

  // Tests identifying peel first with weak zero with destination as zero index.
  // 37 -> 38
  {
    opt::DVEntry dv_entry{};
    EXPECT_FALSE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(37),
                                        store[2], &dv_entry));
    EXPECT_TRUE(dv_entry.peel_last);
  }

  // Tests identifying peel first with weak zero with source as zero index.
  // 42 -> 43
  {
    opt::DVEntry dv_entry{};
    EXPECT_FALSE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(42),
                                        store[3], &dv_entry));
    EXPECT_TRUE(dv_entry.peel_last);
  }
}

/*
  Generated from the following GLSL fragment shader
  with --eliminate-local-multi-store
#version 440 core
void main(){
  int[10][10] arr;
  for (int i = 0; i < 10; i++) {
    arr[i][i] = arr[i][i];
    arr[0][i] = arr[1][i];
    arr[1][i] = arr[0][i];
    arr[i][0] = arr[i][1];
    arr[i][1] = arr[i][0];
    arr[0][1] = arr[1][0];
  }
}
*/
TEST(DependencyAnalysis, MultipleSubscript) {
  const std::string text = R"(               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main"
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 440
               OpName %4 "main"
               OpName %8 "i"
               OpName %24 "arr"
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %6 = OpTypeInt 32 1
          %7 = OpTypePointer Function %6
          %9 = OpConstant %6 0
         %16 = OpConstant %6 10
         %17 = OpTypeBool
         %19 = OpTypeInt 32 0
         %20 = OpConstant %19 10
         %21 = OpTypeArray %6 %20
         %22 = OpTypeArray %21 %20
         %23 = OpTypePointer Function %22
         %33 = OpConstant %6 1
          %4 = OpFunction %2 None %3
          %5 = OpLabel
          %8 = OpVariable %7 Function
         %24 = OpVariable %23 Function
               OpStore %8 %9
               OpBranch %10
         %10 = OpLabel
         %58 = OpPhi %6 %9 %5 %57 %13
               OpLoopMerge %12 %13 None
               OpBranch %14
         %14 = OpLabel
         %18 = OpSLessThan %17 %58 %16
               OpBranchConditional %18 %11 %12
         %11 = OpLabel
         %29 = OpAccessChain %7 %24 %58 %58
         %30 = OpLoad %6 %29
         %31 = OpAccessChain %7 %24 %58 %58
               OpStore %31 %30
         %35 = OpAccessChain %7 %24 %33 %58
         %36 = OpLoad %6 %35
         %37 = OpAccessChain %7 %24 %9 %58
               OpStore %37 %36
         %40 = OpAccessChain %7 %24 %9 %58
         %41 = OpLoad %6 %40
         %42 = OpAccessChain %7 %24 %33 %58
               OpStore %42 %41
         %45 = OpAccessChain %7 %24 %58 %33
         %46 = OpLoad %6 %45
         %47 = OpAccessChain %7 %24 %58 %9
               OpStore %47 %46
         %50 = OpAccessChain %7 %24 %58 %9
         %51 = OpLoad %6 %50
         %52 = OpAccessChain %7 %24 %58 %33
               OpStore %52 %51
         %53 = OpAccessChain %7 %24 %33 %9
         %54 = OpLoad %6 %53
         %55 = OpAccessChain %7 %24 %9 %33
               OpStore %55 %54
               OpBranch %13
         %13 = OpLabel
         %57 = OpIAdd %6 %58 %33
               OpStore %8 %57
               OpBranch %10
         %12 = OpLabel
               OpReturn
               OpFunctionEnd
)";

  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << text << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 4);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

  const ir::Instruction* store[6];
  int stores_found = 0;
  for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 11)) {
    if (inst.opcode() == SpvOp::SpvOpStore) {
      store[stores_found] = &inst;
      ++stores_found;
    }
  }

  for (int i = 0; i < 6; ++i) {
    EXPECT_TRUE(store[i]);
  }

  // 30 -> 31
  {
    opt::DVEntry dv_entry{};
    EXPECT_FALSE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(30),
                                        store[0], &dv_entry));
    EXPECT_EQ(dv_entry.direction, opt::DVDirections::EQ);
    EXPECT_EQ(dv_entry.distance, 0);
  }

  // 36 -> 37
  {
    opt::DVEntry dv_entry{};
    EXPECT_TRUE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(36),
                                        store[1], &dv_entry));
    EXPECT_EQ(dv_entry.direction, opt::DVDirections::NONE);
  }

  // 41 -> 42
  {
    opt::DVEntry dv_entry{};
    EXPECT_TRUE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(41),
                                        store[2], &dv_entry));
    EXPECT_EQ(dv_entry.direction, opt::DVDirections::NONE);
  }

  // 46 -> 47
  {
    opt::DVEntry dv_entry{};
    EXPECT_TRUE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(46),
                                        store[3], &dv_entry));
    EXPECT_EQ(dv_entry.direction, opt::DVDirections::NONE);
  }

  // 51 -> 52
  {
    opt::DVEntry dv_entry{};
    EXPECT_TRUE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(51),
                                        store[4], &dv_entry));
    EXPECT_EQ(dv_entry.direction, opt::DVDirections::NONE);
  }

  // 54 -> 55
  {
    opt::DVEntry dv_entry{};
    EXPECT_TRUE(analysis.GetDependence(context->get_def_use_mgr()->GetDef(54),
                                        store[5], &dv_entry));
    EXPECT_EQ(dv_entry.direction, opt::DVDirections::NONE);
  }
}

/*
  Generated from the following GLSL fragment shader
  with --eliminate-local-multi-store
#version 440 core
void main(){
  int[10] arr;
  int[10] arr2;
  int[13] arr3;
  int[13] arr4;
  int a = 2;
  int b = 3;
  for (int i = 0; i < 10; i++) {
    for (int j = 2; j < 10; j++) {
      arr[i] = arr[j];
      arr2[j] = arr2[i];
      arr3[j-2] = arr3[i+3];
      arr3[j-a] = arr3[i+b];
    }
  }
}
*/
TEST(DependencyAnalysis, MIV) {
  const std::string text = R"(               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main"
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 440
               OpName %4 "main"
               OpName %35 "arr"
               OpName %41 "arr2"
               OpName %50 "arr3"
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %6 = OpTypeInt 32 1
          %7 = OpTypePointer Function %6
          %9 = OpConstant %6 2
         %11 = OpConstant %6 3
         %13 = OpConstant %6 0
         %20 = OpConstant %6 10
         %21 = OpTypeBool
         %31 = OpTypeInt 32 0
         %32 = OpConstant %31 10
         %33 = OpTypeArray %6 %32
         %34 = OpTypePointer Function %33
         %47 = OpConstant %31 13
         %48 = OpTypeArray %6 %47
         %49 = OpTypePointer Function %48
         %68 = OpConstant %6 1
         %73 = OpUndef %6
          %4 = OpFunction %2 None %3
          %5 = OpLabel
         %35 = OpVariable %34 Function
         %41 = OpVariable %34 Function
         %50 = OpVariable %49 Function
               OpBranch %14
         %14 = OpLabel
         %72 = OpPhi %6 %13 %5 %71 %17
         %74 = OpPhi %6 %73 %5 %75 %17
               OpLoopMerge %16 %17 None
               OpBranch %18
         %18 = OpLabel
         %22 = OpSLessThan %21 %72 %20
               OpBranchConditional %22 %15 %16
         %15 = OpLabel
               OpBranch %24
         %24 = OpLabel
         %75 = OpPhi %6 %9 %15 %69 %27
               OpLoopMerge %26 %27 None
               OpBranch %28
         %28 = OpLabel
         %30 = OpSLessThan %21 %75 %20
               OpBranchConditional %30 %25 %26
         %25 = OpLabel
         %38 = OpAccessChain %7 %35 %75
         %39 = OpLoad %6 %38
         %40 = OpAccessChain %7 %35 %72
               OpStore %40 %39
         %44 = OpAccessChain %7 %41 %72
         %45 = OpLoad %6 %44
         %46 = OpAccessChain %7 %41 %75
               OpStore %46 %45
         %52 = OpISub %6 %75 %9
         %54 = OpIAdd %6 %72 %11
         %55 = OpAccessChain %7 %50 %54
         %56 = OpLoad %6 %55
         %57 = OpAccessChain %7 %50 %52
               OpStore %57 %56
         %60 = OpISub %6 %75 %9
         %63 = OpIAdd %6 %72 %11
         %64 = OpAccessChain %7 %50 %63
         %65 = OpLoad %6 %64
         %66 = OpAccessChain %7 %50 %60
               OpStore %66 %65
               OpBranch %27
         %27 = OpLabel
         %69 = OpIAdd %6 %75 %68
               OpBranch %24
         %26 = OpLabel
               OpBranch %17
         %17 = OpLabel
         %71 = OpIAdd %6 %72 %68
               OpBranch %14
         %16 = OpLabel
               OpReturn
               OpFunctionEnd
)";
  // TODO(Alexander): Work out subscripts, direction and distance vectors for
  // this
}

/*
  Generated from the following GLSL fragment shader
  with --eliminate-local-multi-store
#version 440 core
void main(){
  // Multiple passes loop form Practical Dependence Testing 5.3.1
  int[10][11][21] arr;
  for (int i = 0; i < 10; i++) {
    for (int j = 1; j < 11; j++) {
      for (int k = 0; k < 10; k++) {
        arr[j-1][i+1][j+k] = arr[j-1][i][j+k];
      }
    }
  }
  // Skewed loop from Practical Dependence Testing 5.3.1
  int[12][13] arr2;
  for (int i = 1; i < 10; i++) {
    for (int j = 1; j < 11; j++) {
      arr2[i][j] = arr2[i-1][j] + arr2[i][j-1] + arr2[i+1][j] + arr2[i][j+1];
    }
  }
}
*/
TEST(DependencyAnalysis, Delta) {
  const std::string text = R"(               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main"
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 440
               OpName %4 "main"
               OpName %45 "arr"
               OpName %89 "arr2"
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %6 = OpTypeInt 32 1
          %7 = OpTypePointer Function %6
          %9 = OpConstant %6 0
         %16 = OpConstant %6 10
         %17 = OpTypeBool
         %20 = OpConstant %6 1
         %27 = OpConstant %6 11
         %37 = OpTypeInt 32 0
         %38 = OpConstant %37 21
         %39 = OpTypeArray %6 %38
         %40 = OpConstant %37 11
         %41 = OpTypeArray %39 %40
         %42 = OpConstant %37 10
         %43 = OpTypeArray %41 %42
         %44 = OpTypePointer Function %43
         %84 = OpConstant %37 13
         %85 = OpTypeArray %6 %84
         %86 = OpConstant %37 12
         %87 = OpTypeArray %85 %86
         %88 = OpTypePointer Function %87
        %121 = OpUndef %6
          %4 = OpFunction %2 None %3
          %5 = OpLabel
         %45 = OpVariable %44 Function
         %89 = OpVariable %88 Function
               OpBranch %10
         %10 = OpLabel
        %120 = OpPhi %6 %9 %5 %67 %13
        %122 = OpPhi %6 %121 %5 %124 %13
        %123 = OpPhi %6 %121 %5 %125 %13
               OpLoopMerge %12 %13 None
               OpBranch %14
         %14 = OpLabel
         %18 = OpSLessThan %17 %120 %16
               OpBranchConditional %18 %11 %12
         %11 = OpLabel
               OpBranch %21
         %21 = OpLabel
        %124 = OpPhi %6 %20 %11 %65 %24
        %125 = OpPhi %6 %123 %11 %126 %24
               OpLoopMerge %23 %24 None
               OpBranch %25
         %25 = OpLabel
         %28 = OpSLessThan %17 %124 %27
               OpBranchConditional %28 %22 %23
         %22 = OpLabel
               OpBranch %30
         %30 = OpLabel
        %126 = OpPhi %6 %9 %22 %63 %33
               OpLoopMerge %32 %33 None
               OpBranch %34
         %34 = OpLabel
         %36 = OpSLessThan %17 %126 %16
               OpBranchConditional %36 %31 %32
         %31 = OpLabel
         %47 = OpISub %6 %124 %20
         %49 = OpIAdd %6 %120 %20
         %52 = OpIAdd %6 %124 %126
         %54 = OpISub %6 %124 %20
         %58 = OpIAdd %6 %124 %126
         %59 = OpAccessChain %7 %45 %54 %120 %58
         %60 = OpLoad %6 %59
         %61 = OpAccessChain %7 %45 %47 %49 %52
               OpStore %61 %60
               OpBranch %33
         %33 = OpLabel
         %63 = OpIAdd %6 %126 %20
               OpBranch %30
         %32 = OpLabel
               OpBranch %24
         %24 = OpLabel
         %65 = OpIAdd %6 %124 %20
               OpBranch %21
         %23 = OpLabel
               OpBranch %13
         %13 = OpLabel
         %67 = OpIAdd %6 %120 %20
               OpBranch %10
         %12 = OpLabel
               OpBranch %69
         %69 = OpLabel
        %127 = OpPhi %6 %20 %12 %119 %72
        %128 = OpPhi %6 %121 %12 %129 %72
               OpLoopMerge %71 %72 None
               OpBranch %73
         %73 = OpLabel
         %75 = OpSLessThan %17 %127 %16
               OpBranchConditional %75 %70 %71
         %70 = OpLabel
               OpBranch %77
         %77 = OpLabel
        %129 = OpPhi %6 %20 %70 %117 %80
               OpLoopMerge %79 %80 None
               OpBranch %81
         %81 = OpLabel
         %83 = OpSLessThan %17 %129 %27
               OpBranchConditional %83 %78 %79
         %78 = OpLabel
         %93 = OpISub %6 %127 %20
         %95 = OpAccessChain %7 %89 %93 %129
         %96 = OpLoad %6 %95
         %99 = OpISub %6 %129 %20
        %100 = OpAccessChain %7 %89 %127 %99
        %101 = OpLoad %6 %100
        %102 = OpIAdd %6 %96 %101
        %104 = OpIAdd %6 %127 %20
        %106 = OpAccessChain %7 %89 %104 %129
        %107 = OpLoad %6 %106
        %108 = OpIAdd %6 %102 %107
        %111 = OpIAdd %6 %129 %20
        %112 = OpAccessChain %7 %89 %127 %111
        %113 = OpLoad %6 %112
        %114 = OpIAdd %6 %108 %113
        %115 = OpAccessChain %7 %89 %127 %129
               OpStore %115 %114
               OpBranch %80
         %80 = OpLabel
        %117 = OpIAdd %6 %129 %20
               OpBranch %77
         %79 = OpLabel
               OpBranch %72
         %72 = OpLabel
        %119 = OpIAdd %6 %127 %20
               OpBranch %69
         %71 = OpLabel
               OpReturn
               OpFunctionEnd
)";

  // 60 -> 61
  // Per subscript
  // Direction vector of (=), (<), (=)
  // Distance vector of (0), (1), (0)
  // Subscripts combined
  // (=, <, =)
  // (0, 1, 0)

  // 114 -> 115
  // Per subscript
  // Direction vector of (<, =), (=, >)
  // Distance vector of (1,0), (0,1)
}

}  // namespace
