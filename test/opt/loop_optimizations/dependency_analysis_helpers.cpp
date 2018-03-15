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
  with --eliminate-local-multi-store#version 440 core
#version 440 core
layout(location = 0) in vec4 in_vec;
void a() {
  int N = int(in_vec.x);
  int arr[10];
  for (int i = 0; i < N; i++) {
    arr[i] = arr[i+N];
    arr[i+N] = arr[i];
  }
}
void b() {
  int N = int(in_vec.x);
  int arr[10];
  for (int i = 0; i <= N; i++) {
    arr[i] = arr[i+N];
    arr[i+N] = arr[i];
  }
}
void c() {
  int N = int(in_vec.x);
  int arr[10];
  for (int i = 9; i > N; i++) {
    arr[i] = arr[i+N];
    arr[i+N] = arr[i];
  }
}
void d() {
  int N = int(in_vec.x);
  int arr[10];
  for (int i = 9; i >= N; i++) {
    arr[i] = arr[i+N];
    arr[i+N] = arr[i];
  }
}
void main(){
  a();
  b();
  c();
  d();
}
*/
TEST(DependencyAnalysisHelpers, symbolic_checks) {
  const std::string text = R"(               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main" %20
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 440
               OpName %4 "main"
               OpName %6 "a("
               OpName %8 "b("
               OpName %10 "c("
               OpName %12 "d("
               OpName %16 "N"
               OpName %20 "in_vec"
               OpName %27 "i"
               OpName %41 "arr"
               OpName %59 "N"
               OpName %63 "i"
               OpName %72 "arr"
               OpName %89 "N"
               OpName %93 "i"
               OpName %103 "arr"
               OpName %120 "N"
               OpName %124 "i"
               OpName %133 "arr"
               OpDecorate %20 Location 0
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
         %14 = OpTypeInt 32 1
         %15 = OpTypePointer Function %14
         %17 = OpTypeFloat 32
         %18 = OpTypeVector %17 4
         %19 = OpTypePointer Input %18
         %20 = OpVariable %19 Input
         %21 = OpTypeInt 32 0
         %22 = OpConstant %21 0
         %23 = OpTypePointer Input %17
         %28 = OpConstant %14 0
         %36 = OpTypeBool
         %38 = OpConstant %21 10
         %39 = OpTypeArray %14 %38
         %40 = OpTypePointer Function %39
         %57 = OpConstant %14 1
         %94 = OpConstant %14 9
          %4 = OpFunction %2 None %3
          %5 = OpLabel
        %150 = OpFunctionCall %2 %6
        %151 = OpFunctionCall %2 %8
        %152 = OpFunctionCall %2 %10
        %153 = OpFunctionCall %2 %12
               OpReturn
               OpFunctionEnd
          %6 = OpFunction %2 None %3
          %7 = OpLabel
         %16 = OpVariable %15 Function
         %27 = OpVariable %15 Function
         %41 = OpVariable %40 Function
         %24 = OpAccessChain %23 %20 %22
         %25 = OpLoad %17 %24
         %26 = OpConvertFToS %14 %25
               OpStore %16 %26
               OpStore %27 %28
               OpBranch %29
         %29 = OpLabel
        %154 = OpPhi %14 %28 %7 %58 %32
               OpLoopMerge %31 %32 None
               OpBranch %33
         %33 = OpLabel
         %37 = OpSLessThan %36 %154 %26
               OpBranchConditional %37 %30 %31
         %30 = OpLabel
         %45 = OpIAdd %14 %154 %26
         %46 = OpAccessChain %15 %41 %45
         %47 = OpLoad %14 %46
         %48 = OpAccessChain %15 %41 %154
               OpStore %48 %47
         %51 = OpIAdd %14 %154 %26
         %53 = OpAccessChain %15 %41 %154
         %54 = OpLoad %14 %53
         %55 = OpAccessChain %15 %41 %51
               OpStore %55 %54
               OpBranch %32
         %32 = OpLabel
         %58 = OpIAdd %14 %154 %57
               OpStore %27 %58
               OpBranch %29
         %31 = OpLabel
               OpReturn
               OpFunctionEnd
          %8 = OpFunction %2 None %3
          %9 = OpLabel
         %59 = OpVariable %15 Function
         %63 = OpVariable %15 Function
         %72 = OpVariable %40 Function
         %60 = OpAccessChain %23 %20 %22
         %61 = OpLoad %17 %60
         %62 = OpConvertFToS %14 %61
               OpStore %59 %62
               OpStore %63 %28
               OpBranch %64
         %64 = OpLabel
        %155 = OpPhi %14 %28 %9 %88 %67
               OpLoopMerge %66 %67 None
               OpBranch %68
         %68 = OpLabel
         %71 = OpSLessThanEqual %36 %155 %62
               OpBranchConditional %71 %65 %66
         %65 = OpLabel
         %76 = OpIAdd %14 %155 %62
         %77 = OpAccessChain %15 %72 %76
         %78 = OpLoad %14 %77
         %79 = OpAccessChain %15 %72 %155
               OpStore %79 %78
         %82 = OpIAdd %14 %155 %62
         %84 = OpAccessChain %15 %72 %155
         %85 = OpLoad %14 %84
         %86 = OpAccessChain %15 %72 %82
               OpStore %86 %85
               OpBranch %67
         %67 = OpLabel
         %88 = OpIAdd %14 %155 %57
               OpStore %63 %88
               OpBranch %64
         %66 = OpLabel
               OpReturn
               OpFunctionEnd
         %10 = OpFunction %2 None %3
         %11 = OpLabel
         %89 = OpVariable %15 Function
         %93 = OpVariable %15 Function
        %103 = OpVariable %40 Function
         %90 = OpAccessChain %23 %20 %22
         %91 = OpLoad %17 %90
         %92 = OpConvertFToS %14 %91
               OpStore %89 %92
               OpStore %93 %94
               OpBranch %95
         %95 = OpLabel
        %156 = OpPhi %14 %94 %11 %119 %98
               OpLoopMerge %97 %98 None
               OpBranch %99
         %99 = OpLabel
        %102 = OpSGreaterThan %36 %156 %92
               OpBranchConditional %102 %96 %97
         %96 = OpLabel
        %107 = OpIAdd %14 %156 %92
        %108 = OpAccessChain %15 %103 %107
        %109 = OpLoad %14 %108
        %110 = OpAccessChain %15 %103 %156
               OpStore %110 %109
        %113 = OpIAdd %14 %156 %92
        %115 = OpAccessChain %15 %103 %156
        %116 = OpLoad %14 %115
        %117 = OpAccessChain %15 %103 %113
               OpStore %117 %116
               OpBranch %98
         %98 = OpLabel
        %119 = OpISub %14 %156 %57
               OpStore %93 %119
               OpBranch %95
         %97 = OpLabel
               OpReturn
               OpFunctionEnd
         %12 = OpFunction %2 None %3
         %13 = OpLabel
        %120 = OpVariable %15 Function
        %124 = OpVariable %15 Function
        %133 = OpVariable %40 Function
        %121 = OpAccessChain %23 %20 %22
        %122 = OpLoad %17 %121
        %123 = OpConvertFToS %14 %122
               OpStore %120 %123
               OpStore %124 %94
               OpBranch %125
        %125 = OpLabel
        %157 = OpPhi %14 %94 %13 %149 %128
               OpLoopMerge %127 %128 None
               OpBranch %129
        %129 = OpLabel
        %132 = OpSGreaterThanEqual %36 %157 %123
               OpBranchConditional %132 %126 %127
        %126 = OpLabel
        %137 = OpIAdd %14 %157 %123
        %138 = OpAccessChain %15 %133 %137
        %139 = OpLoad %14 %138
        %140 = OpAccessChain %15 %133 %157
               OpStore %140 %139
        %143 = OpIAdd %14 %157 %123
        %145 = OpAccessChain %15 %133 %157
        %146 = OpLoad %14 %145
        %147 = OpAccessChain %15 %133 %143
               OpStore %147 %146
               OpBranch %128
        %128 = OpLabel
        %149 = OpISub %14 %157 %57
               OpStore %124 %149
               OpBranch %125
        %127 = OpLabel
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
    for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 30)) {
      if (inst.opcode() == SpvOp::SpvOpStore) {
        stores[stores_found] = &inst;
        ++stores_found;
      }
    }

    for (int i = 0; i < 2; ++i) {
      EXPECT_TRUE(stores[i]);
    }

    // 47 -> 48
    {
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(
              context->get_def_use_mgr()->GetDef(47)));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(stores[0]));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      EXPECT_TRUE(analysis.IsProvablyOutwithLoopBounds(delta));
    }

    // 53 -> 54
    {
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(
              context->get_def_use_mgr()->GetDef(53)));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(stores[1]));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      EXPECT_TRUE(analysis.IsProvablyOutwithLoopBounds(delta));
    }
  }
  {
    // Function b
    const ir::Function* f = spvtest::GetFunction(module, 8);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    const ir::Instruction* stores[2];
    int stores_found = 0;
    for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 65)) {
      if (inst.opcode() == SpvOp::SpvOpStore) {
        stores[stores_found] = &inst;
        ++stores_found;
      }
    }

    for (int i = 0; i < 2; ++i) {
      EXPECT_TRUE(stores[i]);
    }

    // 78 -> 79
    {
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(
              context->get_def_use_mgr()->GetDef(78)));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(stores[0]));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(delta));
    }

    // 85 -> 86
    {
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(
              context->get_def_use_mgr()->GetDef(85)));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(stores[1]));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(delta));
    }
  }
  {
    // Function c
    const ir::Function* f = spvtest::GetFunction(module, 10);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    const ir::Instruction* stores[2];
    int stores_found = 0;
    for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 96)) {
      if (inst.opcode() == SpvOp::SpvOpStore) {
        stores[stores_found] = &inst;
        ++stores_found;
      }
    }

    for (int i = 0; i < 2; ++i) {
      EXPECT_TRUE(stores[i]);
    }

    // 109 -> 110
    {
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(
              context->get_def_use_mgr()->GetDef(109)));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(stores[0]));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      EXPECT_TRUE(analysis.IsProvablyOutwithLoopBounds(delta));
    }

    // 116 -> 117
    {
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(
              context->get_def_use_mgr()->GetDef(116)));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(stores[1]));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      EXPECT_TRUE(analysis.IsProvablyOutwithLoopBounds(delta));
    }
  }
  {
    // Function d
    const ir::Function* f = spvtest::GetFunction(module, 12);
    ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);
    opt::LoopDependenceAnalysis analysis{context.get(), ld.GetLoopByIndex(0)};

    const ir::Instruction* stores[2];
    int stores_found = 0;
    for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 126)) {
      if (inst.opcode() == SpvOp::SpvOpStore) {
        stores[stores_found] = &inst;
        ++stores_found;
      }
    }

    for (int i = 0; i < 2; ++i) {
      EXPECT_TRUE(stores[i]);
    }

    // 139 -> 140
    {
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(
              context->get_def_use_mgr()->GetDef(139)));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(stores[0]));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(delta));
    }

    // 146 -> 147
    {
      opt::SENode* load = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(
              context->get_def_use_mgr()->GetDef(146)));
      opt::SENode* store = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->AnalyzeInstruction(stores[1]));

      opt::SENode* delta = analysis.GetScalarEvolution()->SimplifyExpression(
          analysis.GetScalarEvolution()->CreateSubtraction(load, store));

      EXPECT_FALSE(analysis.IsProvablyOutwithLoopBounds(delta));
    }
  }
}

}  // namespace
