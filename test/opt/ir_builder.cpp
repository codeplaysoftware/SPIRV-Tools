// Copyright (c) 2018 Google Inc.
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
#include <gtest/gtest.h>
#include <algorithm>
#include <vector>

#ifdef SPIRV_EFFCEE
#include "effcee/effcee.h"
#endif

#include "opt/basic_block.h"
#include "opt/ir_builder.h"

#include "opt/build_module.h"
#include "opt/instruction.h"
#include "opt/type_manager.h"
#include "spirv-tools/libspirv.hpp"

namespace {

using namespace spvtools;
using ir::IRContext;
using Analysis = IRContext::Analysis;

#ifdef SPIRV_EFFCEE

bool Validate(const std::vector<uint32_t>& bin) {
  spv_target_env target_env = SPV_ENV_UNIVERSAL_1_2;
  spv_context spvContext = spvContextCreate(target_env);
  spv_diagnostic diagnostic = nullptr;
  spv_const_binary_t binary = {bin.data(), bin.size()};
  spv_result_t error = spvValidate(spvContext, &binary, &diagnostic);
  if (error != 0) spvDiagnosticPrint(diagnostic);
  spvDiagnosticDestroy(diagnostic);
  spvContextDestroy(spvContext);
  return error == 0;
}

void Match(const std::string& original, ir::IRContext* context,
           bool do_validation = true) {
  std::vector<uint32_t> bin;
  context->module()->ToBinary(&bin, true);
  if (do_validation) {
    EXPECT_TRUE(Validate(bin));
  }
  std::string assembly;
  SpirvTools tools(SPV_ENV_UNIVERSAL_1_2);
  EXPECT_TRUE(
      tools.Disassemble(bin, &assembly, SpirvTools::kDefaultDisassembleOption))
      << "Disassembling failed for shader:\n"
      << assembly << std::endl;
  auto match_result = effcee::Match(assembly, original);
  EXPECT_EQ(effcee::Result::Status::Ok, match_result.status())
      << match_result.message() << "\nChecking result:\n"
      << assembly;
}

TEST(IRBuilderTest, TestInsnAddition) {
  const std::string text = R"(
; CHECK: %18 = OpLabel
; CHECK: OpPhi %int %int_0 %14
; CHECK: OpPhi %bool %16 %14
; CHECK: OpBranch %17
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main" %3
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 330
               OpName %2 "main"
               OpName %4 "i"
               OpName %3 "c"
               OpDecorate %3 Location 0
          %5 = OpTypeVoid
          %6 = OpTypeFunction %5
          %7 = OpTypeInt 32 1
          %8 = OpTypePointer Function %7
          %9 = OpConstant %7 0
         %10 = OpTypeBool
         %11 = OpTypeFloat 32
         %12 = OpTypeVector %11 4
         %13 = OpTypePointer Output %12
          %3 = OpVariable %13 Output
          %2 = OpFunction %5 None %6
         %14 = OpLabel
          %4 = OpVariable %8 Function
               OpStore %4 %9
         %15 = OpLoad %7 %4
         %16 = OpINotEqual %10 %15 %9
               OpSelectionMerge %17 None
               OpBranchConditional %16 %18 %17
         %18 = OpLabel
               OpBranch %17
         %17 = OpLabel
               OpReturn
               OpFunctionEnd
)";

  {
    std::unique_ptr<ir::IRContext> context =
        BuildModule(SPV_ENV_UNIVERSAL_1_2, nullptr, text);

    ir::BasicBlock* bb = context->cfg()->block(18);

    // Build the def/use manager.
    context->get_def_use_mgr();

    opt::InstructionBuilder<> builder(context.get(), bb->begin());
    ir::Instruction* phi1 = builder.AddPhi(7, {9, 14});
    ir::Instruction* phi2 = builder.AddPhi(10, {16, 14});

    // Make sure the InstructionBuilder did not update the def/use manager.
    EXPECT_EQ(context->get_def_use_mgr()->GetDef(phi1->result_id()), nullptr);
    EXPECT_EQ(context->get_def_use_mgr()->GetDef(phi2->result_id()), nullptr);

    Match(text, context.get());
  }

  {
    std::unique_ptr<ir::IRContext> context =
        BuildModule(SPV_ENV_UNIVERSAL_1_2, nullptr, text);

    ir::BasicBlock* bb = context->cfg()->block(18);
    opt::InstructionBuilder<ir::IRContext::kAnalysisDefUse> builder(
        context.get(), bb->begin());
    ir::Instruction* phi1 = builder.AddPhi(7, {9, 14});
    ir::Instruction* phi2 = builder.AddPhi(10, {16, 14});

    // Make sure InstructionBuilder updated the def/use manager
    EXPECT_NE(context->get_def_use_mgr()->GetDef(phi1->result_id()), nullptr);
    EXPECT_NE(context->get_def_use_mgr()->GetDef(phi2->result_id()), nullptr);

    Match(text, context.get());
  }
}

#endif  // SPIRV_EFFCEE

}  // anonymous namespace
