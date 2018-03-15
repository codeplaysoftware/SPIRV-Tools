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
  EXPECT_TRUE(
      simplified->GetChild(0)->AsSEConstantNode()->FoldToSingleValue() == 1);

  EXPECT_TRUE(simplified->GetChild(1)->GetType() == opt::SENode::Constant);
  EXPECT_TRUE(
      simplified->GetChild(1)->AsSEConstantNode()->FoldToSingleValue() == 1);

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
  EXPECT_TRUE(rec->GetCoefficient()->AsSEConstantNode()->FoldToSingleValue() ==
              1);
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
  EXPECT_TRUE(simplified->AsSEConstantNode()->FoldToSingleValue() == 33);
}

/*
Generated from the following GLSL + --eliminate-local-multi-store

#version 410 core
layout(location = 0) in vec4 c;
layout (location = 1) out float array[10];
void main() {
  int N = int(c.x);
  for (int i = 0; i < 10; ++i) {
    array[i] = array[i];
    array[i] = array[i-1];
    array[i] = array[i+1];
    array[i+1] = array[i+1];
    array[i+N] = array[i+N];
    array[i] = array[i+N];
  }
}

*/
TEST_F(PassClassTest, Simplify) {
  const std::string text = R"(               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main" %12 %33
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 410
               OpName %4 "main"
               OpName %8 "N"
               OpName %12 "c"
               OpName %19 "i"
               OpName %33 "array"
               OpDecorate %12 Location 0
               OpDecorate %33 Location 1
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
         %27 = OpConstant %6 10
         %28 = OpTypeBool
         %30 = OpConstant %13 10
         %31 = OpTypeArray %9 %30
         %32 = OpTypePointer Output %31
         %33 = OpVariable %32 Output
         %36 = OpTypePointer Output %9
         %42 = OpConstant %6 1
          %4 = OpFunction %2 None %3
          %5 = OpLabel
          %8 = OpVariable %7 Function
         %19 = OpVariable %7 Function
         %16 = OpAccessChain %15 %12 %14
         %17 = OpLoad %9 %16
         %18 = OpConvertFToS %6 %17
               OpStore %8 %18
               OpStore %19 %20
               OpBranch %21
         %21 = OpLabel
         %78 = OpPhi %6 %20 %5 %77 %24
               OpLoopMerge %23 %24 None
               OpBranch %25
         %25 = OpLabel
         %29 = OpSLessThan %28 %78 %27
               OpBranchConditional %29 %22 %23
         %22 = OpLabel
         %37 = OpAccessChain %36 %33 %78
         %38 = OpLoad %9 %37
         %39 = OpAccessChain %36 %33 %78
               OpStore %39 %38
         %43 = OpISub %6 %78 %42
         %44 = OpAccessChain %36 %33 %43
         %45 = OpLoad %9 %44
         %46 = OpAccessChain %36 %33 %78
               OpStore %46 %45
         %49 = OpIAdd %6 %78 %42
         %50 = OpAccessChain %36 %33 %49
         %51 = OpLoad %9 %50
         %52 = OpAccessChain %36 %33 %78
               OpStore %52 %51
         %54 = OpIAdd %6 %78 %42
         %56 = OpIAdd %6 %78 %42
         %57 = OpAccessChain %36 %33 %56
         %58 = OpLoad %9 %57
         %59 = OpAccessChain %36 %33 %54
               OpStore %59 %58
         %62 = OpIAdd %6 %78 %18
         %65 = OpIAdd %6 %78 %18
         %66 = OpAccessChain %36 %33 %65
         %67 = OpLoad %9 %66
         %68 = OpAccessChain %36 %33 %62
               OpStore %68 %67
         %72 = OpIAdd %6 %78 %18
         %73 = OpAccessChain %36 %33 %72
         %74 = OpLoad %9 %73
         %75 = OpAccessChain %36 %33 %78
               OpStore %75 %74
               OpBranch %24
         %24 = OpLabel
         %77 = OpIAdd %6 %78 %42
               OpStore %19 %77
               OpBranch %21
         %23 = OpLabel
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

  const ir::Instruction* loads[6];
  const ir::Instruction* stores[6];
  int load_count = 0;
  int store_count = 0;

  for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 22)) {
    if (inst.opcode() == SpvOp::SpvOpLoad) {
      loads[load_count] = &inst;
      ++load_count;
    }
    if (inst.opcode() == SpvOp::SpvOpStore) {
      stores[store_count] = &inst;
      ++store_count;
    }
  }

  EXPECT_EQ(load_count, 6);
  EXPECT_EQ(store_count, 6);

  ir::Instruction* load_access_chain;
  ir::Instruction* store_access_chain;
  ir::Instruction* load_child;
  ir::Instruction* store_child;
  opt::SENode* load_node;
  opt::SENode* store_node;
  opt::SENode* subtract_node;
  opt::SENode* simplified_node;

  // Testing [i] - [i] == 0
  load_access_chain =
      context->get_def_use_mgr()->GetDef(loads[0]->GetSingleWordInOperand(0));
  store_access_chain =
      context->get_def_use_mgr()->GetDef(stores[0]->GetSingleWordInOperand(0));

  load_child = context->get_def_use_mgr()->GetDef(
      load_access_chain->GetSingleWordInOperand(1));
  store_child = context->get_def_use_mgr()->GetDef(
      store_access_chain->GetSingleWordInOperand(1));

  load_node = analysis.AnalyzeInstruction(load_child);
  store_node = analysis.AnalyzeInstruction(store_child);

  subtract_node = analysis.CreateSubtraction(store_node, load_node);
  simplified_node = analysis.SimplifyExpression(subtract_node);
  EXPECT_TRUE(simplified_node->GetType() == opt::SENode::Constant);
  EXPECT_TRUE(simplified_node->AsSEConstantNode()->FoldToSingleValue() == 0);

  // Testing [i] - [i-1] == 1
  load_access_chain =
      context->get_def_use_mgr()->GetDef(loads[1]->GetSingleWordInOperand(0));
  store_access_chain =
      context->get_def_use_mgr()->GetDef(stores[1]->GetSingleWordInOperand(0));

  load_child = context->get_def_use_mgr()->GetDef(
      load_access_chain->GetSingleWordInOperand(1));
  store_child = context->get_def_use_mgr()->GetDef(
      store_access_chain->GetSingleWordInOperand(1));

  load_node = analysis.AnalyzeInstruction(load_child);
  store_node = analysis.AnalyzeInstruction(store_child);

  subtract_node = analysis.CreateSubtraction(store_node, load_node);
  simplified_node = analysis.SimplifyExpression(subtract_node);

  EXPECT_TRUE(simplified_node->GetType() == opt::SENode::Constant);
  EXPECT_TRUE(simplified_node->AsSEConstantNode()->FoldToSingleValue() == 1);

  // Testing [i] - [i+1] == -1
  load_access_chain =
      context->get_def_use_mgr()->GetDef(loads[2]->GetSingleWordInOperand(0));
  store_access_chain =
      context->get_def_use_mgr()->GetDef(stores[2]->GetSingleWordInOperand(0));

  load_child = context->get_def_use_mgr()->GetDef(
      load_access_chain->GetSingleWordInOperand(1));
  store_child = context->get_def_use_mgr()->GetDef(
      store_access_chain->GetSingleWordInOperand(1));

  load_node = analysis.AnalyzeInstruction(load_child);
  store_node = analysis.AnalyzeInstruction(store_child);

  subtract_node = analysis.CreateSubtraction(store_node, load_node);
  simplified_node = analysis.SimplifyExpression(subtract_node);
  EXPECT_TRUE(simplified_node->GetType() == opt::SENode::Constant);
  EXPECT_TRUE(simplified_node->AsSEConstantNode()->FoldToSingleValue() == -1);

  // Testing [i+1] - [i+1] == 0
  load_access_chain =
      context->get_def_use_mgr()->GetDef(loads[3]->GetSingleWordInOperand(0));
  store_access_chain =
      context->get_def_use_mgr()->GetDef(stores[3]->GetSingleWordInOperand(0));

  load_child = context->get_def_use_mgr()->GetDef(
      load_access_chain->GetSingleWordInOperand(1));
  store_child = context->get_def_use_mgr()->GetDef(
      store_access_chain->GetSingleWordInOperand(1));

  load_node = analysis.AnalyzeInstruction(load_child);
  store_node = analysis.AnalyzeInstruction(store_child);

  subtract_node = analysis.CreateSubtraction(store_node, load_node);
  simplified_node = analysis.SimplifyExpression(subtract_node);
  EXPECT_TRUE(simplified_node->GetType() == opt::SENode::Constant);
  EXPECT_TRUE(simplified_node->AsSEConstantNode()->FoldToSingleValue() == 0);

  // Testing [i+N] - [i+N] == 0
  load_access_chain =
      context->get_def_use_mgr()->GetDef(loads[4]->GetSingleWordInOperand(0));
  store_access_chain =
      context->get_def_use_mgr()->GetDef(stores[4]->GetSingleWordInOperand(0));

  load_child = context->get_def_use_mgr()->GetDef(
      load_access_chain->GetSingleWordInOperand(1));
  store_child = context->get_def_use_mgr()->GetDef(
      store_access_chain->GetSingleWordInOperand(1));

  load_node = analysis.AnalyzeInstruction(load_child);
  store_node = analysis.AnalyzeInstruction(store_child);

  subtract_node = analysis.CreateSubtraction(store_node, load_node);

  simplified_node = analysis.SimplifyExpression(subtract_node);
  EXPECT_TRUE(simplified_node->GetType() == opt::SENode::Constant);
  EXPECT_TRUE(simplified_node->AsSEConstantNode()->FoldToSingleValue() == 0);

  // Testing [i] - [i+N] == -N
  load_access_chain =
      context->get_def_use_mgr()->GetDef(loads[5]->GetSingleWordInOperand(0));
  store_access_chain =
      context->get_def_use_mgr()->GetDef(stores[5]->GetSingleWordInOperand(0));

  load_child = context->get_def_use_mgr()->GetDef(
      load_access_chain->GetSingleWordInOperand(1));
  store_child = context->get_def_use_mgr()->GetDef(
      store_access_chain->GetSingleWordInOperand(1));

  load_node = analysis.AnalyzeInstruction(load_child);
  store_node = analysis.AnalyzeInstruction(store_child);

  subtract_node = analysis.CreateSubtraction(store_node, load_node);
  simplified_node = analysis.SimplifyExpression(subtract_node);
  EXPECT_TRUE(simplified_node->GetType() == opt::SENode::Negative);
}

/*
Generated from the following GLSL + --eliminate-local-multi-store

#version 430
layout(location = 1) out float array[10];
layout(location = 2) flat in int loop_invariant;
void main(void) {
  for (int i = 0; i < 10; ++i) {
    array[i * 2 + i * 5] = array[i * i * 2];
    array[i * 2] = array[i * 5];
  }
}

*/

TEST_F(PassClassTest, SimplifyMultiplyInductions) {
  const std::string text = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main" %3 %4
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 430
               OpName %2 "main"
               OpName %5 "i"
               OpName %3 "array"
               OpName %4 "loop_invariant"
               OpDecorate %3 Location 1
               OpDecorate %4 Flat
               OpDecorate %4 Location 2
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
         %17 = OpTypePointer Output %16
          %3 = OpVariable %17 Output
         %18 = OpConstant %8 2
         %19 = OpConstant %8 5
         %20 = OpTypePointer Output %13
         %21 = OpConstant %8 1
         %22 = OpTypePointer Input %8
          %4 = OpVariable %22 Input
          %2 = OpFunction %6 None %7
         %23 = OpLabel
          %5 = OpVariable %9 Function
               OpStore %5 %10
               OpBranch %24
         %24 = OpLabel
         %25 = OpPhi %8 %10 %23 %26 %27
               OpLoopMerge %28 %27 None
               OpBranch %29
         %29 = OpLabel
         %30 = OpSLessThan %12 %25 %11
               OpBranchConditional %30 %31 %28
         %31 = OpLabel
         %32 = OpIMul %8 %25 %18
         %33 = OpIMul %8 %25 %19
         %34 = OpIAdd %8 %32 %33
         %35 = OpIMul %8 %25 %25
         %36 = OpIMul %8 %35 %18
         %37 = OpAccessChain %20 %3 %36
         %38 = OpLoad %13 %37
         %39 = OpAccessChain %20 %3 %34
               OpStore %39 %38
         %40 = OpIMul %8 %25 %18
         %41 = OpIMul %8 %25 %19
         %42 = OpAccessChain %20 %3 %41
         %43 = OpLoad %13 %42
         %44 = OpAccessChain %20 %3 %40
               OpStore %44 %43
               OpBranch %27
         %27 = OpLabel
         %26 = OpIAdd %8 %25 %21
               OpStore %5 %26
               OpBranch %24
         %28 = OpLabel
               OpReturn
               OpFunctionEnd
    )";
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << text << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 2);
  opt::ScalarEvolutionAnalysis analysis{context.get()};

  const ir::Instruction* loads[2] = {nullptr, nullptr};
  const ir::Instruction* stores[2] = {nullptr, nullptr};
  int load_count = 0;
  int store_count = 0;

  for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 31)) {
    if (inst.opcode() == SpvOp::SpvOpLoad) {
      loads[load_count] = &inst;
      ++load_count;
    }
    if (inst.opcode() == SpvOp::SpvOpStore) {
      stores[store_count] = &inst;
      ++store_count;
    }
  }

  EXPECT_EQ(load_count, 2);
  EXPECT_EQ(store_count, 2);

  ir::Instruction* load_access_chain =
      context->get_def_use_mgr()->GetDef(loads[0]->GetSingleWordInOperand(0));
  ir::Instruction* store_access_chain =
      context->get_def_use_mgr()->GetDef(stores[0]->GetSingleWordInOperand(0));

  ir::Instruction* load_child = context->get_def_use_mgr()->GetDef(
      load_access_chain->GetSingleWordInOperand(1));
  ir::Instruction* store_child = context->get_def_use_mgr()->GetDef(
      store_access_chain->GetSingleWordInOperand(1));

  opt::SENode* store_node = analysis.AnalyzeInstruction(store_child);

  opt::SENode* store_simplified = analysis.SimplifyExpression(store_node);

  load_access_chain =
      context->get_def_use_mgr()->GetDef(loads[1]->GetSingleWordInOperand(0));
  store_access_chain =
      context->get_def_use_mgr()->GetDef(stores[1]->GetSingleWordInOperand(0));
  load_child = context->get_def_use_mgr()->GetDef(
      load_access_chain->GetSingleWordInOperand(1));
  store_child = context->get_def_use_mgr()->GetDef(
      store_access_chain->GetSingleWordInOperand(1));

  opt::SENode* second_store =
      analysis.SimplifyExpression(analysis.AnalyzeInstruction(store_child));
  opt::SENode* second_load =
      analysis.SimplifyExpression(analysis.AnalyzeInstruction(load_child));
  opt::SENode* combined_add = analysis.SimplifyExpression(
      analysis.CreateAddNode(second_load, second_store));

  // We're checking that the two recurrent expression have been correctly
  // folded. In store_simplified they will have been folded as the entire
  // expression was simplified as one. In combined_add the two expressions have
  // been simplified one after the other which means the recurrent expressions
  // aren't exactly the same but should still be folded as they are with respect
  // to the same loop.
  EXPECT_EQ(combined_add, store_simplified);
}

/*
Generated from the following GLSL + --eliminate-local-multi-store

#version 430
void main(void) {
    for (int i = 0; i < 10; --i) {
        array[i] = array[i];
    }
}

*/

TEST_F(PassClassTest, SimplifyNegativeSteps) {
  const std::string text = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main" %3 %4
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 430
               OpName %2 "main"
               OpName %5 "i"
               OpName %3 "array"
               OpName %4 "loop_invariant"
               OpDecorate %3 Location 1
               OpDecorate %4 Flat
               OpDecorate %4 Location 2
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
         %17 = OpTypePointer Output %16
          %3 = OpVariable %17 Output
         %18 = OpTypePointer Output %13
         %19 = OpConstant %8 1
         %20 = OpTypePointer Input %8
          %4 = OpVariable %20 Input
          %2 = OpFunction %6 None %7
         %21 = OpLabel
          %5 = OpVariable %9 Function
               OpStore %5 %10
               OpBranch %22
         %22 = OpLabel
         %23 = OpPhi %8 %10 %21 %24 %25
               OpLoopMerge %26 %25 None
               OpBranch %27
         %27 = OpLabel
         %28 = OpSLessThan %12 %23 %11
               OpBranchConditional %28 %29 %26
         %29 = OpLabel
         %30 = OpAccessChain %18 %3 %23
         %31 = OpLoad %13 %30
         %32 = OpAccessChain %18 %3 %23
               OpStore %32 %31
               OpBranch %25
         %25 = OpLabel
         %24 = OpISub %8 %23 %19
               OpStore %5 %24
               OpBranch %22
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
  const ir::Function* f = spvtest::GetFunction(module, 2);
  opt::ScalarEvolutionAnalysis analysis{context.get()};

  const ir::Instruction* loads[1] = {nullptr};
  int load_count = 0;

  for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, 29)) {
    if (inst.opcode() == SpvOp::SpvOpLoad) {
      loads[load_count] = &inst;
      ++load_count;
    }
  }

  EXPECT_EQ(load_count, 1);

  ir::Instruction* load_access_chain =
      context->get_def_use_mgr()->GetDef(loads[0]->GetSingleWordInOperand(0));
  ir::Instruction* load_child = context->get_def_use_mgr()->GetDef(
      load_access_chain->GetSingleWordInOperand(1));

  opt::SENode* load_node = analysis.AnalyzeInstruction(load_child);

  EXPECT_TRUE(load_node);
  EXPECT_EQ(load_node->GetType(), opt::SENode::RecurrentExpr);
  EXPECT_TRUE(load_node->AsSERecurrentNode());

  opt::SENode* child_1 = load_node->AsSERecurrentNode()->GetCoefficient();
  opt::SENode* child_2 = load_node->AsSERecurrentNode()->GetOffset();

  EXPECT_EQ(child_1->GetType(), opt::SENode::Constant);
  EXPECT_EQ(child_2->GetType(), opt::SENode::Constant);

  EXPECT_EQ(child_1->AsSEConstantNode()->FoldToSingleValue(), -1);
  EXPECT_EQ(child_2->AsSEConstantNode()->FoldToSingleValue(), 0);

  opt::SERecurrentNode* load_simplified =
      analysis.SimplifyExpression(load_node)->AsSERecurrentNode();

  EXPECT_TRUE(load_simplified);
  EXPECT_EQ(load_node, load_simplified);

  EXPECT_EQ(load_simplified->GetType(), opt::SENode::RecurrentExpr);
  EXPECT_TRUE(load_simplified->AsSERecurrentNode());

  opt::SENode* simplified_child_1 =
      load_simplified->AsSERecurrentNode()->GetCoefficient();
  opt::SENode* simplified_child_2 =
      load_simplified->AsSERecurrentNode()->GetOffset();

  EXPECT_EQ(child_1, simplified_child_1);
  EXPECT_EQ(child_2, simplified_child_2);
}

opt::SENode* GetAnalysisFromInstruction(
    const ir::Instruction* instruction, ir::IRContext* context,
    opt::ScalarEvolutionAnalysis* analysis) {
  ir::Instruction* access_chain = context->get_def_use_mgr()->GetDef(
      instruction->GetSingleWordInOperand(0));

  ir::Instruction* child = context->get_def_use_mgr()->GetDef(
      access_chain->GetSingleWordInOperand(1));

  opt::SENode* node = analysis->AnalyzeInstruction(child);

  return node;
}

void CheckNumberOfVariables(const ir::Instruction* instruction,
                            int expected_count, ir::IRContext* context,
                            opt::ScalarEvolutionAnalysis* analysis) {
  opt::SENode* node =
      GetAnalysisFromInstruction(instruction, context, analysis);
  auto variable_nodes = node->CollectRecurrentNodes();

  EXPECT_EQ(expected_count, variable_nodes.size());

  opt::SENode* simplified = analysis->SimplifyExpression(node);
  variable_nodes = simplified->CollectRecurrentNodes();
  EXPECT_EQ(expected_count, variable_nodes.size());
}

/*
Generated from the following GLSL + --eliminate-local-multi-store

#version 440 core
void main() {
  int[100] array;
  for (int i = 0; i < 10; ++i) {
    for (int j = 0; j < 10; ++j) {
      for (int k = 0; k < 10; ++k) {
        array[i] = array[i];
        array[j] = array[j];
        array[k] = array[k];
        array[i+j] = array[i+j];
        array[i+j+k] = array[i+j+k];
        array[i+j+k+10] = array[i+j+k+10];
        array[2*i+4*j+3*k+10] = array[2*i+4*j+3*k+10];
        array[10*i+j] = array[10*i+j];
      }
    }
  }
}
*/
TEST_F(PassClassTest, CollectAllInductionVariables) {
  const std::string text = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %4 "main"
               OpExecutionMode %4 OriginUpperLeft
               OpSource GLSL 440
               OpName %4 "main"
               OpName %8 "i"
               OpName %19 "j"
               OpName %27 "k"
               OpName %39 "array"
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %6 = OpTypeInt 32 1
          %7 = OpTypePointer Function %6
          %9 = OpConstant %6 0
         %16 = OpConstant %6 10
         %17 = OpTypeBool
         %35 = OpTypeInt 32 0
         %36 = OpConstant %35 100
         %37 = OpTypeArray %6 %36
         %38 = OpTypePointer Function %37
         %92 = OpConstant %6 2
         %95 = OpConstant %6 4
         %99 = OpConstant %6 3
        %128 = OpConstant %6 1
        %135 = OpUndef %6
          %4 = OpFunction %2 None %3
          %5 = OpLabel
          %8 = OpVariable %7 Function
         %19 = OpVariable %7 Function
         %27 = OpVariable %7 Function
         %39 = OpVariable %38 Function
               OpStore %8 %9
               OpBranch %10
         %10 = OpLabel
        %134 = OpPhi %6 %9 %5 %133 %13
        %136 = OpPhi %6 %135 %5 %138 %13
        %137 = OpPhi %6 %135 %5 %139 %13
               OpLoopMerge %12 %13 None
               OpBranch %14
         %14 = OpLabel
         %18 = OpSLessThan %17 %134 %16
               OpBranchConditional %18 %11 %12
         %11 = OpLabel
               OpStore %19 %9
               OpBranch %20
         %20 = OpLabel
        %138 = OpPhi %6 %9 %11 %131 %23
        %139 = OpPhi %6 %137 %11 %140 %23
               OpLoopMerge %22 %23 None
               OpBranch %24
         %24 = OpLabel
         %26 = OpSLessThan %17 %138 %16
               OpBranchConditional %26 %21 %22
         %21 = OpLabel
               OpStore %27 %9
               OpBranch %28
         %28 = OpLabel
        %140 = OpPhi %6 %9 %21 %129 %31
               OpLoopMerge %30 %31 None
               OpBranch %32
         %32 = OpLabel
         %34 = OpSLessThan %17 %140 %16
               OpBranchConditional %34 %29 %30
         %29 = OpLabel
         %42 = OpAccessChain %7 %39 %134
         %43 = OpLoad %6 %42
         %44 = OpAccessChain %7 %39 %134
               OpStore %44 %43
         %47 = OpAccessChain %7 %39 %138
         %48 = OpLoad %6 %47
         %49 = OpAccessChain %7 %39 %138
               OpStore %49 %48
         %52 = OpAccessChain %7 %39 %140
         %53 = OpLoad %6 %52
         %54 = OpAccessChain %7 %39 %140
               OpStore %54 %53
         %57 = OpIAdd %6 %134 %138
         %60 = OpIAdd %6 %134 %138
         %61 = OpAccessChain %7 %39 %60
         %62 = OpLoad %6 %61
         %63 = OpAccessChain %7 %39 %57
               OpStore %63 %62
         %66 = OpIAdd %6 %134 %138
         %68 = OpIAdd %6 %66 %140
         %71 = OpIAdd %6 %134 %138
         %73 = OpIAdd %6 %71 %140
         %74 = OpAccessChain %7 %39 %73
         %75 = OpLoad %6 %74
         %76 = OpAccessChain %7 %39 %68
               OpStore %76 %75
         %79 = OpIAdd %6 %134 %138
         %81 = OpIAdd %6 %79 %140
         %82 = OpIAdd %6 %81 %16
         %85 = OpIAdd %6 %134 %138
         %87 = OpIAdd %6 %85 %140
         %88 = OpIAdd %6 %87 %16
         %89 = OpAccessChain %7 %39 %88
         %90 = OpLoad %6 %89
         %91 = OpAccessChain %7 %39 %82
               OpStore %91 %90
         %94 = OpIMul %6 %92 %134
         %97 = OpIMul %6 %95 %138
         %98 = OpIAdd %6 %94 %97
        %101 = OpIMul %6 %99 %140
        %102 = OpIAdd %6 %98 %101
        %103 = OpIAdd %6 %102 %16
        %105 = OpIMul %6 %92 %134
        %107 = OpIMul %6 %95 %138
        %108 = OpIAdd %6 %105 %107
        %110 = OpIMul %6 %99 %140
        %111 = OpIAdd %6 %108 %110
        %112 = OpIAdd %6 %111 %16
        %113 = OpAccessChain %7 %39 %112
        %114 = OpLoad %6 %113
        %115 = OpAccessChain %7 %39 %103
               OpStore %115 %114
        %117 = OpIMul %6 %16 %134
        %119 = OpIAdd %6 %117 %138
        %121 = OpIMul %6 %16 %134
        %123 = OpIAdd %6 %121 %138
        %124 = OpAccessChain %7 %39 %123
        %125 = OpLoad %6 %124
        %126 = OpAccessChain %7 %39 %119
               OpStore %126 %125
               OpBranch %31
         %31 = OpLabel
        %129 = OpIAdd %6 %140 %128
               OpStore %27 %129
               OpBranch %28
         %30 = OpLabel
               OpBranch %23
         %23 = OpLabel
        %131 = OpIAdd %6 %138 %128
               OpStore %19 %131
               OpBranch %20
         %22 = OpLabel
               OpBranch %13
         %13 = OpLabel
        %133 = OpIAdd %6 %134 %128
               OpStore %8 %133
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
  EXPECT_NE(nullptr, f);
  opt::ScalarEvolutionAnalysis analysis{context.get()};

  constexpr int expected_instructions = 8;
  const ir::Instruction* loads[expected_instructions];
  int load_count = 0;

  int block_id = 29;
  for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f, block_id)) {
    if (inst.opcode() == SpvOp::SpvOpLoad) {
      loads[load_count] = &inst;
      ++load_count;
    }
  }

  EXPECT_EQ(expected_instructions, load_count);

  CheckNumberOfVariables(loads[0], 1, context.get(), &analysis);
  CheckNumberOfVariables(loads[1], 1, context.get(), &analysis);
  CheckNumberOfVariables(loads[2], 1, context.get(), &analysis);
  CheckNumberOfVariables(loads[3], 2, context.get(), &analysis);
  CheckNumberOfVariables(loads[4], 3, context.get(), &analysis);
  CheckNumberOfVariables(loads[5], 3, context.get(), &analysis);
  CheckNumberOfVariables(loads[6], 3, context.get(), &analysis);
  CheckNumberOfVariables(loads[7], 2, context.get(), &analysis);
}

}  // namespace
