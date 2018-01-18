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

#include <memory>
#include <string>
#include <vector>

#ifdef SPIRV_EFFCEE
#include "effcee/effcee.h"
#endif

#include "../assembly_builder.h"
#include "../function_utils.h"

#include "opt/build_module.h"
#include "opt/loop_descriptor.h"
#include "opt/pass.h"

namespace {

using namespace spvtools;

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
      tools.Disassemble(bin, &assembly, SPV_BINARY_TO_TEXT_OPTION_NO_HEADER))
      << "Disassembling failed for shader:\n"
      << assembly << std::endl;
  auto match_result = effcee::Match(assembly, original);
  EXPECT_EQ(effcee::Result::Status::Ok, match_result.status())
      << match_result.message() << "\nChecking result:\n"
      << assembly;
}

using LCSSATest = ::testing::Test;

/*
Generated from the following GLSL + --eliminate-local-multi-store

#version 330 core
layout(location = 0) out vec4 c;
void main() {
  int i = 0;
  for (; i < 10; i++) {
  }
  if (i != 0) {
    i = 1;
  }
}
*/
TEST_F(LCSSATest, SimpleLCSSA) {
  const std::string text = R"(
; CHECK: %18 = OpLabel
; CHECK-NEXT: %32 = OpPhi %7 %30 %20
; CHECK-NEXT: %27 = OpINotEqual %11 %32 %9
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main" %3
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 330
               OpName %2 "main"
               OpName %3 "c"
               OpDecorate %3 Location 0
          %5 = OpTypeVoid
          %6 = OpTypeFunction %5
          %7 = OpTypeInt 32 1
          %8 = OpTypePointer Function %7
          %9 = OpConstant %7 0
         %10 = OpConstant %7 10
         %11 = OpTypeBool
         %12 = OpConstant %7 1
         %13 = OpTypeFloat 32
         %14 = OpTypeVector %13 4
         %15 = OpTypePointer Output %14
          %3 = OpVariable %15 Output
          %2 = OpFunction %5 None %6
         %16 = OpLabel
               OpBranch %17
         %17 = OpLabel
         %30 = OpPhi %7 %9 %16 %25 %19
               OpLoopMerge %18 %19 None
               OpBranch %20
         %20 = OpLabel
         %22 = OpSLessThan %11 %30 %10
               OpBranchConditional %22 %23 %18
         %23 = OpLabel
               OpBranch %19
         %19 = OpLabel
         %25 = OpIAdd %7 %30 %12
               OpBranch %17
         %18 = OpLabel
         %27 = OpINotEqual %11 %30 %9
               OpSelectionMerge %28 None
               OpBranchConditional %27 %29 %28
         %29 = OpLabel
               OpBranch %28
         %28 = OpLabel
         %31 = OpPhi %7 %30 %18 %12 %29
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
  ir::LoopDescriptor ld{f};

  ir::LoopUtils Util(context.get(), ld[17]);
  Util.MakeLoopClosedSSA();
  Match(text, context.get());
}

#endif  // SPIRV_EFFCEE

}  // namespace
