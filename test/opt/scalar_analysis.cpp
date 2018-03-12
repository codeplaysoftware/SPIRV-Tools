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
  opt::ScalarEvolutionAnalysis analysis{context.get()};

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

  ir::Instruction* access_chain =
      context->get_def_use_mgr()->GetDef(load->GetSingleWordInOperand(0));

  ir::Instruction* child = context->get_def_use_mgr()->GetDef(
      access_chain->GetSingleWordInOperand(1));
  const opt::SENode* node = analysis.AnalyzeInstruction(child);

  EXPECT_TRUE(node);

  // Unsimplified node should have the form of ADD(REC(0,1), 1)
  EXPECT_TRUE(node->GetType() == opt::SENode::Add);

  const opt::SENode* child_1 = node->GetChild(0);
  EXPECT_TRUE(child_1->GetType() == opt::SENode::Constant ||
              child_1->GetType() == opt::SENode::RecurrentExpr);

  const opt::SENode* child_2 = node->GetChild(1);
  EXPECT_TRUE(child_2->GetType() == opt::SENode::Constant ||
              child_2->GetType() == opt::SENode::RecurrentExpr);

  opt::SENode* simplified =
      analysis.SimplifyExpression(const_cast<opt::SENode*>(node));

  // Simplified should be in the form of REC(1,1)
  EXPECT_TRUE(simplified->GetType() == opt::SENode::RecurrentExpr);

  EXPECT_TRUE(simplified->GetChild(0)->GetType() == opt::SENode::Constant);
  EXPECT_TRUE(simplified->GetChild(0)->FoldToSingleValue() == 1);

  EXPECT_TRUE(simplified->GetChild(1)->GetType() == opt::SENode::Constant);
  EXPECT_TRUE(simplified->GetChild(1)->FoldToSingleValue() == 1);

  EXPECT_EQ(simplified->GetChild(0), simplified->GetChild(1));
}

/*
Generated from the following GLSL + --eliminate-local-multi-store

#version 410 core
layout (location = 1) out float array[10];
layout (location = 2) flat in int loop_invariant;
void main() {
  for (int i = 0; i < 10; ++i) {
    array[i] = array[i+loop_invariant];
  }
}

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

  ir::Instruction* access_chain =
      context->get_def_use_mgr()->GetDef(load->GetSingleWordInOperand(0));

  ir::Instruction* child = context->get_def_use_mgr()->GetDef(
      access_chain->GetSingleWordInOperand(1));
  //  const opt::SENode* node =
  //  analysis.GetNodeFromInstruction(child->unique_id());

  const opt::SENode* node = analysis.AnalyzeInstruction(child);

  EXPECT_TRUE(node);

  // Unsimplified node should have the form of ADD(REC(0,1), X)
  EXPECT_TRUE(node->GetType() == opt::SENode::Add);

  const opt::SENode* child_1 = node->GetChild(0);
  EXPECT_TRUE(child_1->GetType() == opt::SENode::ValueUnknown ||
              child_1->GetType() == opt::SENode::RecurrentExpr);

  const opt::SENode* child_2 = node->GetChild(1);
  EXPECT_TRUE(child_2->GetType() == opt::SENode::ValueUnknown ||
              child_2->GetType() == opt::SENode::RecurrentExpr);

  opt::SENode* simplified =
      analysis.SimplifyExpression(const_cast<opt::SENode*>(node));
  EXPECT_TRUE(simplified->GetType() == opt::SENode::RecurrentExpr);

  const opt::SERecurrentNode* rec = simplified->AsSERecurrentNode();

  EXPECT_NE(rec->GetChild(0), rec->GetChild(1));

  EXPECT_TRUE(rec->GetOffset()->GetType() == opt::SENode::ValueUnknown);

  EXPECT_TRUE(rec->GetCoefficient()->GetType() == opt::SENode::Constant);
  EXPECT_TRUE(rec->GetCoefficient()->FoldToSingleValue() == 1);
}

/*
Generated from the following GLSL + --eliminate-local-multi-store

#version 410 core
layout (location = 1) out float array[10];
layout (location = 2) flat in int loop_invariant;
void main() {
  array[0] = array[loop_invariant * 2 + 4 + 5 - 24 - loop_invariant -
loop_invariant+ 16 * 3];
}

*/
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
          %7 = OpTypeFloat 32
          %8 = OpTypeInt 32 0
          %9 = OpConstant %8 10
         %10 = OpTypeArray %7 %9
         %11 = OpTypePointer Output %10
          %3 = OpVariable %11 Output
         %12 = OpTypeInt 32 1
         %13 = OpConstant %12 0
         %14 = OpTypePointer Input %12
          %4 = OpVariable %14 Input
         %15 = OpConstant %12 2
         %16 = OpConstant %12 4
         %17 = OpConstant %12 5
         %18 = OpConstant %12 24
         %19 = OpConstant %12 48
         %20 = OpTypePointer Output %7
          %2 = OpFunction %5 None %6
         %21 = OpLabel
         %22 = OpLoad %12 %4
         %23 = OpIMul %12 %22 %15
         %24 = OpIAdd %12 %23 %16
         %25 = OpIAdd %12 %24 %17
         %26 = OpISub %12 %25 %18
         %27 = OpLoad %12 %4
         %28 = OpISub %12 %26 %27
         %29 = OpLoad %12 %4
         %30 = OpISub %12 %28 %29
         %31 = OpIAdd %12 %30 %19
         %32 = OpAccessChain %20 %3 %31
         %33 = OpLoad %7 %32
         %34 = OpAccessChain %20 %3 %13
               OpStore %34 %33
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
  opt::ScalarEvolutionAnalysis analysis{context.get()};

  const ir::Instruction* load = nullptr;
  for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 21)) {
    if (inst.opcode() == SpvOp::SpvOpLoad && inst.result_id() == 33) {
      load = &inst;
    }
  }

  EXPECT_TRUE(load);

  ir::Instruction* access_chain =
      context->get_def_use_mgr()->GetDef(load->GetSingleWordInOperand(0));

  ir::Instruction* child = context->get_def_use_mgr()->GetDef(
      access_chain->GetSingleWordInOperand(1));

  const opt::SENode* node = analysis.AnalyzeInstruction(child);

  // Unsimplified is a very large graph with an add at the top.
  EXPECT_TRUE(node);
  EXPECT_TRUE(node->GetType() == opt::SENode::Add);

  // Simplified node should resolve down to a constant expression as the loads
  // will eliminate themselves.
  opt::SENode* simplified =
      analysis.SimplifyExpression(const_cast<opt::SENode*>(node));
  EXPECT_TRUE(simplified->GetType() == opt::SENode::Constant);
  EXPECT_TRUE(simplified->FoldToSingleValue() == 33);
}

}  // namespace
