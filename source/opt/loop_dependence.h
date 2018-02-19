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

#ifndef LIBSPIRV_OPT_LOOP_DEPENDENCE_H_
#define LIBSPIRV_OPT_LOOP_DEPENDENCE_H_

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "opt/basic_block.h"
#include "opt/tree_iterator.h"
#include "opt/loop_descriptor.h"

namespace spvtools {
namespace opt {


class LoopDependenceAnalysis {
public:
  LoopDependenceAnalysis(const ir::Loop& loop):loop_(loop) { };

  int32_t DependenceVectorForInduction(ir::Instruction*, int64_t step,
                                       int64_t start) const {
    return static_cast<int32_t>(step - start);
  }

  void DumpIterationSpaceAsDot(std::ostream& out_stream) const {
    out_stream << "digraph {\n";
    ir::BasicBlock* loop_condition = loop_.FindConditionBlock();

    ir::Instruction* induction = loop_.FindInductionVariable(loop_condition);

    size_t iterations = 0;
    int64_t step_amount = 0;
    int64_t init_value = 0;

    bool found = loop_.FindNumberOfIterations(induction, &*loop_condition->ctail(), &iterations,
                                        &step_amount, &init_value);
    uint32_t vector =
        DependenceVectorForInduction(induction, step_amount, init_value);

    if (found) {
      for (size_t iteration = 0; iteration < iterations; ++iteration) {
        out_stream << iteration << "[label=\"" << iteration << "\"];\n";
        out_stream << iteration << " -> " << iteration + vector << ";\n";
      }
    }

    out_stream << "}\n";
  }
private:

  // The loop we are analysing the dependence of.
  const ir::Loop& loop_;
};

}  // namespace ir
}  // namespace spvtools

#endif  // LIBSPIRV_OPT_LOOP_DEPENDENCE_H__
