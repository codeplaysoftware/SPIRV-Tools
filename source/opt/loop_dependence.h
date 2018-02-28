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
#include <vector>

#include "opt/loop_descriptor.h"
#include "opt/scalar_analysis.h"
namespace spvtools {
namespace opt {

class LoopDependenceAnalysis {
 public:
  LoopDependenceAnalysis(ir::IRContext* context, const ir::Loop& loop)
      : context_(context), loop_(loop), scalar_evolution_(context){};

  bool GetDependence(const ir::Instruction* source,
                     const ir::Instruction* destination);

  void DumpIterationSpaceAsDot(std::ostream& out_stream);

 private:
  ir::IRContext* context_;

  // The loop we are analysing the dependence of.
  const ir::Loop& loop_;

  ScalarEvolutionAnalysis scalar_evolution_;

  std::map<const ir::Instruction*, std::vector<SENode*>>
      memory_access_to_indice_;

  bool ZIVTest(const SENode& source, const SENode& destination);

  bool SIVTest(SENode* source, SENode* destination);

  // Takes the form a*i + c1, a*i + c2
  // When c1 and c2 are loop invariant and a is constant
  // distance = (c1 - c2)/a
  //              < if distance > 0
  // direction =  = if distance = 0
  //              > if distance < 0
  bool StrongSIVTest(SENode* source, SENode* destination);

  // Takes the form a1*i + c1, a2*i + c2
  // Where a1 and a2 are constant and different
  bool WeakSIVTest();

  // Takes the form a1*i + c1, a2*i + c2
  // when a1 = 0
  // i = (c2 - c1) / a2
  bool WeakZeroSourceSIVTest();

  // Takes the form a1*i + c1, a2*i + c2
  // when a2 = 0
  // i = (c2 - c1) / a1
  bool WeakZeroDestinationSIVTest();

  // Takes the form a1*i + c1, a2*i + c2
  // When a1 = -a2
  // i = (c2 - c1) / 2*a1
  bool WeakCrossingSIVTest();
};

}  // namespace ir
}  // namespace spvtools

#endif  // LIBSPIRV_OPT_LOOP_DEPENDENCE_H__
