// Copyright (c) 2017 Google Inc.
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
#include <set>
#include <string>
#include <vector>

#include "../assembly_builder.h"
#include "../function_utils.h"
#include "../pass_fixture.h"
#include "../pass_utils.h"
#include "opt/dominator_analysis_pass.h"
#include "opt/pass.h"

namespace {

using namespace spvtools;
using ::testing::UnorderedElementsAre;

using PassClassTest = PassTest<::testing::Test>;

// Check that x dominates y, and
//   if x != y then
//      x strictly dominates y and
//      y does not dominate x and
//      y does not strictly dominate x
//   if x == x then
//      x does not strictly dominate itself
void check_dominance(const opt::DominatorAnalysisBase& dom_tree,
                     const ir::Function* fn, uint32_t x, uint32_t y) {
  SCOPED_TRACE("Check dominance properties for Basic Block " +
               std::to_string(x) + " and " + std::to_string(y));
  EXPECT_TRUE(dom_tree.Dominates(spvtest::GetBasicBlock(fn, x),
                                 spvtest::GetBasicBlock(fn, y)));
  EXPECT_TRUE(dom_tree.Dominates(x, y));
  if (x == y) {
    EXPECT_FALSE(dom_tree.StrictlyDominates(x, x));
  } else {
    EXPECT_TRUE(dom_tree.StrictlyDominates(x, y));
    EXPECT_FALSE(dom_tree.Dominates(y, x));
    EXPECT_FALSE(dom_tree.StrictlyDominates(y, x));
  }
}

// Check that x does not dominates y and vise versa
void check_no_dominance(const opt::DominatorAnalysisBase& dom_tree,
                        const ir::Function* fn, uint32_t x, uint32_t y) {
  SCOPED_TRACE("Check no domination for Basic Block " + std::to_string(x) +
               " and " + std::to_string(y));
  EXPECT_FALSE(dom_tree.Dominates(spvtest::GetBasicBlock(fn, x),
                                  spvtest::GetBasicBlock(fn, y)));
  EXPECT_FALSE(dom_tree.Dominates(x, y));
  EXPECT_FALSE(dom_tree.StrictlyDominates(spvtest::GetBasicBlock(fn, x),
                                          spvtest::GetBasicBlock(fn, y)));
  EXPECT_FALSE(dom_tree.StrictlyDominates(x, y));

  EXPECT_FALSE(dom_tree.Dominates(spvtest::GetBasicBlock(fn, y),
                                  spvtest::GetBasicBlock(fn, x)));
  EXPECT_FALSE(dom_tree.Dominates(y, x));
  EXPECT_FALSE(dom_tree.StrictlyDominates(spvtest::GetBasicBlock(fn, y),
                                          spvtest::GetBasicBlock(fn, x)));
  EXPECT_FALSE(dom_tree.StrictlyDominates(y, x));
}

TEST_F(PassClassTest, DominatorSimpleCFG) {
  const std::string text = R"(
               OpCapability Addresses
               OpCapability Kernel
               OpMemoryModel Physical64 OpenCL
               OpEntryPoint Kernel %1 "main"
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %4 = OpTypeBool
          %5 = OpTypeInt 32 0
          %6 = OpConstant %5 0
          %7 = OpConstantFalse %4
          %8 = OpConstantTrue %4
          %9 = OpConstant %5 1
          %1 = OpFunction %2 None %3
         %10 = OpLabel
               OpBranch %11
         %11 = OpLabel
               OpSwitch %6 %12 1 %13
         %12 = OpLabel
               OpBranch %14
         %13 = OpLabel
               OpBranch %14
         %14 = OpLabel
               OpBranchConditional %8 %11 %15
         %15 = OpLabel
               OpReturn
               OpFunctionEnd
)";
  // clang-format on
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_0, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << text << std::endl;
  const ir::Function* fn = spvtest::GetFunction(module, 1);
  const ir::BasicBlock* entry = spvtest::GetBasicBlock(fn, 10);
  EXPECT_EQ(entry, fn->entry().get())
      << "The entry node is not the expected one";

  // Test normal dominator tree
  {
    opt::DominatorAnalysis dom_tree;
    ir::CFG cfg(module);
    dom_tree.InitializeTree(fn, cfg);

    // Inspect the actual tree
    opt::DominatorTree& tree = dom_tree.GetDomTree();
    EXPECT_EQ(tree.GetRoot()->bb_, entry);

    // (strict) dominance checks
    for (uint32_t id : {10, 11, 12, 13, 14, 15})
      check_dominance(dom_tree, fn, id, id);

    check_dominance(dom_tree, fn, 10, 11);
    check_dominance(dom_tree, fn, 10, 12);
    check_dominance(dom_tree, fn, 10, 13);
    check_dominance(dom_tree, fn, 10, 14);
    check_dominance(dom_tree, fn, 10, 15);

    check_dominance(dom_tree, fn, 11, 12);
    check_dominance(dom_tree, fn, 11, 13);
    check_dominance(dom_tree, fn, 11, 14);
    check_dominance(dom_tree, fn, 11, 15);

    check_dominance(dom_tree, fn, 14, 15);

    check_no_dominance(dom_tree, fn, 12, 13);
    check_no_dominance(dom_tree, fn, 12, 14);
    check_no_dominance(dom_tree, fn, 13, 14);

    // check with some invalid inputs
    EXPECT_FALSE(dom_tree.Dominates(nullptr, entry));
    EXPECT_FALSE(dom_tree.Dominates(entry, nullptr));
    EXPECT_FALSE(dom_tree.Dominates(nullptr, nullptr));
    EXPECT_FALSE(dom_tree.Dominates(10, 1));
    EXPECT_FALSE(dom_tree.Dominates(1, 10));
    EXPECT_FALSE(dom_tree.Dominates(1, 1));

    EXPECT_FALSE(dom_tree.StrictlyDominates(nullptr, entry));
    EXPECT_FALSE(dom_tree.StrictlyDominates(entry, nullptr));
    EXPECT_FALSE(dom_tree.StrictlyDominates(nullptr, nullptr));
    EXPECT_FALSE(dom_tree.StrictlyDominates(10, 1));
    EXPECT_FALSE(dom_tree.StrictlyDominates(1, 10));
    EXPECT_FALSE(dom_tree.StrictlyDominates(1, 1));

    EXPECT_EQ(dom_tree.ImmediateDominator(entry), nullptr);
    EXPECT_EQ(dom_tree.ImmediateDominator(nullptr), nullptr);

    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 11)),
              spvtest::GetBasicBlock(fn, 10));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 12)),
              spvtest::GetBasicBlock(fn, 11));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 13)),
              spvtest::GetBasicBlock(fn, 11));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 14)),
              spvtest::GetBasicBlock(fn, 11));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 15)),
              spvtest::GetBasicBlock(fn, 14));
  }

  // Test post dominator tree
  {
    opt::PostDominatorAnalysis dom_tree;
    ir::CFG cfg(module);
    dom_tree.InitializeTree(fn, cfg);

    // Inspect the actual tree
    opt::DominatorTree& tree = dom_tree.GetDomTree();
    EXPECT_EQ(tree.GetRoot()->bb_, spvtest::GetBasicBlock(fn, 15));

    // (strict) dominance checks
    for (uint32_t id : {10, 11, 12, 13, 14, 15})
      check_dominance(dom_tree, fn, id, id);

    check_dominance(dom_tree, fn, 14, 10);
    check_dominance(dom_tree, fn, 14, 11);
    check_dominance(dom_tree, fn, 14, 12);
    check_dominance(dom_tree, fn, 14, 13);

    check_dominance(dom_tree, fn, 15, 10);
    check_dominance(dom_tree, fn, 15, 11);
    check_dominance(dom_tree, fn, 15, 12);
    check_dominance(dom_tree, fn, 15, 13);
    check_dominance(dom_tree, fn, 15, 14);

    check_no_dominance(dom_tree, fn, 13, 12);
    check_no_dominance(dom_tree, fn, 12, 11);
    check_no_dominance(dom_tree, fn, 13, 11);

    // check with some invalid inputs
    EXPECT_FALSE(dom_tree.Dominates(nullptr, entry));
    EXPECT_FALSE(dom_tree.Dominates(entry, nullptr));
    EXPECT_FALSE(dom_tree.Dominates(nullptr, nullptr));
    EXPECT_FALSE(dom_tree.Dominates(10, 1));
    EXPECT_FALSE(dom_tree.Dominates(1, 10));
    EXPECT_FALSE(dom_tree.Dominates(1, 1));

    EXPECT_FALSE(dom_tree.StrictlyDominates(nullptr, entry));
    EXPECT_FALSE(dom_tree.StrictlyDominates(entry, nullptr));
    EXPECT_FALSE(dom_tree.StrictlyDominates(nullptr, nullptr));
    EXPECT_FALSE(dom_tree.StrictlyDominates(10, 1));
    EXPECT_FALSE(dom_tree.StrictlyDominates(1, 10));
    EXPECT_FALSE(dom_tree.StrictlyDominates(1, 1));

    EXPECT_EQ(dom_tree.ImmediateDominator(nullptr), nullptr);

    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 11)),
              spvtest::GetBasicBlock(fn, 14));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 12)),
              spvtest::GetBasicBlock(fn, 14));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 13)),
              spvtest::GetBasicBlock(fn, 14));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 14)),
              spvtest::GetBasicBlock(fn, 15));

    // Exit node
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 15)),
              nullptr);
  }
}

TEST_F(PassClassTest, DominatorIrreducibleCFG) {
  const std::string text = R"(
               OpCapability Addresses
               OpCapability Kernel
               OpMemoryModel Physical64 OpenCL
               OpEntryPoint Kernel %1 "main"
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %4 = OpTypeBool
          %5 = OpTypeInt 32 0
          %6 = OpConstantFalse %4
          %7 = OpConstantTrue %4
          %1 = OpFunction %2 None %3
          %8 = OpLabel
               OpBranch %9
          %9 = OpLabel
               OpBranchConditional %7 %10 %11
         %10 = OpLabel
               OpBranch %11
         %11 = OpLabel
               OpBranchConditional %7 %10 %12
         %12 = OpLabel
               OpReturn
               OpFunctionEnd
)";
  // clang-format on
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_0, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << text << std::endl;
  const ir::Function* fn = spvtest::GetFunction(module, 1);

  const ir::BasicBlock* entry = spvtest::GetBasicBlock(fn, 8);
  EXPECT_EQ(entry, fn->entry().get())
      << "The entry node is not the expected one";

  // Check normal dominator tree
  {
    opt::DominatorAnalysis dom_tree;
    ir::CFG cfg(module);
    dom_tree.InitializeTree(fn, cfg);

    // Inspect the actual tree
    opt::DominatorTree& tree = dom_tree.GetDomTree();
    EXPECT_EQ(tree.GetRoot()->bb_, entry);

    // (strict) dominance checks
    for (uint32_t id : {8, 9, 10, 11, 12})
      check_dominance(dom_tree, fn, id, id);

    check_dominance(dom_tree, fn, 8, 9);
    check_dominance(dom_tree, fn, 8, 10);
    check_dominance(dom_tree, fn, 8, 11);
    check_dominance(dom_tree, fn, 8, 12);

    check_dominance(dom_tree, fn, 9, 10);
    check_dominance(dom_tree, fn, 9, 11);
    check_dominance(dom_tree, fn, 9, 12);

    check_dominance(dom_tree, fn, 11, 12);

    check_no_dominance(dom_tree, fn, 10, 11);

    EXPECT_EQ(dom_tree.ImmediateDominator(entry), nullptr);

    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 9)),
              spvtest::GetBasicBlock(fn, 8));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 10)),
              spvtest::GetBasicBlock(fn, 9));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 11)),
              spvtest::GetBasicBlock(fn, 9));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 12)),
              spvtest::GetBasicBlock(fn, 11));
  }

  // Check post dominator tree
  {
    opt::PostDominatorAnalysis dom_tree;
    ir::CFG cfg(module);
    dom_tree.InitializeTree(fn, cfg);

    // Inspect the actual tree
    opt::DominatorTree& tree = dom_tree.GetDomTree();
    EXPECT_EQ(tree.GetRoot()->bb_, spvtest::GetBasicBlock(fn, 12));

    // (strict) dominance checks
    for (uint32_t id : {8, 9, 10, 11, 12})
      check_dominance(dom_tree, fn, id, id);

    check_dominance(dom_tree, fn, 12, 8);
    check_dominance(dom_tree, fn, 12, 10);
    check_dominance(dom_tree, fn, 12, 11);
    check_dominance(dom_tree, fn, 12, 12);

    check_dominance(dom_tree, fn, 11, 8);
    check_dominance(dom_tree, fn, 11, 9);
    check_dominance(dom_tree, fn, 11, 10);

    check_dominance(dom_tree, fn, 9, 8);

    EXPECT_EQ(dom_tree.ImmediateDominator(entry),
              spvtest::GetBasicBlock(fn, 9));

    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 9)),
              spvtest::GetBasicBlock(fn, 11));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 10)),
              spvtest::GetBasicBlock(fn, 11));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 11)),
              spvtest::GetBasicBlock(fn, 12));

    // Exit node.
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 12)),
              nullptr);
  }
}

TEST_F(PassClassTest, DominatorLoopToSelf) {
  const std::string text = R"(
               OpCapability Addresses
               OpCapability Kernel
               OpMemoryModel Physical64 OpenCL
               OpEntryPoint Kernel %1 "main"
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %4 = OpTypeBool
          %5 = OpTypeInt 32 0
          %6 = OpConstant %5 0
          %7 = OpConstantFalse %4
          %8 = OpConstantTrue %4
          %9 = OpConstant %5 1
          %1 = OpFunction %2 None %3
         %10 = OpLabel
               OpBranch %11
         %11 = OpLabel
               OpSwitch %6 %12 1 %11
         %12 = OpLabel
               OpReturn
               OpFunctionEnd
)";
  // clang-format on
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_0, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << text << std::endl;
  const ir::Function* fn = spvtest::GetFunction(module, 1);

  const ir::BasicBlock* entry = spvtest::GetBasicBlock(fn, 10);
  EXPECT_EQ(entry, fn->entry().get())
      << "The entry node is not the expected one";

  // Check normal dominator tree
  {
    opt::DominatorAnalysis dom_tree;
    ir::CFG cfg(module);
    dom_tree.InitializeTree(fn, cfg);

    // Inspect the actual tree
    opt::DominatorTree& tree = dom_tree.GetDomTree();
    EXPECT_EQ(tree.GetRoot()->bb_, entry);

    // (strict) dominance checks
    for (uint32_t id : {10, 11, 12}) check_dominance(dom_tree, fn, id, id);

    check_dominance(dom_tree, fn, 10, 11);
    check_dominance(dom_tree, fn, 10, 12);
    check_dominance(dom_tree, fn, 11, 12);

    EXPECT_EQ(dom_tree.ImmediateDominator(entry), nullptr);

    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 11)),
              spvtest::GetBasicBlock(fn, 10));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 12)),
              spvtest::GetBasicBlock(fn, 11));
  }

  // Check post dominator tree
  {
    opt::PostDominatorAnalysis dom_tree;
    ir::CFG cfg(module);
    dom_tree.InitializeTree(fn, cfg);

    // Inspect the actual tree
    opt::DominatorTree& tree = dom_tree.GetDomTree();
    EXPECT_EQ(tree.GetRoot()->bb_, spvtest::GetBasicBlock(fn, 12));

    // (strict) dominance checks
    for (uint32_t id : {10, 11, 12}) check_dominance(dom_tree, fn, id, id);

    check_dominance(dom_tree, fn, 12, 10);
    check_dominance(dom_tree, fn, 12, 11);
    check_dominance(dom_tree, fn, 12, 12);

    EXPECT_EQ(dom_tree.ImmediateDominator(entry),
              spvtest::GetBasicBlock(fn, 11));

    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 11)),
              spvtest::GetBasicBlock(fn, 12));

    // Exit node
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 12)),
              nullptr);
  }
}

TEST_F(PassClassTest, DominatorUnreachableInLoop) {
  const std::string text = R"(
               OpCapability Addresses
               OpCapability Kernel
               OpMemoryModel Physical64 OpenCL
               OpEntryPoint Kernel %1 "main"
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %4 = OpTypeBool
          %5 = OpTypeInt 32 0
          %6 = OpConstant %5 0
          %7 = OpConstantFalse %4
          %8 = OpConstantTrue %4
          %9 = OpConstant %5 1
          %1 = OpFunction %2 None %3
         %10 = OpLabel
               OpBranch %11
         %11 = OpLabel
               OpSwitch %6 %12 1 %13
         %12 = OpLabel
               OpBranch %14
         %13 = OpLabel
               OpUnreachable
         %14 = OpLabel
               OpBranchConditional %8 %11 %15
         %15 = OpLabel
               OpReturn
               OpFunctionEnd
)";
  // clang-format on
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_0, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << text << std::endl;
  const ir::Function* fn = spvtest::GetFunction(module, 1);

  const ir::BasicBlock* entry = spvtest::GetBasicBlock(fn, 10);
  EXPECT_EQ(entry, fn->entry().get())
      << "The entry node is not the expected one";

  // Check normal dominator tree
  {
    opt::DominatorAnalysis dom_tree;
    ir::CFG cfg(module);
    dom_tree.InitializeTree(fn, cfg);

    // Inspect the actual tree
    opt::DominatorTree& tree = dom_tree.GetDomTree();
    EXPECT_EQ(tree.GetRoot()->bb_, entry);

    // (strict) dominance checks
    for (uint32_t id : {10, 11, 12, 13, 14, 15})
      check_dominance(dom_tree, fn, id, id);

    check_dominance(dom_tree, fn, 10, 11);
    check_dominance(dom_tree, fn, 10, 13);
    check_dominance(dom_tree, fn, 10, 12);
    check_dominance(dom_tree, fn, 10, 14);
    check_dominance(dom_tree, fn, 10, 15);

    check_dominance(dom_tree, fn, 11, 12);
    check_dominance(dom_tree, fn, 11, 13);
    check_dominance(dom_tree, fn, 11, 14);
    check_dominance(dom_tree, fn, 11, 15);

    check_dominance(dom_tree, fn, 12, 14);
    check_dominance(dom_tree, fn, 12, 15);

    check_dominance(dom_tree, fn, 14, 15);

    check_no_dominance(dom_tree, fn, 13, 12);
    check_no_dominance(dom_tree, fn, 13, 14);
    check_no_dominance(dom_tree, fn, 13, 15);

    EXPECT_EQ(dom_tree.ImmediateDominator(entry), nullptr);

    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 11)),
              spvtest::GetBasicBlock(fn, 10));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 12)),
              spvtest::GetBasicBlock(fn, 11));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 13)),
              spvtest::GetBasicBlock(fn, 11));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 14)),
              spvtest::GetBasicBlock(fn, 12));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 15)),
              spvtest::GetBasicBlock(fn, 14));
  }

  // Check post dominator tree
  {
    opt::PostDominatorAnalysis dom_tree;
    ir::CFG cfg(module);
    dom_tree.InitializeTree(fn, cfg);

    std::set<uint32_t> exits{15, 13, 14, 11};
    opt::DominatorTree& tree = dom_tree.GetDomTree();
    for (const opt::DominatorTreeNode* node : tree.Roots()) {
      EXPECT_TRUE(exits.count(node->id()) != 0);
    }

    // (strict) dominance checks
    for (uint32_t id : {10, 11, 12, 13, 14, 15})
      check_dominance(dom_tree, fn, id, id);

    check_no_dominance(dom_tree, fn, 15, 10);
    check_no_dominance(dom_tree, fn, 15, 11);
    check_no_dominance(dom_tree, fn, 15, 12);
    check_no_dominance(dom_tree, fn, 15, 13);
    check_no_dominance(dom_tree, fn, 15, 14);

    check_dominance(dom_tree, fn, 14, 12);

    check_no_dominance(dom_tree, fn, 13, 10);
    check_no_dominance(dom_tree, fn, 13, 11);
    check_no_dominance(dom_tree, fn, 13, 12);
    check_no_dominance(dom_tree, fn, 13, 14);
    check_no_dominance(dom_tree, fn, 13, 15);

    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 10)),
              spvtest::GetBasicBlock(fn, 11));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 12)),
              spvtest::GetBasicBlock(fn, 14));

    // Exit nodes.
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 15)),
              nullptr);
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 13)),
              nullptr);
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 14)),
              nullptr);
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 11)),
              nullptr);
  }
}

TEST_F(PassClassTest, DominatorInfinitLoop) {
  const std::string text = R"(
               OpCapability Addresses
               OpCapability Kernel
               OpMemoryModel Physical64 OpenCL
               OpEntryPoint Kernel %1 "main"
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %4 = OpTypeBool
          %5 = OpTypeInt 32 0
          %6 = OpConstant %5 0
          %7 = OpConstantFalse %4
          %8 = OpConstantTrue %4
          %9 = OpConstant %5 1
          %1 = OpFunction %2 None %3
         %10 = OpLabel
               OpBranch %11
         %11 = OpLabel
               OpSwitch %6 %12 1 %13
         %12 = OpLabel
               OpReturn
         %13 = OpLabel
               OpBranch %13
               OpFunctionEnd
)";
  // clang-format on
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_0, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << text << std::endl;
  const ir::Function* fn = spvtest::GetFunction(module, 1);

  const ir::BasicBlock* entry = spvtest::GetBasicBlock(fn, 10);
  EXPECT_EQ(entry, fn->entry().get())
      << "The entry node is not the expected one";
  // Check normal dominator tree
  {
    opt::DominatorAnalysis dom_tree;
    ir::CFG cfg(module);
    dom_tree.InitializeTree(fn, cfg);

    // Inspect the actual tree
    opt::DominatorTree& tree = dom_tree.GetDomTree();
    EXPECT_EQ(tree.GetRoot()->bb_, entry);

    // (strict) dominance checks
    for (uint32_t id : {10, 11, 12, 13}) check_dominance(dom_tree, fn, id, id);

    check_dominance(dom_tree, fn, 10, 11);
    check_dominance(dom_tree, fn, 10, 12);
    check_dominance(dom_tree, fn, 10, 13);

    check_dominance(dom_tree, fn, 11, 12);
    check_dominance(dom_tree, fn, 11, 13);

    check_no_dominance(dom_tree, fn, 13, 12);

    EXPECT_EQ(dom_tree.ImmediateDominator(entry), nullptr);

    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 11)),
              spvtest::GetBasicBlock(fn, 10));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 12)),
              spvtest::GetBasicBlock(fn, 11));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 13)),
              spvtest::GetBasicBlock(fn, 11));
  }

  // Check post dominator tree
  {
    opt::PostDominatorAnalysis dom_tree;
    ir::CFG cfg(module);
    dom_tree.InitializeTree(fn, cfg);

    // Inspect the actual tree
    opt::DominatorTree& tree = dom_tree.GetDomTree();
    EXPECT_EQ(tree.GetRoot()->bb_, spvtest::GetBasicBlock(fn, 12));

    // (strict) dominance checks
    for (uint32_t id : {10, 11, 12}) check_dominance(dom_tree, fn, id, id);

    check_dominance(dom_tree, fn, 12, 11);
    check_dominance(dom_tree, fn, 12, 10);

    // 13 should be completely out of tree as it's unreachable from exit nodes
    check_no_dominance(dom_tree, fn, 12, 13);
    check_no_dominance(dom_tree, fn, 11, 13);
    check_no_dominance(dom_tree, fn, 10, 13);

    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 10)),
              spvtest::GetBasicBlock(fn, 11));

    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 11)),
              spvtest::GetBasicBlock(fn, 12));

    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 12)),
              nullptr);
  }
}

TEST_F(PassClassTest, DominatorUnreachableFromEntry) {
  const std::string text = R"(
               OpCapability Addresses
               OpCapability Addresses
               OpCapability Kernel
               OpMemoryModel Physical64 OpenCL
               OpEntryPoint Kernel %1 "main"
          %2 = OpTypeVoid
          %3 = OpTypeFunction %2
          %4 = OpTypeBool
          %5 = OpTypeInt 32 0
          %6 = OpConstantFalse %4
          %7 = OpConstantTrue %4
          %1 = OpFunction %2 None %3
          %8 = OpLabel
               OpBranch %9
          %9 = OpLabel
               OpReturn
         %10 = OpLabel
               OpBranch %9
               OpFunctionEnd
)";
  // clang-format on
  std::unique_ptr<ir::IRContext> context =
      BuildModule(SPV_ENV_UNIVERSAL_1_0, nullptr, text,
                  SPV_TEXT_TO_BINARY_OPTION_PRESERVE_NUMERIC_IDS);
  ir::Module* module = context->module();
  EXPECT_NE(nullptr, module) << "Assembling failed for shader:\n"
                             << text << std::endl;
  const ir::Function* fn = spvtest::GetFunction(module, 1);

  const ir::BasicBlock* entry = spvtest::GetBasicBlock(fn, 8);
  EXPECT_EQ(entry, fn->entry().get())
      << "The entry node is not the expected one";

  // Check dominator tree
  {
    opt::DominatorAnalysis dom_tree;
    ir::CFG cfg(module);
    dom_tree.InitializeTree(fn, cfg);
    // Inspect the actual tree
    opt::DominatorTree& tree = dom_tree.GetDomTree();
    EXPECT_EQ(tree.GetRoot()->bb_, entry);

    // (strict) dominance checks
    for (uint32_t id : {8, 9}) check_dominance(dom_tree, fn, id, id);

    check_dominance(dom_tree, fn, 8, 9);

    check_no_dominance(dom_tree, fn, 10, 8);
    check_no_dominance(dom_tree, fn, 10, 9);

    EXPECT_EQ(dom_tree.ImmediateDominator(entry), nullptr);

    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 9)),
              spvtest::GetBasicBlock(fn, 8));
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 10)),
              nullptr);
  }

  // Check post dominator tree
  {
    opt::PostDominatorAnalysis dom_tree;
    ir::CFG cfg(module);
    dom_tree.InitializeTree(fn, cfg);

    // Inspect the actual tree
    opt::DominatorTree& tree = dom_tree.GetDomTree();
    EXPECT_EQ(tree.GetRoot()->bb_, spvtest::GetBasicBlock(fn, 9));

    // (strict) dominance checks
    for (uint32_t id : {8, 9, 10}) check_dominance(dom_tree, fn, id, id);

    check_dominance(dom_tree, fn, 9, 8);
    check_dominance(dom_tree, fn, 9, 10);

    EXPECT_EQ(dom_tree.ImmediateDominator(entry),
              spvtest::GetBasicBlock(fn, 9));

    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 9)),
              nullptr);
    EXPECT_EQ(dom_tree.ImmediateDominator(spvtest::GetBasicBlock(fn, 10)),
              spvtest::GetBasicBlock(fn, 9));
  }
}

}  // namespace
