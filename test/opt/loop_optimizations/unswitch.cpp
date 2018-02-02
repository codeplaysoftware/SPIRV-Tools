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
#include <vector>

#ifdef SPIRV_EFFCEE
#include "effcee/effcee.h"
#endif

#include "../assembly_builder.h"
#include "../function_utils.h"

#include "opt/build_module.h"
#include "opt/loop_descriptor.h"
#include "opt/loop_utils.h"
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

using UnswitchTest = ::testing::Test;

/*
Generated from the following GLSL + --eliminate-local-multi-store

#version 330 core
in vec4 c;
void main() {
  int i = 0;
  int j = 0;
  bool cond = c[0] == 0;
  for (; i < 10; i++, j++) {
    if (cond) {
      i++;
    }
    else {
      j++;
    }
  }
}
*/
TEST_F(UnswitchTest, SimpleUnswitch) {
  const std::string text = R"(
; CHECK: OpLoopMerge [[merge:%\w+]] %19 None
; CHECK: [[merge]] = OpLabel
; CHECK-NEXT: [[phi:%\w+]] = OpPhi {{%\w+}} %30 %20
; CHECK-NEXT: %27 = OpINotEqual {{%\w+}} [[phi]] %9
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %2 "main" %3
               OpExecutionMode %2 OriginUpperLeft
               OpSource GLSL 330
               OpName %2 "main"
               OpName %3 "c"
               OpDecorate %3 Location 0
          %4 = OpTypeVoid
          %5 = OpTypeFunction %4
          %6 = OpTypeInt 32 1
          %7 = OpTypePointer Function %6
          %8 = OpConstant %6 0
          %9 = OpTypeBool
         %10 = OpTypePointer Function %9
         %11 = OpTypeFloat 32
         %12 = OpTypeVector %11 4
         %13 = OpTypePointer Input %12
          %3 = OpVariable %13 Input
         %14 = OpTypeInt 32 0
         %15 = OpConstant %14 0
         %16 = OpTypePointer Input %11
         %17 = OpConstant %11 0
         %18 = OpConstant %6 10
         %19 = OpConstant %6 1
          %2 = OpFunction %4 None %5
         %20 = OpLabel
         %21 = OpAccessChain %16 %3 %15
         %22 = OpLoad %11 %21
         %23 = OpFOrdEqual %9 %22 %17
               OpBranch %24
         %24 = OpLabel
         %25 = OpPhi %6 %8 %20 %26 %27
         %28 = OpPhi %6 %8 %20 %29 %27
               OpLoopMerge %30 %27 None
               OpBranch %31
         %31 = OpLabel
         %32 = OpSLessThan %9 %25 %18
               OpBranchConditional %32 %33 %30
         %33 = OpLabel
               OpSelectionMerge %34 None
               OpBranchConditional %23 %35 %36
         %35 = OpLabel
         %37 = OpIAdd %6 %25 %19
               OpBranch %34
         %36 = OpLabel
         %38 = OpIAdd %6 %28 %19
               OpBranch %34
         %34 = OpLabel
         %39 = OpPhi %6 %37 %35 %25 %36
         %40 = OpPhi %6 %28 %35 %38 %36
               OpBranch %27
         %27 = OpLabel
         %26 = OpIAdd %6 %39 %19
         %29 = OpIAdd %6 %40 %19
               OpBranch %24
         %30 = OpLabel
               OpReturn
               OpFunctionEnd
  )";

  SinglePassRunAndCheck<opt::LICMPass>(before_hoist, after_hoist, true);

  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_1, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << text << std::endl;
  const ir::Function* f = spvtest::GetFunction(module, 2);
  ir::LoopDescriptor ld{f};

  ir::Loop* loop = ld[17];
  EXPECT_FALSE(loop->IsLCSSA());
  opt::LoopUtils Util(context.get(), loop);
  Util.MakeLoopClosedSSA();
  EXPECT_TRUE(loop->IsLCSSA());
  Match(text, context.get());
}

#endif  // SPIRV_EFFCEE

}  // namespace
