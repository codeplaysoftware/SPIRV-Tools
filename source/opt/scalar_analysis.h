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

class ScalarEvolutionAnalysis {
 public:
  explicit ScalarEvolutionAnalysis(ir::IRContext* context)
      : context_(context) {}

  ~ScalarEvolutionAnalysis() {
    for (auto& pair : scalar_evolutions_) {
      delete pair.second;
    }
  }

  const SENode* GetNodeFromInstruction(uint32_t instruction) {
    auto itr = scalar_evolutions_.find(instruction);
    if (itr == scalar_evolutions_.end()) return nullptr;
    return itr->second;
  }

  void DumpAsDot(std::ostream& out_stream) {
    out_stream << "digraph  {\n";
    for (auto& pair : scalar_evolutions_) {
      pair.second->DumpDot(out_stream);
    }
    out_stream << "}\n";
  }

  void AnalyzeLoop(const ir::Loop& loop) {
    for (uint32_t id : loop.GetBlocks()) {
      ir::BasicBlock* block = context_->cfg()->block(id);

      for (ir::Instruction& inst : *block) {
        if (inst.opcode() == SpvOp::SpvOpStore ||
            inst.opcode() == SpvOp::SpvOpLoad) {
          const ir::Instruction* access_chain =
              context_->get_def_use_mgr()->GetDef(
                  inst.GetSingleWordInOperand(0));

          for (uint32_t i = 1u; i < access_chain->NumInOperands(); ++i) {
            const ir::Instruction* index = context_->get_def_use_mgr()->GetDef(
                access_chain->GetSingleWordInOperand(i));
            AnalyzeInstruction(index);
          }
        }
      }
    }
  }

  SENode* CreateNegation(SENode* operand);

  SENode* CreateAddNode(SENode* operand_1, SENode* operand_2);

  SENode* AnalyzeInstruction(const ir::Instruction* inst);

  SENode* SimplifyExpression(SENode*);

  SENode* CloneGraphFromNode(SENode* node);

  // If the graph contains a recurrent expression, ie, an expression with the
  // loop iterations as a term in the expression, then the whole expression can
  // be rewritten to be a recurrent expression.
  SENode* GetRecurrentExpression(SENode*);

  bool CanProveEqual(const SENode& source, const SENode& destination);
  bool CanProveNotEqual(const SENode& source, const SENode& destination);

  SENode* GetCachedOrAdd(SENode* perspective_node);

 private:
  ir::IRContext* context_;
  std::map<uint32_t, SENode*> scalar_evolutions_;

  struct NodePointersEquality {
    bool operator()(const SENode* const lhs, const SENode* const rhs) const {
      return *lhs == *rhs;
    }
  };

  std::unordered_set<SENode*, SENodeHash, NodePointersEquality> node_cache_;

  SENode* AnalyzeConstant(const ir::Instruction* inst);
  SENode* AnalyzeAddOp(const ir::Instruction* add);

  SENode* AnalyzeLoadOp(const ir::Instruction* load);

  SENode* AnalyzeMultiplyOp(const ir::Instruction* multiply);

  SENode* AnalyzeDivideOp(const ir::Instruction* divide);

  SENode* AnalyzePhiInstruction(const ir::Instruction* phi);
};

}  // namespace opt
}  // namespace spvtools

#endif  // LIBSPIRV_OPT_SCALAR_ANALYSIS_H__
