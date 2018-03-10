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

#ifndef LIBSPIRV_OPT_SCALAR_ANALYSIS_H_
#define LIBSPIRV_OPT_SCALAR_ANALYSIS_H_

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "opt/basic_block.h"
#include "opt/instruction.h"
#include "opt/ir_context.h"
#include "opt/scalar_analysis_nodes.h"

namespace spvtools {
namespace opt {

// Manager for the Scalar Evolution analysis. Creates and maintains a DAG of
// scalar operations generated from analysing the use def graph from incoming
// instructions. Each node is hashed as it is added so like node (for instance,
// two induction variables i=0,i++ and j=0,j++) become the same node. After
// creating a DAG with AnalyzeInstruction it can the be simplified into a more
// usable form with SimplifyExpression.
class ScalarEvolutionAnalysis {
 public:
  explicit ScalarEvolutionAnalysis(ir::IRContext* context)
      : context_(context) {}

  void DumpAsDot(std::ostream& out_stream) {
    out_stream << "digraph  {\n";
    for (const std::unique_ptr<SENode>& node : node_cache_) {
      node->DumpDot(out_stream);
    }
    out_stream << "}\n";
  }

  // Create a unary negative node on |operand|.
  SENode* CreateNegation(SENode* operand);

  // Creates a subtraction between the two operands by adding |operand_1| to the
  // negation of |operand_2|
  SENode* CreateSubtraction(SENode* operand_1, SENode* operand_2);

  // Create an addition node between two operands.
  SENode* CreateAddNode(SENode* operand_1, SENode* operand_2);

  // Create a multiply node between two operands.
  SENode* CreateMultiplyNode(SENode* operand_1, SENode* operand_2);

  // Create a node representing a constant integer.
  SENode* CreateConstant(int64_t integer);

  // Create a value unknown node, such as a load.
  SENode* CreateValueUnknownNode();

  // Create a CantComputeNode. Used to exit out of analysis.
  SENode* CreateCantComputeNode();

  // Construct the DAG by traversing use def chain of |inst|.
  SENode* AnalyzeInstruction(const ir::Instruction* inst);

  // Simplify the |node| by grouping like terms or if contains a recurrent
  // expression, rewrite the graph so the whole DAG (from |node| down) is in
  // terms of that recurrent expression.
  //
  // For example.
  // Induction variable i=0, i++ would produce Rec(0,1) so i+1 could be
  // transformed into Rec(1,1).
  //
  // X+X*2+Y-Y+34-17 would be transformed into 3*X + 17, where X and Y are
  // ValueUnknown nodes (such as a load instruction).
  SENode* SimplifyExpression(SENode* node);

  // Can we prove that |source| and |destination| are equal. If they are not
  // equal or it cannot be proven that they are equal return false.
  bool CanProveEqual(const SENode& source, const SENode& destination);

  // Can we prove that |source| and |destination| are not equal. If they can be
  // proven to be equal or cannot be proven to not equal return false.
  bool CanProveNotEqual(const SENode& source, const SENode& destination);

  // Add |prospective_node| into the cache and return a raw pointer to it. If
  // |prospective_node| is already in the cache just return the raw pointer.
  SENode* GetCachedOrAdd(std::unique_ptr<SENode> prospective_node);

 private:
  ir::IRContext* context_;

  // A map of instructions to SENodes. Not every SENode comes from an
  // instruction however this is used when nodes are created through the analyze
  // instruction methods.
  std::map<const ir::Instruction*, SENode*> instruction_map_;

  // Helper functor to allow two unique_ptr to nodes to be compare. Only needed
  // for the unordered_set implementation.
  struct NodePointersEquality {
    bool operator()(const std::unique_ptr<SENode>& lhs,
                    const std::unique_ptr<SENode>& rhs) const {
      return *lhs == *rhs;
    }
  };

  // Cache of nodes. All pointers to the nodes are references to the memory
  // managed by they set.
  std::unordered_set<std::unique_ptr<SENode>, SENodeHash, NodePointersEquality>
      node_cache_;

  SENode* AnalyzeConstant(const ir::Instruction* inst);

  // Handles both addition and subtraction. If the |is_subtraction| flag is set
  // then the addition will be op1+(-op2) otherwise op1+op2.
  SENode* AnalyzeAddOp(const ir::Instruction* add, bool is_subtraction);

  SENode* AnalyzeMultiplyOp(const ir::Instruction* multiply);

  SENode* AnalyzePhiInstruction(const ir::Instruction* phi);
};

}  // namespace opt
}  // namespace spvtools

#endif  // LIBSPIRV_OPT_SCALAR_ANALYSIS_H__
