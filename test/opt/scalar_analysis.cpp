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
#include "opt/tree_iterator.h"
#include "opt/scalar_analysis.h"

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

    opt::ScalarEvolutionAnalysis analysis { context.get() };
  //opt::LoopDependenceAnalysis analysis {context.get(), ld.etLoopByIndex(0)};
 // analysis.DumpIterationSpaceAsDot(std::cout);

  const ir::Instruction* store = nullptr;
  for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f,11)) {
    if (inst.opcode() == SpvOp::SpvOpStore) {
      store = &inst;
    }
  }

  EXPECT_TRUE(store);

	analysis.AnalyzeLoop(ld.GetLoopByIndex(0));
analysis.DumpAsDot(std::cout);


  ir::Instruction* access_chain = context->get_def_use_mgr()->GetDef(
                    store->GetSingleWordInOperand(0));

  ir::Instruction* child =  context->get_def_use_mgr()->GetDef(access_chain->GetSingleWordInOperand(1));
  const opt::SENode* node = analysis.GetNodeFromInstruction(child->unique_id());

  EXPECT_TRUE(node);
  EXPECT_TRUE(node->CanFoldToConstant());
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

    opt::ScalarEvolutionAnalysis analysis { context.get() };
  //opt::LoopDependenceAnalysis analysis {context.get(), ld.etLoopByIndex(0)};
 // analysis.DumpIterationSpaceAsDot(std::cout);

  const ir::Instruction* load = nullptr;
  for (const ir::Instruction& inst : *spvtest::GetBasicBlock(f,28)) {
    if (inst.opcode() == SpvOp::SpvOpLoad) {
      load = &inst;
    }
  }

  EXPECT_TRUE(load);

	analysis.AnalyzeLoop(ld.GetLoopByIndex(0));
  analysis.DumpAsDot(std::cout);


  ir::Instruction* access_chain = context->get_def_use_mgr()->GetDef(
                    load->GetSingleWordInOperand(0));

  ir::Instruction* child =  context->get_def_use_mgr()->GetDef(access_chain->GetSingleWordInOperand(1));
  const opt::SENode* node = analysis.GetNodeFromInstruction(child->unique_id());

  EXPECT_TRUE(node);
  EXPECT_FALSE(node->CanFoldToConstant());
  // EXPECT_FALSE(
 //     analysis.GetDependence(store, context->get_def_use_mgr()->GetDef(31)));
 // analysis.DumpIterationSpaceAsDot(std::cout);
}





}  // namespace
