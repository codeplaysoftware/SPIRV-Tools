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
#include "opt/scalar_analysis.h"
#include "opt/tree_iterator.h"

namespace {

using namespace spvtools;
using DependencyAnalysisHelpers = ::testing::Test;

/*
  Generated from the following GLSL fragment shader
  with --eliminate-local-multi-store
#version 440 core
void a() {
  for (int i = -10; i < 0; i++) {

  }
}
void b() {
  for (int i = -5; i < 5; i++) {

  }
}
void c() {
  for (int i = 0; i < 10; i++) {

  }
}
void d() {
  for (int i = 5; i < 15; i++) {

  }
}
void e() {
  for (int i = -10; i <= 0; i++) {

  }
}
void f() {
  for (int i = -5; i <= 5; i++) {

  }
}
void g() {
  for (int i = 0; i <= 10; i++) {

  }
}
void h() {
  for (int i = 5; i <= 15; i++) {

  }
}
void i() {
  for (int i = 0; i > -10; i--) {

  }
}
void j() {
  for (int i = 5; i > -5; i--) {

  }
}
void k() {
  for (int i = 10; i > 0; i--) {

  }
}
void l() {
  for (int i = 15; i > 5; i--) {

  }
}
void m() {
  for (int i = 0; i >= -10; i--) {

  }
}
void n() {
  for (int i = 5; i >= -5; i--) {

  }
}
void o() {
  for (int i = 10; i >= 0; i--) {

  }
}
void p() {
  for (int i = 15; i >= 5; i--) {

  }
}
void main(){
  a();
  b();
  c();
  d();
  e();
  f();
  g();
  h();
  i();
  j();
  k();
  l();
  m();
  n();
  o();
  p();
}
*/
TEST(DependencyAnalysisHelpers, loop_information) {
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
               OpName %14 "e("
               OpName %16 "f("
               OpName %18 "g("
               OpName %20 "h("
               OpName %22 "i("
               OpName %24 "j("
               OpName %26 "k("
               OpName %28 "l("
               OpName %30 "m("
               OpName %32 "n("
               OpName %34 "o("
               OpName %36 "p("
               OpName %40 "i"
               OpName %54 "i"
               OpName %66 "i"
               OpName %77 "i"
               OpName %88 "i"
               OpName %98 "i"
               OpName %108 "i"
               OpName %118 "i"
               OpName %128 "i"
               OpName %138 "i"
               OpName %148 "i"
               OpName %158 "i"
               OpName %168 "i"
               OpName %178 "i"
               OpName %188 "i"
               OpName %198 "i"
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
         %38 = OpTypeInt 32 1
         %39 = OpTypePointer Function %38
         %41 = OpConstant %38 -10
         %48 = OpConstant %38 0
         %49 = OpTypeBool
         %52 = OpConstant %38 1
         %55 = OpConstant %38 -5
         %62 = OpConstant %38 5
         %73 = OpConstant %38 10
         %84 = OpConstant %38 15
          %4 = OpFunction %2 None %3
          %5 = OpLabel
        %208 = OpFunctionCall %2 %6
        %209 = OpFunctionCall %2 %8
        %210 = OpFunctionCall %2 %10
        %211 = OpFunctionCall %2 %12
        %212 = OpFunctionCall %2 %14
        %213 = OpFunctionCall %2 %16
        %214 = OpFunctionCall %2 %18
        %215 = OpFunctionCall %2 %20
        %216 = OpFunctionCall %2 %22
        %217 = OpFunctionCall %2 %24
        %218 = OpFunctionCall %2 %26
        %219 = OpFunctionCall %2 %28
        %220 = OpFunctionCall %2 %30
        %221 = OpFunctionCall %2 %32
        %222 = OpFunctionCall %2 %34
        %223 = OpFunctionCall %2 %36
               OpReturn
               OpFunctionEnd
          %6 = OpFunction %2 None %3
          %7 = OpLabel
         %40 = OpVariable %39 Function
               OpStore %40 %41
               OpBranch %42
         %42 = OpLabel
        %224 = OpPhi %38 %41 %7 %53 %45
               OpLoopMerge %44 %45 None
               OpBranch %46
         %46 = OpLabel
         %50 = OpSLessThan %49 %224 %48
               OpBranchConditional %50 %43 %44
         %43 = OpLabel
               OpBranch %45
         %45 = OpLabel
         %53 = OpIAdd %38 %224 %52
               OpStore %40 %53
               OpBranch %42
         %44 = OpLabel
               OpReturn
               OpFunctionEnd
          %8 = OpFunction %2 None %3
          %9 = OpLabel
         %54 = OpVariable %39 Function
               OpStore %54 %55
               OpBranch %56
         %56 = OpLabel
        %225 = OpPhi %38 %55 %9 %65 %59
               OpLoopMerge %58 %59 None
               OpBranch %60
         %60 = OpLabel
         %63 = OpSLessThan %49 %225 %62
               OpBranchConditional %63 %57 %58
         %57 = OpLabel
               OpBranch %59
         %59 = OpLabel
         %65 = OpIAdd %38 %225 %52
               OpStore %54 %65
               OpBranch %56
         %58 = OpLabel
               OpReturn
               OpFunctionEnd
         %10 = OpFunction %2 None %3
         %11 = OpLabel
         %66 = OpVariable %39 Function
               OpStore %66 %48
               OpBranch %67
         %67 = OpLabel
        %226 = OpPhi %38 %48 %11 %76 %70
               OpLoopMerge %69 %70 None
               OpBranch %71
         %71 = OpLabel
         %74 = OpSLessThan %49 %226 %73
               OpBranchConditional %74 %68 %69
         %68 = OpLabel
               OpBranch %70
         %70 = OpLabel
         %76 = OpIAdd %38 %226 %52
               OpStore %66 %76
               OpBranch %67
         %69 = OpLabel
               OpReturn
               OpFunctionEnd
         %12 = OpFunction %2 None %3
         %13 = OpLabel
         %77 = OpVariable %39 Function
               OpStore %77 %62
               OpBranch %78
         %78 = OpLabel
        %227 = OpPhi %38 %62 %13 %87 %81
               OpLoopMerge %80 %81 None
               OpBranch %82
         %82 = OpLabel
         %85 = OpSLessThan %49 %227 %84
               OpBranchConditional %85 %79 %80
         %79 = OpLabel
               OpBranch %81
         %81 = OpLabel
         %87 = OpIAdd %38 %227 %52
               OpStore %77 %87
               OpBranch %78
         %80 = OpLabel
               OpReturn
               OpFunctionEnd
         %14 = OpFunction %2 None %3
         %15 = OpLabel
         %88 = OpVariable %39 Function
               OpStore %88 %41
               OpBranch %89
         %89 = OpLabel
        %228 = OpPhi %38 %41 %15 %97 %92
               OpLoopMerge %91 %92 None
               OpBranch %93
         %93 = OpLabel
         %95 = OpSLessThanEqual %49 %228 %48
               OpBranchConditional %95 %90 %91
         %90 = OpLabel
               OpBranch %92
         %92 = OpLabel
         %97 = OpIAdd %38 %228 %52
               OpStore %88 %97
               OpBranch %89
         %91 = OpLabel
               OpReturn
               OpFunctionEnd
         %16 = OpFunction %2 None %3
         %17 = OpLabel
         %98 = OpVariable %39 Function
               OpStore %98 %55
               OpBranch %99
         %99 = OpLabel
        %229 = OpPhi %38 %55 %17 %107 %102
               OpLoopMerge %101 %102 None
               OpBranch %103
        %103 = OpLabel
        %105 = OpSLessThanEqual %49 %229 %62
               OpBranchConditional %105 %100 %101
        %100 = OpLabel
               OpBranch %102
        %102 = OpLabel
        %107 = OpIAdd %38 %229 %52
               OpStore %98 %107
               OpBranch %99
        %101 = OpLabel
               OpReturn
               OpFunctionEnd
         %18 = OpFunction %2 None %3
         %19 = OpLabel
        %108 = OpVariable %39 Function
               OpStore %108 %48
               OpBranch %109
        %109 = OpLabel
        %230 = OpPhi %38 %48 %19 %117 %112
               OpLoopMerge %111 %112 None
               OpBranch %113
        %113 = OpLabel
        %115 = OpSLessThanEqual %49 %230 %73
               OpBranchConditional %115 %110 %111
        %110 = OpLabel
               OpBranch %112
        %112 = OpLabel
        %117 = OpIAdd %38 %230 %52
               OpStore %108 %117
               OpBranch %109
        %111 = OpLabel
               OpReturn
               OpFunctionEnd
         %20 = OpFunction %2 None %3
         %21 = OpLabel
        %118 = OpVariable %39 Function
               OpStore %118 %62
               OpBranch %119
        %119 = OpLabel
        %231 = OpPhi %38 %62 %21 %127 %122
               OpLoopMerge %121 %122 None
               OpBranch %123
        %123 = OpLabel
        %125 = OpSLessThanEqual %49 %231 %84
               OpBranchConditional %125 %120 %121
        %120 = OpLabel
               OpBranch %122
        %122 = OpLabel
        %127 = OpIAdd %38 %231 %52
               OpStore %118 %127
               OpBranch %119
        %121 = OpLabel
               OpReturn
               OpFunctionEnd
         %22 = OpFunction %2 None %3
         %23 = OpLabel
        %128 = OpVariable %39 Function
               OpStore %128 %48
               OpBranch %129
        %129 = OpLabel
        %232 = OpPhi %38 %48 %23 %137 %132
               OpLoopMerge %131 %132 None
               OpBranch %133
        %133 = OpLabel
        %135 = OpSGreaterThan %49 %232 %41
               OpBranchConditional %135 %130 %131
        %130 = OpLabel
               OpBranch %132
        %132 = OpLabel
        %137 = OpISub %38 %232 %52
               OpStore %128 %137
               OpBranch %129
        %131 = OpLabel
               OpReturn
               OpFunctionEnd
         %24 = OpFunction %2 None %3
         %25 = OpLabel
        %138 = OpVariable %39 Function
               OpStore %138 %62
               OpBranch %139
        %139 = OpLabel
        %233 = OpPhi %38 %62 %25 %147 %142
               OpLoopMerge %141 %142 None
               OpBranch %143
        %143 = OpLabel
        %145 = OpSGreaterThan %49 %233 %55
               OpBranchConditional %145 %140 %141
        %140 = OpLabel
               OpBranch %142
        %142 = OpLabel
        %147 = OpISub %38 %233 %52
               OpStore %138 %147
               OpBranch %139
        %141 = OpLabel
               OpReturn
               OpFunctionEnd
         %26 = OpFunction %2 None %3
         %27 = OpLabel
        %148 = OpVariable %39 Function
               OpStore %148 %73
               OpBranch %149
        %149 = OpLabel
        %234 = OpPhi %38 %73 %27 %157 %152
               OpLoopMerge %151 %152 None
               OpBranch %153
        %153 = OpLabel
        %155 = OpSGreaterThan %49 %234 %48
               OpBranchConditional %155 %150 %151
        %150 = OpLabel
               OpBranch %152
        %152 = OpLabel
        %157 = OpISub %38 %234 %52
               OpStore %148 %157
               OpBranch %149
        %151 = OpLabel
               OpReturn
               OpFunctionEnd
         %28 = OpFunction %2 None %3
         %29 = OpLabel
        %158 = OpVariable %39 Function
               OpStore %158 %84
               OpBranch %159
        %159 = OpLabel
        %235 = OpPhi %38 %84 %29 %167 %162
               OpLoopMerge %161 %162 None
               OpBranch %163
        %163 = OpLabel
        %165 = OpSGreaterThan %49 %235 %62
               OpBranchConditional %165 %160 %161
        %160 = OpLabel
               OpBranch %162
        %162 = OpLabel
        %167 = OpISub %38 %235 %52
               OpStore %158 %167
               OpBranch %159
        %161 = OpLabel
               OpReturn
               OpFunctionEnd
         %30 = OpFunction %2 None %3
         %31 = OpLabel
        %168 = OpVariable %39 Function
               OpStore %168 %48
               OpBranch %169
        %169 = OpLabel
        %236 = OpPhi %38 %48 %31 %177 %172
               OpLoopMerge %171 %172 None
               OpBranch %173
        %173 = OpLabel
        %175 = OpSGreaterThanEqual %49 %236 %41
               OpBranchConditional %175 %170 %171
        %170 = OpLabel
               OpBranch %172
        %172 = OpLabel
        %177 = OpISub %38 %236 %52
               OpStore %168 %177
               OpBranch %169
        %171 = OpLabel
               OpReturn
               OpFunctionEnd
         %32 = OpFunction %2 None %3
         %33 = OpLabel
        %178 = OpVariable %39 Function
               OpStore %178 %62
               OpBranch %179
        %179 = OpLabel
        %237 = OpPhi %38 %62 %33 %187 %182
               OpLoopMerge %181 %182 None
               OpBranch %183
        %183 = OpLabel
        %185 = OpSGreaterThanEqual %49 %237 %55
               OpBranchConditional %185 %180 %181
        %180 = OpLabel
               OpBranch %182
        %182 = OpLabel
        %187 = OpISub %38 %237 %52
               OpStore %178 %187
               OpBranch %179
        %181 = OpLabel
               OpReturn
               OpFunctionEnd
         %34 = OpFunction %2 None %3
         %35 = OpLabel
        %188 = OpVariable %39 Function
               OpStore %188 %73
               OpBranch %189
        %189 = OpLabel
        %238 = OpPhi %38 %73 %35 %197 %192
               OpLoopMerge %191 %192 None
               OpBranch %193
        %193 = OpLabel
        %195 = OpSGreaterThanEqual %49 %238 %48
               OpBranchConditional %195 %190 %191
        %190 = OpLabel
               OpBranch %192
        %192 = OpLabel
        %197 = OpISub %38 %238 %52
               OpStore %188 %197
               OpBranch %189
        %191 = OpLabel
               OpReturn
               OpFunctionEnd
         %36 = OpFunction %2 None %3
         %37 = OpLabel
        %198 = OpVariable %39 Function
               OpStore %198 %84
               OpBranch %199
        %199 = OpLabel
        %239 = OpPhi %38 %84 %37 %207 %202
               OpLoopMerge %201 %202 None
               OpBranch %203
        %203 = OpLabel
        %205 = OpSGreaterThanEqual %49 %239 %62
               OpBranchConditional %205 %200 %201
        %200 = OpLabel
               OpBranch %202
        %202 = OpLabel
        %207 = OpISub %38 %239 %52
               OpStore %198 %207
               OpBranch %199
        %201 = OpLabel
               OpReturn
               OpFunctionEnd
)";
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << text << std::endl;
  {
    // Function a
    const ir::Function* f = spvtest::GetFunction(module, 6);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    EXPECT_EQ(analysis.GetLowerBound()->AsSEConstantNode()->FoldToSingleValue(),
              -10);
    EXPECT_EQ(analysis.GetUpperBound()->AsSEConstantNode()->FoldToSingleValue(),
              -1);

    EXPECT_EQ(analysis.GetTripCount()->AsSEConstantNode()->FoldToSingleValue(),
              10);

    EXPECT_EQ(analysis.GetFirstTripInductionNode(),
              analysis.GetScalarEvolution()->CreateConstant(-10));

    EXPECT_EQ(analysis.GetFinalTripInductionNode(
                  analysis.GetScalarEvolution()->CreateConstant(1)),
              analysis.GetScalarEvolution()->CreateConstant(-1));
  }
  {
    // Function b
    const ir::Function* f = spvtest::GetFunction(module, 8);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    EXPECT_EQ(analysis.GetLowerBound()->AsSEConstantNode()->FoldToSingleValue(),
              -5);
    EXPECT_EQ(analysis.GetUpperBound()->AsSEConstantNode()->FoldToSingleValue(),
              4);

    EXPECT_EQ(analysis.GetTripCount()->AsSEConstantNode()->FoldToSingleValue(),
              10);

    EXPECT_EQ(analysis.GetFirstTripInductionNode(),
              analysis.GetScalarEvolution()->CreateConstant(-5));

    EXPECT_EQ(analysis.GetFinalTripInductionNode(
                  analysis.GetScalarEvolution()->CreateConstant(1)),
              analysis.GetScalarEvolution()->CreateConstant(4));
  }
  {
    // Function c
    const ir::Function* f = spvtest::GetFunction(module, 10);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    EXPECT_EQ(analysis.GetLowerBound()->AsSEConstantNode()->FoldToSingleValue(),
              0);
    EXPECT_EQ(analysis.GetUpperBound()->AsSEConstantNode()->FoldToSingleValue(),
              9);

    EXPECT_EQ(analysis.GetTripCount()->AsSEConstantNode()->FoldToSingleValue(),
              10);

    EXPECT_EQ(analysis.GetFirstTripInductionNode(),
              analysis.GetScalarEvolution()->CreateConstant(0));

    EXPECT_EQ(analysis.GetFinalTripInductionNode(
                  analysis.GetScalarEvolution()->CreateConstant(1)),
              analysis.GetScalarEvolution()->CreateConstant(9));
  }
  {
    // Function d
    const ir::Function* f = spvtest::GetFunction(module, 12);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    EXPECT_EQ(analysis.GetLowerBound()->AsSEConstantNode()->FoldToSingleValue(),
              5);
    EXPECT_EQ(analysis.GetUpperBound()->AsSEConstantNode()->FoldToSingleValue(),
              14);

    EXPECT_EQ(analysis.GetTripCount()->AsSEConstantNode()->FoldToSingleValue(),
              10);

    EXPECT_EQ(analysis.GetFirstTripInductionNode(),
              analysis.GetScalarEvolution()->CreateConstant(5));

    EXPECT_EQ(analysis.GetFinalTripInductionNode(
                  analysis.GetScalarEvolution()->CreateConstant(1)),
              analysis.GetScalarEvolution()->CreateConstant(14));
  }
  {
    // Function e
    const ir::Function* f = spvtest::GetFunction(module, 14);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    EXPECT_EQ(analysis.GetLowerBound()->AsSEConstantNode()->FoldToSingleValue(),
              -10);
    EXPECT_EQ(analysis.GetUpperBound()->AsSEConstantNode()->FoldToSingleValue(),
              0);

    EXPECT_EQ(analysis.GetTripCount()->AsSEConstantNode()->FoldToSingleValue(),
              11);

    EXPECT_EQ(analysis.GetFirstTripInductionNode(),
              analysis.GetScalarEvolution()->CreateConstant(-10));

    EXPECT_EQ(analysis.GetFinalTripInductionNode(
                  analysis.GetScalarEvolution()->CreateConstant(1)),
              analysis.GetScalarEvolution()->CreateConstant(0));
  }
  {
    // Function f
    const ir::Function* f = spvtest::GetFunction(module, 16);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    EXPECT_EQ(analysis.GetLowerBound()->AsSEConstantNode()->FoldToSingleValue(),
              -5);
    EXPECT_EQ(analysis.GetUpperBound()->AsSEConstantNode()->FoldToSingleValue(),
              5);

    EXPECT_EQ(analysis.GetTripCount()->AsSEConstantNode()->FoldToSingleValue(),
              11);

    EXPECT_EQ(analysis.GetFirstTripInductionNode(),
              analysis.GetScalarEvolution()->CreateConstant(-5));

    EXPECT_EQ(analysis.GetFinalTripInductionNode(
                  analysis.GetScalarEvolution()->CreateConstant(1)),
              analysis.GetScalarEvolution()->CreateConstant(5));
  }
  {
    // Function g
    const ir::Function* f = spvtest::GetFunction(module, 18);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    EXPECT_EQ(analysis.GetLowerBound()->AsSEConstantNode()->FoldToSingleValue(),
              0);
    EXPECT_EQ(analysis.GetUpperBound()->AsSEConstantNode()->FoldToSingleValue(),
              10);

    EXPECT_EQ(analysis.GetTripCount()->AsSEConstantNode()->FoldToSingleValue(),
              11);

    EXPECT_EQ(analysis.GetFirstTripInductionNode(),
              analysis.GetScalarEvolution()->CreateConstant(0));

    EXPECT_EQ(analysis.GetFinalTripInductionNode(
                  analysis.GetScalarEvolution()->CreateConstant(1)),
              analysis.GetScalarEvolution()->CreateConstant(10));
  }
  {
    // Function h
    const ir::Function* f = spvtest::GetFunction(module, 20);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    EXPECT_EQ(analysis.GetLowerBound()->AsSEConstantNode()->FoldToSingleValue(),
              5);
    EXPECT_EQ(analysis.GetUpperBound()->AsSEConstantNode()->FoldToSingleValue(),
              15);

    EXPECT_EQ(analysis.GetTripCount()->AsSEConstantNode()->FoldToSingleValue(),
              11);

    EXPECT_EQ(analysis.GetFirstTripInductionNode(),
              analysis.GetScalarEvolution()->CreateConstant(5));

    EXPECT_EQ(analysis.GetFinalTripInductionNode(
                  analysis.GetScalarEvolution()->CreateConstant(1)),
              analysis.GetScalarEvolution()->CreateConstant(15));
  }
  {
    // Function i
    const ir::Function* f = spvtest::GetFunction(module, 22);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    EXPECT_EQ(analysis.GetLowerBound()->AsSEConstantNode()->FoldToSingleValue(),
              0);
    EXPECT_EQ(analysis.GetUpperBound()->AsSEConstantNode()->FoldToSingleValue(),
              -9);

    EXPECT_EQ(analysis.GetTripCount()->AsSEConstantNode()->FoldToSingleValue(),
              10);

    EXPECT_EQ(analysis.GetFirstTripInductionNode(),
              analysis.GetScalarEvolution()->CreateConstant(0));

    EXPECT_EQ(analysis.GetFinalTripInductionNode(
                  analysis.GetScalarEvolution()->CreateConstant(-1)),
              analysis.GetScalarEvolution()->CreateConstant(-9));
  }
  {
    // Function j
    const ir::Function* f = spvtest::GetFunction(module, 24);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    EXPECT_EQ(analysis.GetLowerBound()->AsSEConstantNode()->FoldToSingleValue(),
              5);
    EXPECT_EQ(analysis.GetUpperBound()->AsSEConstantNode()->FoldToSingleValue(),
              -4);

    EXPECT_EQ(analysis.GetTripCount()->AsSEConstantNode()->FoldToSingleValue(),
              10);

    EXPECT_EQ(analysis.GetFirstTripInductionNode(),
              analysis.GetScalarEvolution()->CreateConstant(5));

    EXPECT_EQ(analysis.GetFinalTripInductionNode(
                  analysis.GetScalarEvolution()->CreateConstant(-1)),
              analysis.GetScalarEvolution()->CreateConstant(-4));
  }
  {
    // Function k
    const ir::Function* f = spvtest::GetFunction(module, 26);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    EXPECT_EQ(analysis.GetLowerBound()->AsSEConstantNode()->FoldToSingleValue(),
              10);
    EXPECT_EQ(analysis.GetUpperBound()->AsSEConstantNode()->FoldToSingleValue(),
              1);

    EXPECT_EQ(analysis.GetTripCount()->AsSEConstantNode()->FoldToSingleValue(),
              10);

    EXPECT_EQ(analysis.GetFirstTripInductionNode(),
              analysis.GetScalarEvolution()->CreateConstant(10));

    EXPECT_EQ(analysis.GetFinalTripInductionNode(
                  analysis.GetScalarEvolution()->CreateConstant(-1)),
              analysis.GetScalarEvolution()->CreateConstant(1));
  }
  {
    // Function l
    const ir::Function* f = spvtest::GetFunction(module, 28);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    EXPECT_EQ(analysis.GetLowerBound()->AsSEConstantNode()->FoldToSingleValue(),
              15);
    EXPECT_EQ(analysis.GetUpperBound()->AsSEConstantNode()->FoldToSingleValue(),
              6);

    EXPECT_EQ(analysis.GetTripCount()->AsSEConstantNode()->FoldToSingleValue(),
              10);

    EXPECT_EQ(analysis.GetFirstTripInductionNode(),
              analysis.GetScalarEvolution()->CreateConstant(15));

    EXPECT_EQ(analysis.GetFinalTripInductionNode(
                  analysis.GetScalarEvolution()->CreateConstant(-1)),
              analysis.GetScalarEvolution()->CreateConstant(6));
  }
  {
    // Function m
    const ir::Function* f = spvtest::GetFunction(module, 30);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    EXPECT_EQ(analysis.GetLowerBound()->AsSEConstantNode()->FoldToSingleValue(),
              0);
    EXPECT_EQ(analysis.GetUpperBound()->AsSEConstantNode()->FoldToSingleValue(),
              -10);

    EXPECT_EQ(analysis.GetTripCount()->AsSEConstantNode()->FoldToSingleValue(),
              11);

    EXPECT_EQ(analysis.GetFirstTripInductionNode(),
              analysis.GetScalarEvolution()->CreateConstant(0));

    EXPECT_EQ(analysis.GetFinalTripInductionNode(
                  analysis.GetScalarEvolution()->CreateConstant(-1)),
              analysis.GetScalarEvolution()->CreateConstant(-10));
  }
  {
    // Function n
    const ir::Function* f = spvtest::GetFunction(module, 32);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    EXPECT_EQ(analysis.GetLowerBound()->AsSEConstantNode()->FoldToSingleValue(),
              5);
    EXPECT_EQ(analysis.GetUpperBound()->AsSEConstantNode()->FoldToSingleValue(),
              -5);

    EXPECT_EQ(analysis.GetTripCount()->AsSEConstantNode()->FoldToSingleValue(),
              11);

    EXPECT_EQ(analysis.GetFirstTripInductionNode(),
              analysis.GetScalarEvolution()->CreateConstant(5));

    EXPECT_EQ(analysis.GetFinalTripInductionNode(
                  analysis.GetScalarEvolution()->CreateConstant(-1)),
              analysis.GetScalarEvolution()->CreateConstant(-5));
  }
  {
    // Function o
    const ir::Function* f = spvtest::GetFunction(module, 34);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    EXPECT_EQ(analysis.GetLowerBound()->AsSEConstantNode()->FoldToSingleValue(),
              10);
    EXPECT_EQ(analysis.GetUpperBound()->AsSEConstantNode()->FoldToSingleValue(),
              0);

    EXPECT_EQ(analysis.GetTripCount()->AsSEConstantNode()->FoldToSingleValue(),
              11);

    EXPECT_EQ(analysis.GetFirstTripInductionNode(),
              analysis.GetScalarEvolution()->CreateConstant(10));

    EXPECT_EQ(analysis.GetFinalTripInductionNode(
                  analysis.GetScalarEvolution()->CreateConstant(-1)),
              analysis.GetScalarEvolution()->CreateConstant(0));
  }
  {
    // Function p
    const ir::Function* f = spvtest::GetFunction(module, 36);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    EXPECT_EQ(analysis.GetLowerBound()->AsSEConstantNode()->FoldToSingleValue(),
              15);
    EXPECT_EQ(analysis.GetUpperBound()->AsSEConstantNode()->FoldToSingleValue(),
              5);

    EXPECT_EQ(analysis.GetTripCount()->AsSEConstantNode()->FoldToSingleValue(),
              11);

    EXPECT_EQ(analysis.GetFirstTripInductionNode(),
              analysis.GetScalarEvolution()->CreateConstant(15));

    EXPECT_EQ(analysis.GetFinalTripInductionNode(
                  analysis.GetScalarEvolution()->CreateConstant(-1)),
              analysis.GetScalarEvolution()->CreateConstant(5));
  }
}

/*
  Generated from the following GLSL fragment shader
  with --eliminate-local-multi-store
#version 440 core
void main(){
  for (int i = 0; i < 10; i++) {

  }
}
*/
TEST(DependencyAnalysisHelpers, bounds_checks) {
  const std::string text = R"(               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main"
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 440
               OpName %4 "main"
               OpName %8 "i"
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %6 = OpTypeInt 32 1
          %7 = OpTypePointer Function %6
          %9 = OpConstant %6 0
         %16 = OpConstant %6 10
         %17 = OpTypeBool
         %20 = OpConstant %6 1
          %4 = OpFunction %2 None %3
          %5 = OpLabel
          %8 = OpVariable %7 Function
               OpStore %8 %9
               OpBranch %10
         %10 = OpLabel
         %22 = OpPhi %6 %9 %5 %21 %13
               OpLoopMerge %12 %13 None
               OpBranch %14
         %14 = OpLabel
         %18 = OpSLessThan %17 %22 %16
               OpBranchConditional %18 %11 %12
         %11 = OpLabel
               OpBranch %13
         %13 = OpLabel
         %21 = OpIAdd %6 %22 %20
               OpStore %8 %21
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
  // We need a shader that includes a loop for this test so we can build a
  // LoopDependenceAnalaysis
  const ir::Function* f = spvtest::GetFunction(module, 4);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
  opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

  EXPECT_TRUE(analysis.IsWithinBounds(0, 0, 0));
  EXPECT_TRUE(analysis.IsWithinBounds(0, -1, 0));
  EXPECT_TRUE(analysis.IsWithinBounds(0, 0, 1));
  EXPECT_TRUE(analysis.IsWithinBounds(0, -1, 1));
  EXPECT_TRUE(analysis.IsWithinBounds(-2, -2, -2));
  EXPECT_TRUE(analysis.IsWithinBounds(-2, -3, 0));
  EXPECT_TRUE(analysis.IsWithinBounds(-2, 0, -3));
  EXPECT_TRUE(analysis.IsWithinBounds(2, 2, 2));
  EXPECT_TRUE(analysis.IsWithinBounds(2, 3, 0));

  EXPECT_FALSE(analysis.IsWithinBounds(2, 3, 3));
  EXPECT_FALSE(analysis.IsWithinBounds(0, 1, 5));
  EXPECT_FALSE(analysis.IsWithinBounds(0, -1, -4));
  EXPECT_FALSE(analysis.IsWithinBounds(-2, -4, -3));
}

/*
  Generated from the following GLSL fragment shader
  with --eliminate-local-multi-store
#version 440 core
layout(location = 0) in vec4 in_vec;
// Loop iterates from constant to symbolic
void a() {
  int N = int(in_vec.x);
  int arr[10];
  for (int i = 0; i < N; i++) { // Bounds are N - 0 - 1
    arr[i] = arr[i+N]; // |distance| = N
    arr[i+N] = arr[i]; // |distance| = N
  }
}
void b() {
  int N = int(in_vec.x);
  int arr[10];
  for (int i = 0; i <= N; i++) { // Bounds are N - 0
    arr[i] = arr[i+N]; // |distance| = N
    arr[i+N] = arr[i]; // |distance| = N
  }
}
void c() {
  int N = int(in_vec.x);
  int arr[10];
  for (int i = 9; i > N; i--) { // Bounds are 9 - N - 1
    arr[i] = arr[i+N]; // |distance| = N
    arr[i+N] = arr[i]; // |distance| = N
  }
}
void d() {
  int N = int(in_vec.x);
  int arr[10];
  for (int i = 9; i >= N; i--) { // Bounds are 9 - N
    arr[i] = arr[i+N]; // |distance| = N
    arr[i+N] = arr[i]; // |distance| = N
  }
}
// Loop iterates from symbolic to constant
void e() {
  int N = int(in_vec.x);
  int arr[10];
  for (int i = N; i < 9; i++) { // Bounds are 9 - N - 1
    arr[i] = arr[i+N]; // |distance| = N
    arr[i+N] = arr[i]; // |distance| = N
  }
}
void f() {
  int N = int(in_vec.x);
  int arr[10];
  for (int i = N; i <= 9; i++) { // Bounds are 9 - N
    arr[i] = arr[i+N]; // |distance| = N
    arr[i+N] = arr[i]; // |distance| = N
  }
}
void g() {
  int N = int(in_vec.x);
  int arr[10];
  for (int i = N; i > 0; i--) { // Bounds are N - 0 - 1
    arr[i] = arr[i+N]; // |distance| = N
    arr[i+N] = arr[i]; // |distance| = N
  }
}
void h() {
  int N = int(in_vec.x);
  int arr[10];
  for (int i = N; i >= 0; i--) { // Bounds are N - 0
    arr[i] = arr[i+N]; // |distance| = N
    arr[i+N] = arr[i]; // |distance| = N
  }
}
// Loop iterates from symbolic to symbolic
void i() {
  int M = int(in_vec.x);
  int N = int(in_vec.y);
  int arr[10];
  for (int i = M; i < N; i++) { // Bounds are N - M - 1
    arr[i+M+N] = arr[i+M+2*N]; // |distance| = N
    arr[i+M+2*N] = arr[i+M+N]; // |distance| = N
  }
}
void j() {
  int M = int(in_vec.x);
  int N = int(in_vec.y);
  int arr[10];
  for (int i = M; i <= N; i++) { // Bounds are N - M
    arr[i+M+N] = arr[i+M+2*N]; // |distance| = N
    arr[i+M+2*N] = arr[i+M+N]; // |distance| = N
  }
}
void k() {
  int M = int(in_vec.x);
  int N = int(in_vec.y);
  int arr[10];
  for (int i = M; i > N; i--) { // Bounds are M - N - 1
    arr[i+M+N] = arr[i+M+2*N]; // |distance| = N
    arr[i+M+2*N] = arr[i+M+N]; // |distance| = N
  }
}
void l() {
  int M = int(in_vec.x);
  int N = int(in_vec.y);
  int arr[10];
  for (int i = M; i >= N; i--) { // Bounds are M - N
    arr[i+M+N] = arr[i+M+2*N]; // |distance| = N
    arr[i+M+2*N] = arr[i+M+N]; // |distance| = N
  }
}
void main(){
  a();
  b();
  c();
  d();
  e();
  f();
  g();
  h();
  i();
  j();
  k();
  l();
}
*/
TEST(DependencyAnalysisHelpers, symbolic_checks) {
  const std::string text = R"(               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main" %36
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 440
               OpName %4 "main"
               OpName %6 "a("
               OpName %8 "b("
               OpName %10 "c("
               OpName %12 "d("
               OpName %14 "e("
               OpName %16 "f("
               OpName %18 "g("
               OpName %20 "h("
               OpName %22 "i("
               OpName %24 "j("
               OpName %26 "k("
               OpName %28 "l("
               OpName %32 "N"
               OpName %36 "in_vec"
               OpName %43 "i"
               OpName %57 "arr"
               OpName %75 "N"
               OpName %79 "i"
               OpName %88 "arr"
               OpName %105 "N"
               OpName %109 "i"
               OpName %119 "arr"
               OpName %136 "N"
               OpName %140 "i"
               OpName %149 "arr"
               OpName %166 "N"
               OpName %170 "i"
               OpName %179 "arr"
               OpName %196 "N"
               OpName %200 "i"
               OpName %209 "arr"
               OpName %226 "N"
               OpName %230 "i"
               OpName %239 "arr"
               OpName %256 "N"
               OpName %260 "i"
               OpName %269 "arr"
               OpName %286 "M"
               OpName %290 "N"
               OpName %295 "i"
               OpName %305 "arr"
               OpName %337 "M"
               OpName %341 "N"
               OpName %345 "i"
               OpName %355 "arr"
               OpName %386 "M"
               OpName %390 "N"
               OpName %394 "i"
               OpName %404 "arr"
               OpName %435 "M"
               OpName %439 "N"
               OpName %443 "i"
               OpName %453 "arr"
               OpDecorate %36 Location 0
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
         %30 = OpTypeInt 32 1
         %31 = OpTypePointer Function %30
         %33 = OpTypeFloat 32
         %34 = OpTypeVector %33 4
         %35 = OpTypePointer Input %34
         %36 = OpVariable %35 Input
         %37 = OpTypeInt 32 0
         %38 = OpConstant %37 0
         %39 = OpTypePointer Input %33
         %44 = OpConstant %30 0
         %52 = OpTypeBool
         %54 = OpConstant %37 10
         %55 = OpTypeArray %30 %54
         %56 = OpTypePointer Function %55
         %73 = OpConstant %30 1
        %110 = OpConstant %30 9
        %291 = OpConstant %37 1
        %314 = OpConstant %30 2
          %4 = OpFunction %2 None %3
          %5 = OpLabel
        %484 = OpFunctionCall %2 %6
        %485 = OpFunctionCall %2 %8
        %486 = OpFunctionCall %2 %10
        %487 = OpFunctionCall %2 %12
        %488 = OpFunctionCall %2 %14
        %489 = OpFunctionCall %2 %16
        %490 = OpFunctionCall %2 %18
        %491 = OpFunctionCall %2 %20
        %492 = OpFunctionCall %2 %22
        %493 = OpFunctionCall %2 %24
        %494 = OpFunctionCall %2 %26
        %495 = OpFunctionCall %2 %28
               OpReturn
               OpFunctionEnd
          %6 = OpFunction %2 None %3
          %7 = OpLabel
         %32 = OpVariable %31 Function
         %43 = OpVariable %31 Function
         %57 = OpVariable %56 Function
         %40 = OpAccessChain %39 %36 %38
         %41 = OpLoad %33 %40
         %42 = OpConvertFToS %30 %41
               OpStore %32 %42
               OpStore %43 %44
               OpBranch %45
         %45 = OpLabel
        %496 = OpPhi %30 %44 %7 %74 %48
               OpLoopMerge %47 %48 None
               OpBranch %49
         %49 = OpLabel
         %53 = OpSLessThan %52 %496 %42
               OpBranchConditional %53 %46 %47
         %46 = OpLabel
         %61 = OpIAdd %30 %496 %42
         %62 = OpAccessChain %31 %57 %61
         %63 = OpLoad %30 %62
         %64 = OpAccessChain %31 %57 %496
               OpStore %64 %63
         %67 = OpIAdd %30 %496 %42
         %69 = OpAccessChain %31 %57 %496
         %70 = OpLoad %30 %69
         %71 = OpAccessChain %31 %57 %67
               OpStore %71 %70
               OpBranch %48
         %48 = OpLabel
         %74 = OpIAdd %30 %496 %73
               OpStore %43 %74
               OpBranch %45
         %47 = OpLabel
               OpReturn
               OpFunctionEnd
          %8 = OpFunction %2 None %3
          %9 = OpLabel
         %75 = OpVariable %31 Function
         %79 = OpVariable %31 Function
         %88 = OpVariable %56 Function
         %76 = OpAccessChain %39 %36 %38
         %77 = OpLoad %33 %76
         %78 = OpConvertFToS %30 %77
               OpStore %75 %78
               OpStore %79 %44
               OpBranch %80
         %80 = OpLabel
        %497 = OpPhi %30 %44 %9 %104 %83
               OpLoopMerge %82 %83 None
               OpBranch %84
         %84 = OpLabel
         %87 = OpSLessThanEqual %52 %497 %78
               OpBranchConditional %87 %81 %82
         %81 = OpLabel
         %92 = OpIAdd %30 %497 %78
         %93 = OpAccessChain %31 %88 %92
         %94 = OpLoad %30 %93
         %95 = OpAccessChain %31 %88 %497
               OpStore %95 %94
         %98 = OpIAdd %30 %497 %78
        %100 = OpAccessChain %31 %88 %497
        %101 = OpLoad %30 %100
        %102 = OpAccessChain %31 %88 %98
               OpStore %102 %101
               OpBranch %83
         %83 = OpLabel
        %104 = OpIAdd %30 %497 %73
               OpStore %79 %104
               OpBranch %80
         %82 = OpLabel
               OpReturn
               OpFunctionEnd
         %10 = OpFunction %2 None %3
         %11 = OpLabel
        %105 = OpVariable %31 Function
        %109 = OpVariable %31 Function
        %119 = OpVariable %56 Function
        %106 = OpAccessChain %39 %36 %38
        %107 = OpLoad %33 %106
        %108 = OpConvertFToS %30 %107
               OpStore %105 %108
               OpStore %109 %110
               OpBranch %111
        %111 = OpLabel
        %498 = OpPhi %30 %110 %11 %135 %114
               OpLoopMerge %113 %114 None
               OpBranch %115
        %115 = OpLabel
        %118 = OpSGreaterThan %52 %498 %108
               OpBranchConditional %118 %112 %113
        %112 = OpLabel
        %123 = OpIAdd %30 %498 %108
        %124 = OpAccessChain %31 %119 %123
        %125 = OpLoad %30 %124
        %126 = OpAccessChain %31 %119 %498
               OpStore %126 %125
        %129 = OpIAdd %30 %498 %108
        %131 = OpAccessChain %31 %119 %498
        %132 = OpLoad %30 %131
        %133 = OpAccessChain %31 %119 %129
               OpStore %133 %132
               OpBranch %114
        %114 = OpLabel
        %135 = OpISub %30 %498 %73
               OpStore %109 %135
               OpBranch %111
        %113 = OpLabel
               OpReturn
               OpFunctionEnd
         %12 = OpFunction %2 None %3
         %13 = OpLabel
        %136 = OpVariable %31 Function
        %140 = OpVariable %31 Function
        %149 = OpVariable %56 Function
        %137 = OpAccessChain %39 %36 %38
        %138 = OpLoad %33 %137
        %139 = OpConvertFToS %30 %138
               OpStore %136 %139
               OpStore %140 %110
               OpBranch %141
        %141 = OpLabel
        %499 = OpPhi %30 %110 %13 %165 %144
               OpLoopMerge %143 %144 None
               OpBranch %145
        %145 = OpLabel
        %148 = OpSGreaterThanEqual %52 %499 %139
               OpBranchConditional %148 %142 %143
        %142 = OpLabel
        %153 = OpIAdd %30 %499 %139
        %154 = OpAccessChain %31 %149 %153
        %155 = OpLoad %30 %154
        %156 = OpAccessChain %31 %149 %499
               OpStore %156 %155
        %159 = OpIAdd %30 %499 %139
        %161 = OpAccessChain %31 %149 %499
        %162 = OpLoad %30 %161
        %163 = OpAccessChain %31 %149 %159
               OpStore %163 %162
               OpBranch %144
        %144 = OpLabel
        %165 = OpISub %30 %499 %73
               OpStore %140 %165
               OpBranch %141
        %143 = OpLabel
               OpReturn
               OpFunctionEnd
         %14 = OpFunction %2 None %3
         %15 = OpLabel
        %166 = OpVariable %31 Function
        %170 = OpVariable %31 Function
        %179 = OpVariable %56 Function
        %167 = OpAccessChain %39 %36 %38
        %168 = OpLoad %33 %167
        %169 = OpConvertFToS %30 %168
               OpStore %166 %169
               OpStore %170 %169
               OpBranch %172
        %172 = OpLabel
        %500 = OpPhi %30 %169 %15 %195 %175
               OpLoopMerge %174 %175 None
               OpBranch %176
        %176 = OpLabel
        %178 = OpSLessThan %52 %500 %110
               OpBranchConditional %178 %173 %174
        %173 = OpLabel
        %183 = OpIAdd %30 %500 %169
        %184 = OpAccessChain %31 %179 %183
        %185 = OpLoad %30 %184
        %186 = OpAccessChain %31 %179 %500
               OpStore %186 %185
        %189 = OpIAdd %30 %500 %169
        %191 = OpAccessChain %31 %179 %500
        %192 = OpLoad %30 %191
        %193 = OpAccessChain %31 %179 %189
               OpStore %193 %192
               OpBranch %175
        %175 = OpLabel
        %195 = OpIAdd %30 %500 %73
               OpStore %170 %195
               OpBranch %172
        %174 = OpLabel
               OpReturn
               OpFunctionEnd
         %16 = OpFunction %2 None %3
         %17 = OpLabel
        %196 = OpVariable %31 Function
        %200 = OpVariable %31 Function
        %209 = OpVariable %56 Function
        %197 = OpAccessChain %39 %36 %38
        %198 = OpLoad %33 %197
        %199 = OpConvertFToS %30 %198
               OpStore %196 %199
               OpStore %200 %199
               OpBranch %202
        %202 = OpLabel
        %501 = OpPhi %30 %199 %17 %225 %205
               OpLoopMerge %204 %205 None
               OpBranch %206
        %206 = OpLabel
        %208 = OpSLessThanEqual %52 %501 %110
               OpBranchConditional %208 %203 %204
        %203 = OpLabel
        %213 = OpIAdd %30 %501 %199
        %214 = OpAccessChain %31 %209 %213
        %215 = OpLoad %30 %214
        %216 = OpAccessChain %31 %209 %501
               OpStore %216 %215
        %219 = OpIAdd %30 %501 %199
        %221 = OpAccessChain %31 %209 %501
        %222 = OpLoad %30 %221
        %223 = OpAccessChain %31 %209 %219
               OpStore %223 %222
               OpBranch %205
        %205 = OpLabel
        %225 = OpIAdd %30 %501 %73
               OpStore %200 %225
               OpBranch %202
        %204 = OpLabel
               OpReturn
               OpFunctionEnd
         %18 = OpFunction %2 None %3
         %19 = OpLabel
        %226 = OpVariable %31 Function
        %230 = OpVariable %31 Function
        %239 = OpVariable %56 Function
        %227 = OpAccessChain %39 %36 %38
        %228 = OpLoad %33 %227
        %229 = OpConvertFToS %30 %228
               OpStore %226 %229
               OpStore %230 %229
               OpBranch %232
        %232 = OpLabel
        %502 = OpPhi %30 %229 %19 %255 %235
               OpLoopMerge %234 %235 None
               OpBranch %236
        %236 = OpLabel
        %238 = OpSGreaterThan %52 %502 %44
               OpBranchConditional %238 %233 %234
        %233 = OpLabel
        %243 = OpIAdd %30 %502 %229
        %244 = OpAccessChain %31 %239 %243
        %245 = OpLoad %30 %244
        %246 = OpAccessChain %31 %239 %502
               OpStore %246 %245
        %249 = OpIAdd %30 %502 %229
        %251 = OpAccessChain %31 %239 %502
        %252 = OpLoad %30 %251
        %253 = OpAccessChain %31 %239 %249
               OpStore %253 %252
               OpBranch %235
        %235 = OpLabel
        %255 = OpISub %30 %502 %73
               OpStore %230 %255
               OpBranch %232
        %234 = OpLabel
               OpReturn
               OpFunctionEnd
         %20 = OpFunction %2 None %3
         %21 = OpLabel
        %256 = OpVariable %31 Function
        %260 = OpVariable %31 Function
        %269 = OpVariable %56 Function
        %257 = OpAccessChain %39 %36 %38
        %258 = OpLoad %33 %257
        %259 = OpConvertFToS %30 %258
               OpStore %256 %259
               OpStore %260 %259
               OpBranch %262
        %262 = OpLabel
        %503 = OpPhi %30 %259 %21 %285 %265
               OpLoopMerge %264 %265 None
               OpBranch %266
        %266 = OpLabel
        %268 = OpSGreaterThanEqual %52 %503 %44
               OpBranchConditional %268 %263 %264
        %263 = OpLabel
        %273 = OpIAdd %30 %503 %259
        %274 = OpAccessChain %31 %269 %273
        %275 = OpLoad %30 %274
        %276 = OpAccessChain %31 %269 %503
               OpStore %276 %275
        %279 = OpIAdd %30 %503 %259
        %281 = OpAccessChain %31 %269 %503
        %282 = OpLoad %30 %281
        %283 = OpAccessChain %31 %269 %279
               OpStore %283 %282
               OpBranch %265
        %265 = OpLabel
        %285 = OpISub %30 %503 %73
               OpStore %260 %285
               OpBranch %262
        %264 = OpLabel
               OpReturn
               OpFunctionEnd
         %22 = OpFunction %2 None %3
         %23 = OpLabel
        %286 = OpVariable %31 Function
        %290 = OpVariable %31 Function
        %295 = OpVariable %31 Function
        %305 = OpVariable %56 Function
        %287 = OpAccessChain %39 %36 %38
        %288 = OpLoad %33 %287
        %289 = OpConvertFToS %30 %288
               OpStore %286 %289
        %292 = OpAccessChain %39 %36 %291
        %293 = OpLoad %33 %292
        %294 = OpConvertFToS %30 %293
               OpStore %290 %294
               OpStore %295 %289
               OpBranch %297
        %297 = OpLabel
        %504 = OpPhi %30 %289 %23 %336 %300
               OpLoopMerge %299 %300 None
               OpBranch %301
        %301 = OpLabel
        %304 = OpSLessThan %52 %504 %294
               OpBranchConditional %304 %298 %299
        %298 = OpLabel
        %308 = OpIAdd %30 %504 %289
        %310 = OpIAdd %30 %308 %294
        %313 = OpIAdd %30 %504 %289
        %316 = OpIMul %30 %314 %294
        %317 = OpIAdd %30 %313 %316
        %318 = OpAccessChain %31 %305 %317
        %319 = OpLoad %30 %318
        %320 = OpAccessChain %31 %305 %310
               OpStore %320 %319
        %323 = OpIAdd %30 %504 %289
        %325 = OpIMul %30 %314 %294
        %326 = OpIAdd %30 %323 %325
        %329 = OpIAdd %30 %504 %289
        %331 = OpIAdd %30 %329 %294
        %332 = OpAccessChain %31 %305 %331
        %333 = OpLoad %30 %332
        %334 = OpAccessChain %31 %305 %326
               OpStore %334 %333
               OpBranch %300
        %300 = OpLabel
        %336 = OpIAdd %30 %504 %73
               OpStore %295 %336
               OpBranch %297
        %299 = OpLabel
               OpReturn
               OpFunctionEnd
         %24 = OpFunction %2 None %3
         %25 = OpLabel
        %337 = OpVariable %31 Function
        %341 = OpVariable %31 Function
        %345 = OpVariable %31 Function
        %355 = OpVariable %56 Function
        %338 = OpAccessChain %39 %36 %38
        %339 = OpLoad %33 %338
        %340 = OpConvertFToS %30 %339
               OpStore %337 %340
        %342 = OpAccessChain %39 %36 %291
        %343 = OpLoad %33 %342
        %344 = OpConvertFToS %30 %343
               OpStore %341 %344
               OpStore %345 %340
               OpBranch %347
        %347 = OpLabel
        %505 = OpPhi %30 %340 %25 %385 %350
               OpLoopMerge %349 %350 None
               OpBranch %351
        %351 = OpLabel
        %354 = OpSLessThanEqual %52 %505 %344
               OpBranchConditional %354 %348 %349
        %348 = OpLabel
        %358 = OpIAdd %30 %505 %340
        %360 = OpIAdd %30 %358 %344
        %363 = OpIAdd %30 %505 %340
        %365 = OpIMul %30 %314 %344
        %366 = OpIAdd %30 %363 %365
        %367 = OpAccessChain %31 %355 %366
        %368 = OpLoad %30 %367
        %369 = OpAccessChain %31 %355 %360
               OpStore %369 %368
        %372 = OpIAdd %30 %505 %340
        %374 = OpIMul %30 %314 %344
        %375 = OpIAdd %30 %372 %374
        %378 = OpIAdd %30 %505 %340
        %380 = OpIAdd %30 %378 %344
        %381 = OpAccessChain %31 %355 %380
        %382 = OpLoad %30 %381
        %383 = OpAccessChain %31 %355 %375
               OpStore %383 %382
               OpBranch %350
        %350 = OpLabel
        %385 = OpIAdd %30 %505 %73
               OpStore %345 %385
               OpBranch %347
        %349 = OpLabel
               OpReturn
               OpFunctionEnd
         %26 = OpFunction %2 None %3
         %27 = OpLabel
        %386 = OpVariable %31 Function
        %390 = OpVariable %31 Function
        %394 = OpVariable %31 Function
        %404 = OpVariable %56 Function
        %387 = OpAccessChain %39 %36 %38
        %388 = OpLoad %33 %387
        %389 = OpConvertFToS %30 %388
               OpStore %386 %389
        %391 = OpAccessChain %39 %36 %291
        %392 = OpLoad %33 %391
        %393 = OpConvertFToS %30 %392
               OpStore %390 %393
               OpStore %394 %389
               OpBranch %396
        %396 = OpLabel
        %506 = OpPhi %30 %389 %27 %434 %399
               OpLoopMerge %398 %399 None
               OpBranch %400
        %400 = OpLabel
        %403 = OpSGreaterThan %52 %506 %393
               OpBranchConditional %403 %397 %398
        %397 = OpLabel
        %407 = OpIAdd %30 %506 %389
        %409 = OpIAdd %30 %407 %393
        %412 = OpIAdd %30 %506 %389
        %414 = OpIMul %30 %314 %393
        %415 = OpIAdd %30 %412 %414
        %416 = OpAccessChain %31 %404 %415
        %417 = OpLoad %30 %416
        %418 = OpAccessChain %31 %404 %409
               OpStore %418 %417
        %421 = OpIAdd %30 %506 %389
        %423 = OpIMul %30 %314 %393
        %424 = OpIAdd %30 %421 %423
        %427 = OpIAdd %30 %506 %389
        %429 = OpIAdd %30 %427 %393
        %430 = OpAccessChain %31 %404 %429
        %431 = OpLoad %30 %430
        %432 = OpAccessChain %31 %404 %424
               OpStore %432 %431
               OpBranch %399
        %399 = OpLabel
        %434 = OpISub %30 %506 %73
               OpStore %394 %434
               OpBranch %396
        %398 = OpLabel
               OpReturn
               OpFunctionEnd
         %28 = OpFunction %2 None %3
         %29 = OpLabel
        %435 = OpVariable %31 Function
        %439 = OpVariable %31 Function
        %443 = OpVariable %31 Function
        %453 = OpVariable %56 Function
        %436 = OpAccessChain %39 %36 %38
        %437 = OpLoad %33 %436
        %438 = OpConvertFToS %30 %437
               OpStore %435 %438
        %440 = OpAccessChain %39 %36 %291
        %441 = OpLoad %33 %440
        %442 = OpConvertFToS %30 %441
               OpStore %439 %442
               OpStore %443 %438
               OpBranch %445
        %445 = OpLabel
        %507 = OpPhi %30 %438 %29 %483 %448
               OpLoopMerge %447 %448 None
               OpBranch %449
        %449 = OpLabel
        %452 = OpSGreaterThanEqual %52 %507 %442
               OpBranchConditional %452 %446 %447
        %446 = OpLabel
        %456 = OpIAdd %30 %507 %438
        %458 = OpIAdd %30 %456 %442
        %461 = OpIAdd %30 %507 %438
        %463 = OpIMul %30 %314 %442
        %464 = OpIAdd %30 %461 %463
        %465 = OpAccessChain %31 %453 %464
        %466 = OpLoad %30 %465
        %467 = OpAccessChain %31 %453 %458
               OpStore %467 %466
        %470 = OpIAdd %30 %507 %438
        %472 = OpIMul %30 %314 %442
        %473 = OpIAdd %30 %470 %472
        %476 = OpIAdd %30 %507 %438
        %478 = OpIAdd %30 %476 %442
        %479 = OpAccessChain %31 %453 %478
        %480 = OpLoad %30 %479
        %481 = OpAccessChain %31 %453 %473
               OpStore %481 %480
               OpBranch %448
        %448 = OpLabel
        %483 = OpISub %30 %507 %73
               OpStore %443 %483
               OpBranch %445
        %447 = OpLabel
               OpReturn
               OpFunctionEnd
)";
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << text << std::endl;
  {
    // Function a
    const ir::Function* f = spvtest::GetFunction(module, 6);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    const ir::Instruction* stores[2];
    int stores_found = 0;
    for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 46)) {
      if (inst.opcode() == SpvOp::SpvOpStore) {
        stores[stores_found] = &inst;
        ++stores_found;
      }
    }

    for (int i = 0; i < 2; ++i) {
      EXPECT_TRUE(stores[i]);
    }

    // 63 -> 64
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(63)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));

      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[0]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      // Independent and supported.
      EXPECT_TRUE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }

    // 70 -> 71
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(70)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));

      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[1]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      // Independent but not supported.
      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }
  }
  {
    // Function b
    const ir::Function* f = spvtest::GetFunction(module, 8);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    const ir::Instruction* stores[2];
    int stores_found = 0;
    for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 81)) {
      if (inst.opcode() == SpvOp::SpvOpStore) {
        stores[stores_found] = &inst;
        ++stores_found;
      }
    }

    for (int i = 0; i < 2; ++i) {
      EXPECT_TRUE(stores[i]);
    }

    // 94 -> 95
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(94)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));
      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[0]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      // Dependent.
      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }

    // 101 -> 102
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(101)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));
      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[1]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      // Dependent.
      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }
  }
  {
    // Function c
    const ir::Function* f = spvtest::GetFunction(module, 10);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    const ir::Instruction* stores[2];
    int stores_found = 0;
    for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 112)) {
      if (inst.opcode() == SpvOp::SpvOpStore) {
        stores[stores_found] = &inst;
        ++stores_found;
      }
    }

    for (int i = 0; i < 2; ++i) {
      EXPECT_TRUE(stores[i]);
    }

    // 125 -> 126
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(125)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));
      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[0]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      // Independent but not supported.
      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }

    // 132 -> 133
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(132)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));
      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[1]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      // Independent but not supported.
      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }
  }
  {
    // Function d
    const ir::Function* f = spvtest::GetFunction(module, 12);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    const ir::Instruction* stores[2];
    int stores_found = 0;
    for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 142)) {
      if (inst.opcode() == SpvOp::SpvOpStore) {
        stores[stores_found] = &inst;
        ++stores_found;
      }
    }

    for (int i = 0; i < 2; ++i) {
      EXPECT_TRUE(stores[i]);
    }

    // 155 -> 156
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(155)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));
      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[0]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      // Dependent.
      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }

    // 162 -> 162
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(162)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));
      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[1]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      // Dependent.
      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }
  }
  {
    // Function e
    const ir::Function* f = spvtest::GetFunction(module, 14);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    const ir::Instruction* stores[2];
    int stores_found = 0;
    for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 173)) {
      if (inst.opcode() == SpvOp::SpvOpStore) {
        stores[stores_found] = &inst;
        ++stores_found;
      }
    }

    for (int i = 0; i < 2; ++i) {
      EXPECT_TRUE(stores[i]);
    }

    // 185 -> 186
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(185)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));

      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[0]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      // Independent but not supported.
      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }

    // 192 -> 193
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(192)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));

      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[1]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      // Independent but not supported.
      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }
  }
  {
    // Function f
    const ir::Function* f = spvtest::GetFunction(module, 16);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    const ir::Instruction* stores[2];
    int stores_found = 0;
    for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 203)) {
      if (inst.opcode() == SpvOp::SpvOpStore) {
        stores[stores_found] = &inst;
        ++stores_found;
      }
    }

    for (int i = 0; i < 2; ++i) {
      EXPECT_TRUE(stores[i]);
    }

    // 215 -> 216
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(215)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));

      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[0]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      // Dependent.
      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }

    // 222 -> 223
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(222)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));

      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[1]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      // Dependent.
      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }
  }
  {
    // Function g
    const ir::Function* f = spvtest::GetFunction(module, 18);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    const ir::Instruction* stores[2];
    int stores_found = 0;
    for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 233)) {
      if (inst.opcode() == SpvOp::SpvOpStore) {
        stores[stores_found] = &inst;
        ++stores_found;
      }
    }

    for (int i = 0; i < 2; ++i) {
      EXPECT_TRUE(stores[i]);
    }

    // 245 -> 246
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(245)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));

      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[0]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      // Independent and supported.
      EXPECT_TRUE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }

    // 252 -> 253
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(252)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));

      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[1]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      // Independent but not supported.
      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }
  }
  {
    // Function h
    const ir::Function* f = spvtest::GetFunction(module, 20);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    const ir::Instruction* stores[2];
    int stores_found = 0;
    for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 263)) {
      if (inst.opcode() == SpvOp::SpvOpStore) {
        stores[stores_found] = &inst;
        ++stores_found;
      }
    }

    for (int i = 0; i < 2; ++i) {
      EXPECT_TRUE(stores[i]);
    }

    // 275 -> 276
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(275)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));

      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[0]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      // Dependent
      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }

    // 282 -> 283
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(282)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));

      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[1]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      // Dependent
      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }
  }
  {
    // Function i
    const ir::Function* f = spvtest::GetFunction(module, 22);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    const ir::Instruction* stores[2];
    int stores_found = 0;
    for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 301)) {
      if (inst.opcode() == SpvOp::SpvOpStore) {
        stores[stores_found] = &inst;
        ++stores_found;
      }
    }

    for (int i = 0; i < 2; ++i) {
      EXPECT_TRUE(stores[i]);
    }

    // 319 -> 320
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(319)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));

      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[0]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }

    // 333 -> 334
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(333)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));

      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[1]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }
  }
  {
    // Function j
    const ir::Function* f = spvtest::GetFunction(module, 24);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    const ir::Instruction* stores[2];
    int stores_found = 0;
    for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 348)) {
      if (inst.opcode() == SpvOp::SpvOpStore) {
        stores[stores_found] = &inst;
        ++stores_found;
      }
    }

    for (int i = 0; i < 2; ++i) {
      EXPECT_TRUE(stores[i]);
    }

    // 368 -> 369
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(368)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));

      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[0]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }

    // 382 -> 383
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(382)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));

      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[1]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }
  }
  {
    // Function k
    const ir::Function* f = spvtest::GetFunction(module, 26);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    const ir::Instruction* stores[2];
    int stores_found = 0;
    for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 397)) {
      if (inst.opcode() == SpvOp::SpvOpStore) {
        stores[stores_found] = &inst;
        ++stores_found;
      }
    }

    for (int i = 0; i < 2; ++i) {
      EXPECT_TRUE(stores[i]);
    }

    // 417 -> 418
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(417)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));

      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[0]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }

    // 431 -> 432
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(431)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));

      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[1]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }
  }
  {
    // Function l
    const ir::Function* f = spvtest::GetFunction(module, 28);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    const ir::Instruction* stores[2];
    int stores_found = 0;
    for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 446)) {
      if (inst.opcode() == SpvOp::SpvOpStore) {
        stores[stores_found] = &inst;
        ++stores_found;
      }
    }

    for (int i = 0; i < 2; ++i) {
      EXPECT_TRUE(stores[i]);
    }

    // 466 -> 467
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(466)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));

      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[0]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }

    // 480 -> 481
    {
      // Analyse and simplify the instruction behind the access chain of this
      // load.
      ir::Instruction* load_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(context->get_def_use_mgr()
                           ->GetDef(480)
                           ->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(load_var));

      // Analyse and simplify the instruction behind the access chain of this
      // store.
      ir::Instruction* store_var = context->get_def_use_mgr()->GetDef(
          context->get_def_use_mgr()
              ->GetDef(stores[1]->GetSingleWordInOperand(0))
              ->GetSingleWordInOperand(1));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(store_var));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(
          delta, store->AsSERecurrentNode()->GetCoefficient()));
    }
  }
}

}  // namespace
