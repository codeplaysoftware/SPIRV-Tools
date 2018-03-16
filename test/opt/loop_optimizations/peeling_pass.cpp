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

#ifdef SPIRV_EFFCEE
#include "effcee/effcee.h"
#endif

#include "../pass_fixture.h"
#include "opt/ir_builder.h"
#include "opt/loop_descriptor.h"
#include "opt/loop_peeling.h"

namespace {

using namespace spvtools;

class PeelingTest : public PassTest<::testing::Test> {
 public:
  // Generic routine to run the loop peeling pass and check
  opt::LoopPeelingPass::LoopPeelingStats RunPeelingTest(
      const std::string& text_head, const std::string& text_tail, SpvOp opcode,
      const std::string& res_id, const std::string& op1, const std::string& op2,
      size_t nb_of_loops) {
    std::string opcode_str;
    switch (opcode) {
      case SpvOpSLessThan:
        opcode_str = "OpSLessThan";
        break;
      case SpvOpSGreaterThan:
        opcode_str = "OpSGreaterThan";
        break;
      case SpvOpSLessThanEqual:
        opcode_str = "OpSLessThanEqual";
        break;
      case SpvOpSGreaterThanEqual:
        opcode_str = "OpSGreaterThanEqual";
        break;
      case SpvOpIEqual:
        opcode_str = "OpIEqual";
        break;
      case SpvOpINotEqual:
        opcode_str = "OpINotEqual";
        break;
      default:
        assert(false && "Unhandled");
        break;
    }
    std::string test_cond =
        res_id + " = " + opcode_str + "  %bool " + op1 + " " + op2 + "\n";

    opt::LoopPeelingPass::LoopPeelingStats stats;
    SinglePassRunAndDisassemble<opt::LoopPeelingPass>(
        text_head + test_cond + text_tail, true, true, &stats);

    ir::Function& f = *context()->module()->begin();
    ir::LoopDescriptor& ld = *context()->GetLoopDescriptor(&f);
    EXPECT_EQ(ld.NumLoops(), nb_of_loops);

    return stats;
  }
};

/*
Test are derivation of the following generated test from the following GLSL +
--eliminate-local-multi-store

#version 330 core
void main() {
  int a = 0;
  for(int i = 0; i < 10; ++i) {
    if (i < 3) {
      a += 2;
    }
  }
}

The condition is interchanged to test < > <= >= == and peel before/after
opportunities.
*/
TEST_F(PeelingTest, PeelingPassBasic) {
  const std::string text_head = R"(
               OpCapability Shader
          %1 = OpExtInstImport "GLSL.std.450"
               OpMemoryModel Logical GLSL450
               OpEntryPoint Fragment %main "main"
               OpExecutionMode %main OriginLowerLeft
               OpSource GLSL 330
               OpName %main "main"
               OpName %a "a"
               OpName %i "i"
       %void = OpTypeVoid
          %3 = OpTypeFunction %void
        %int = OpTypeInt 32 1
%_ptr_Function_int = OpTypePointer Function %int
       %bool = OpTypeBool
     %int_10 = OpConstant %int 10
      %int_9 = OpConstant %int 9
      %int_8 = OpConstant %int 8
      %int_7 = OpConstant %int 7
      %int_6 = OpConstant %int 6
      %int_5 = OpConstant %int 5
      %int_4 = OpConstant %int 4
      %int_3 = OpConstant %int 3
      %int_2 = OpConstant %int 2
      %int_1 = OpConstant %int 1
      %int_0 = OpConstant %int 0
       %main = OpFunction %void None %3
          %5 = OpLabel
          %a = OpVariable %_ptr_Function_int Function
          %i = OpVariable %_ptr_Function_int Function
               OpStore %a %int_0
               OpStore %i %int_0
               OpBranch %11
         %11 = OpLabel
         %31 = OpPhi %int %int_0 %5 %33 %14
         %32 = OpPhi %int %int_0 %5 %30 %14
               OpLoopMerge %13 %14 None
               OpBranch %15
         %15 = OpLabel
         %19 = OpSLessThan %bool %32 %int_10
               OpBranchConditional %19 %12 %13
         %12 = OpLabel
  )";
  const std::string text_tail = R"(
               OpSelectionMerge %24 None
               OpBranchConditional %22 %23 %24
         %23 = OpLabel
         %27 = OpIAdd %int %31 %int_2
               OpStore %a %27
               OpBranch %24
         %24 = OpLabel
         %33 = OpPhi %int %31 %12 %27 %23
               OpBranch %14
         %14 = OpLabel
         %30 = OpIAdd %int %32 %int_1
               OpStore %i %30
               OpBranch %11
         %13 = OpLabel
               OpReturn
               OpFunctionEnd
  )";

  auto run_test = [&text_head, &text_tail, this](SpvOp opcode,
                                                 const std::string& op1,
                                                 const std::string& op2) {
    auto stats =
        RunPeelingTest(text_head, text_tail, opcode, "%22", op1, op2, 2);

    EXPECT_EQ(stats.peeled_loops_.size(), 1u);
    if (stats.peeled_loops_.size() != 1u)
      return std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t>{
          opt::LoopPeelingPass::PeelDirection::kNone, 0};

    return std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t>{
        std::get<1>(*stats.peeled_loops_.begin()),
        std::get<2>(*stats.peeled_loops_.begin())};
  };

  // Test LT
  // Peel before by a factor of 3.
  {
    SCOPED_TRACE("Peel before iv < 3");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpSLessThan, "%32", "%int_3");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kBefore);
    EXPECT_EQ(peel_info.second, 3u);
  }
  {
    SCOPED_TRACE("Peel before 3 > iv");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpSGreaterThan, "%int_3", "%32");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kBefore);
    EXPECT_EQ(peel_info.second, 3u);
  }

  // Peel after by a factor of 2.
  {
    SCOPED_TRACE("Peel after iv < 8");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpSLessThan, "%32", "%int_8");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kAfter);
    EXPECT_EQ(peel_info.second, 2u);
  }
  {
    SCOPED_TRACE("Peel after 8 > iv");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpSGreaterThan, "%int_8", "%32");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kAfter);
    EXPECT_EQ(peel_info.second, 2u);
  }

  // Test GT
  // Peel before by a factor of 1.
  {
    SCOPED_TRACE("Peel before iv > 1");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpSGreaterThan, "%32", "%int_1");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kBefore);
    EXPECT_EQ(peel_info.second, 1u);
  }
  {
    SCOPED_TRACE("Peel before 1 < iv");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpSLessThan, "%int_1", "%32");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kBefore);
    EXPECT_EQ(peel_info.second, 1u);
  }

  // Peel after by a factor of 3.
  {
    SCOPED_TRACE("Peel after iv > 7");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpSGreaterThan, "%32", "%int_7");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kAfter);
    EXPECT_EQ(peel_info.second, 3u);
  }
  {
    SCOPED_TRACE("Peel after 7 < iv");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpSLessThan, "%int_7", "%32");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kAfter);
    EXPECT_EQ(peel_info.second, 3u);
  }

  // Test LE
  // Peel before by a factor of 2.
  {
    SCOPED_TRACE("Peel before iv <= 1");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpSLessThanEqual, "%32", "%int_1");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kBefore);
    EXPECT_EQ(peel_info.second, 2u);
  }
  {
    SCOPED_TRACE("Peel before 1 => iv");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpSGreaterThanEqual, "%int_1", "%32");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kBefore);
    EXPECT_EQ(peel_info.second, 2u);
  }

  // Peel after by a factor of 4.
  {
    SCOPED_TRACE("Peel after iv <= 7");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpSLessThanEqual, "%32", "%int_7");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kAfter);
    EXPECT_EQ(peel_info.second, 2u);
  }
  {
    SCOPED_TRACE("Peel after 7 => iv");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpSGreaterThanEqual, "%int_7", "%32");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kAfter);
    EXPECT_EQ(peel_info.second, 2u);
  }

  // Test GE
  // Peel before by a factor of 2.
  {
    SCOPED_TRACE("Peel before iv >= 2");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpSGreaterThanEqual, "%32", "%int_2");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kBefore);
    EXPECT_EQ(peel_info.second, 1u);
  }
  {
    SCOPED_TRACE("Peel before 2 >= iv");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpSLessThanEqual, "%int_2", "%32");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kBefore);
    EXPECT_EQ(peel_info.second, 1u);
  }

  // Peel after by a factor of 4.
  {
    SCOPED_TRACE("Peel after iv >= 7");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpSGreaterThanEqual, "%32", "%int_7");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kAfter);
    EXPECT_EQ(peel_info.second, 4u);
  }
  {
    SCOPED_TRACE("Peel after 7 <= iv");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpSLessThanEqual, "%int_7", "%32");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kAfter);
    EXPECT_EQ(peel_info.second, 4u);
  }
  // Test EQ
  // Peel before by a factor of 1.
  {
    SCOPED_TRACE("Peel before iv == 0");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpIEqual, "%32", "%int_0");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kBefore);
    EXPECT_EQ(peel_info.second, 1u);
  }
  {
    SCOPED_TRACE("Peel before 0 == iv");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpIEqual, "%int_0", "%32");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kBefore);
    EXPECT_EQ(peel_info.second, 1u);
  }

  // Peel after by a factor of 1.
  {
    SCOPED_TRACE("Peel after iv == 9");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpIEqual, "%32", "%int_9");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kAfter);
    EXPECT_EQ(peel_info.second, 1u);
  }
  {
    SCOPED_TRACE("Peel after 9 == iv");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpIEqual, "%int_9", "%32");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kAfter);
    EXPECT_EQ(peel_info.second, 1u);
  }

  // Test NE
  // Peel before by a factor of 1.
  {
    SCOPED_TRACE("Peel before iv != 0");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpINotEqual, "%32", "%int_0");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kBefore);
    EXPECT_EQ(peel_info.second, 1u);
  }
  {
    SCOPED_TRACE("Peel before 0 != iv");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpINotEqual, "%int_0", "%32");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kBefore);
    EXPECT_EQ(peel_info.second, 1u);
  }

  // Peel after by a factor of 1.
  {
    SCOPED_TRACE("Peel after iv != 9");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpINotEqual, "%32", "%int_9");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kAfter);
    EXPECT_EQ(peel_info.second, 1u);
  }
  {
    SCOPED_TRACE("Peel after 9 != iv");

    std::pair<opt::LoopPeelingPass::PeelDirection, uint32_t> peel_info =
        run_test(SpvOpINotEqual, "%int_9", "%32");
    EXPECT_EQ(peel_info.first, opt::LoopPeelingPass::PeelDirection::kAfter);
    EXPECT_EQ(peel_info.second, 1u);
  }
}

}  // namespace
