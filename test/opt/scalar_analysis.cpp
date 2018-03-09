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

#include "assembly_builder.h"
#include "function_utils.h"
#include "pass_fixture.h"
#include "pass_utils.h"

#include "opt/iterator.h"
#include "opt/loop_descriptor.h"
#include "opt/pass.h"
#include "opt/scalar_analysis.h"
#include "opt/tree_iterator.h"

namespace {

using namespace spvtools;
using ::testing::UnorderedElementsAre;

using PassClassTest = PassTest<::testing::Test>;

/*
Generated from the following GLSL + --eliminate-local-multi-store

#version 410 core
layout (location = 1) out float array[10];
void main() {
  for (int i = 0; i < 10; ++i) {
    array[i] = array[i+1];
  }
}
*/
TEST_F(PassClassTest, BasicEvolutionTest) {
  const std::string text = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main" %24
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 410
               OpName %4 "main"
               OpName %24 "array"
               OpDecorate %24 Location 1
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %6 = OpTypeInt 32 1
          %7 = OpTypePointer Function %6
          %9 = OpConstant %6 0
         %16 = OpConstant %6 10
         %17 = OpTypeBool
         %19 = OpTypeFloat 32
         %20 = OpTypeInt 32 0
         %21 = OpConstant %20 10
         %22 = OpTypeArray %19 %21
         %23 = OpTypePointer Output %22
         %24 = OpVariable %23 Output
         %27 = OpConstant %6 1
         %29 = OpTypePointer Output %19
          %4 = OpFunction %2 None %3
          %5 = OpLabel
               OpBranch %10
         %10 = OpLabel
         %35 = OpPhi %6 %9 %5 %34 %13
               OpLoopMerge %12 %13 None
               OpBranch %14
         %14 = OpLabel
         %18 = OpSLessThan %17 %35 %16
               OpBranchConditional %18 %11 %12
         %11 = OpLabel
         %28 = OpIAdd %6 %35 %27
         %30 = OpAccessChain %29 %24 %28
         %31 = OpLoad %19 %30
         %32 = OpAccessChain %29 %24 %35
               OpStore %32 %31
               OpBranch %13
         %13 = OpLabel
         %34 = OpIAdd %6 %35 %27
               OpBranch %10
         %12 = OpLabel
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
  const ir::Function* f = spvtest::GetFunction(module, 4);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  opt::ScalarEvolutionAnalysis analysis{context.get()};
  // opt::LoopDependenceAnalysis analysis {context.get(), ld.etLoopByIndex(0)};
  // analysis.DumpIterationSpaceAsDot(std::cout);

  const ir::Instruction* store = nullptr;
  const ir::Instruction* load = nullptr;
  for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 11)) {
    if (inst.opcode() == SpvOp::SpvOpStore) {
      store = &inst;
    }
    if (inst.opcode() == SpvOp::SpvOpLoad) {
      load = &inst;
    }
  }

  EXPECT_TRUE(load);
  EXPECT_TRUE(store);

  analysis.AnalyzeLoop(ld.GetLoopByIndex(0));
  analysis.DumpAsDot(std::cout);

  ir::Instruction* access_chain =
      context->get_def_use_mgr()->GetDef(load->GetSingleWordInOperand(0));

  ir::Instruction* child = context->get_def_use_mgr()->GetDef(
      access_chain->GetSingleWordInOperand(1));
  const opt::SENode* node = analysis.AnalyzeInstruction(child);

  EXPECT_TRUE(node);
  EXPECT_TRUE(node->CanFoldToConstant());
  node->DumpDot(std::cout);
  analysis.SimplifyExpression(const_cast<opt::SENode*>(node));

  analysis.DumpAsDot(std::cout);

  // EXPECT_FALSE(
  //     analysis.GetDependence(store, context->get_def_use_mgr()->GetDef(31)));
  // analysis.DumpIterationSpaceAsDot(std::cout);
}

/*
Generated from the following GLSL + --eliminate-local-multi-store


*/
TEST_F(PassClassTest, LoadTest) {
  const std::string text = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main" %3 %4
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 430
               OpName %2 "main"
               OpName %3 "array"
               OpName %4 "loop_invariant"
               OpDecorate %3 Location 1
               OpDecorate %4 Flat
               OpDecorate %4 Location 2
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
         %16 = OpTypePointer Output %15
          %3 = OpVariable %16 Output
         %17 = OpTypePointer Input %7
          %4 = OpVariable %17 Input
         %18 = OpTypePointer Output %12
         %19 = OpConstant %7 1
          %2 = OpFunction %5 None %6
         %20 = OpLabel
               OpBranch %21
         %21 = OpLabel
         %22 = OpPhi %7 %9 %20 %23 %24
               OpLoopMerge %25 %24 None
               OpBranch %26
         %26 = OpLabel
         %27 = OpSLessThan %11 %22 %10
               OpBranchConditional %27 %28 %25
         %28 = OpLabel
         %29 = OpLoad %7 %4
         %30 = OpIAdd %7 %22 %29
         %31 = OpAccessChain %18 %3 %30
         %32 = OpLoad %12 %31
         %33 = OpAccessChain %18 %3 %22
               OpStore %33 %32
               OpBranch %24
         %24 = OpLabel
         %23 = OpIAdd %7 %22 %19
               OpBranch %21
         %25 = OpLabel
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
  const ir::Function* f = spvtest::GetFunction(module, 2);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  opt::ScalarEvolutionAnalysis analysis{context.get()};
  // opt::LoopDependenceAnalysis analysis {context.get(), ld.etLoopByIndex(0)};
  // analysis.DumpIterationSpaceAsDot(std::cout);

  const ir::Instruction* load = nullptr;
  for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 28)) {
    if (inst.opcode() == SpvOp::SpvOpLoad) {
      load = &inst;
    }
  }

  EXPECT_TRUE(load);

  analysis.AnalyzeLoop(ld.GetLoopByIndex(0));
  analysis.DumpAsDot(std::cout);

  ir::Instruction* access_chain =
      context->get_def_use_mgr()->GetDef(load->GetSingleWordInOperand(0));

  ir::Instruction* child = context->get_def_use_mgr()->GetDef(
      access_chain->GetSingleWordInOperand(1));
  //  const opt::SENode* node =
  //  analysis.GetNodeFromInstruction(child->unique_id());

  const opt::SENode* node = analysis.AnalyzeInstruction(child);
  EXPECT_TRUE(node);
  EXPECT_FALSE(node->CanFoldToConstant());
  // EXPECT_FALSE(
  //     analysis.GetDependence(store, context->get_def_use_mgr()->GetDef(31)));
  // analysis.DumpIterationSpaceAsDot(std::cout);
}

TEST_F(PassClassTest, SimplifySimple) {
  const std::string text = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main" %3 %4
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 430
               OpName %2 "main"
               OpName %3 "array"
               OpName %4 "loop_invariant"
               OpDecorate %3 Location 1
               OpDecorate %4 Flat
               OpDecorate %4 Location 2
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
         %16 = OpTypePointer Output %15
          %3 = OpVariable %16 Output
         %17 = OpTypePointer Input %7
          %4 = OpVariable %17 Input
         %18 = OpConstant %7 4
         %19 = OpConstant %7 48
         %20 = OpTypePointer Output %12
         %21 = OpConstant %7 32
         %22 = OpConstant %7 3
         %23 = OpConstant %7 2
         %24 = OpConstant %7 15
         %25 = OpConstant %7 1
          %2 = OpFunction %5 None %6
         %26 = OpLabel
               OpBranch %27
         %27 = OpLabel
         %28 = OpPhi %7 %9 %26 %29 %30
               OpLoopMerge %31 %30 None
               OpBranch %32
         %32 = OpLabel
         %33 = OpSLessThan %11 %28 %10
               OpBranchConditional %33 %34 %31
         %34 = OpLabel
         %35 = OpLoad %7 %4
         %36 = OpIMul %7 %35 %18
         %37 = OpIAdd %7 %36 %18
         %38 = OpIAdd %7 %37 %18
         %39 = OpIAdd %7 %38 %19
         %40 = OpAccessChain %20 %3 %39
         %41 = OpLoad %12 %40
         %42 = OpAccessChain %20 %3 %28
               OpStore %42 %41
         %43 = OpLoad %7 %4
         %44 = OpIMul %7 %43 %18
         %45 = OpIAdd %7 %44 %21
         %46 = OpLoad %7 %4
         %47 = OpIMul %7 %46 %22
         %48 = OpISub %7 %45 %47
         %49 = OpAccessChain %20 %3 %48
         %50 = OpLoad %12 %49
         %51 = OpAccessChain %20 %3 %28
               OpStore %51 %50
         %52 = OpLoad %7 %4
         %53 = OpIMul %7 %52 %23
         %54 = OpIAdd %7 %53 %21
         %55 = OpLoad %7 %4
         %56 = OpISub %7 %54 %55
         %57 = OpLoad %7 %4
         %58 = OpISub %7 %56 %57
         %59 = OpISub %7 %58 %24
         %60 = OpAccessChain %20 %3 %59
         %61 = OpLoad %12 %60
         %62 = OpAccessChain %20 %3 %28
               OpStore %62 %61
               OpBranch %30
         %30 = OpLabel
         %29 = OpIAdd %7 %28 %25
               OpBranch %27
         %31 = OpLabel
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
  const ir::Function* f = spvtest::GetFunction(module, 2);
  ir::LoopDescriptor& ld = *context->GetLoopDescriptor(f);

  opt::ScalarEvolutionAnalysis analysis{context.get()};
  // opt::LoopDependenceAnalysis analysis {context.get(), ld.etLoopByIndex(0)};
  // analysis.DumpIterationSpaceAsDot(std::cout);

  int count = 0;
  const ir::Instruction* load = nullptr;
  for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 34)) {
    if (inst.opcode() == SpvOp::SpvOpLoad) {
      count++;
      load = &inst;

      if (count == 5) break;
    }
  }

  EXPECT_TRUE(load);

  analysis.AnalyzeLoop(ld.GetLoopByIndex(0));
  //  analysis.AnalyzeInstruction(load);
  analysis.DumpAsDot(std::cout);

  ir::Instruction* access_chain =
      context->get_def_use_mgr()->GetDef(load->GetSingleWordInOperand(0));

  ir::Instruction* child = context->get_def_use_mgr()->GetDef(
      access_chain->GetSingleWordInOperand(1));
  //  const opt::SENode* node =
  //  analysis.GetNodeFromInstruction(child->unique_id());

  const opt::SENode* node = analysis.AnalyzeInstruction(child);
  EXPECT_TRUE(node);
  EXPECT_FALSE(node->CanFoldToConstant());

  std::cout << "digraph  {\n";
  node->DumpDot(std::cout, true);
  std::cout << "}\n";

  analysis.SimplifyExpression(const_cast<opt::SENode*>(node));

  std::cout << "digraph  {\n";
  node->DumpDot(std::cout, true);
  std::cout << "}\n";
  //  analysis.DumpAsDot(std::cout);

  // EXPECT_FALSE(
  //     analysis.GetDependence(store, context->get_def_use_mgr()->GetDef(31)));
  // analysis.DumpIterationSpaceAsDot(std::cout);
}

}  // namespace
