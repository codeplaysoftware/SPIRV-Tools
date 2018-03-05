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

struct DVEntry {
  enum { NONE = 0, LT = 1, EQ = 2, LE = 3, GT = 4, NE = 5, GE = 6, ALL = 7 };
  unsigned char direction : 3;
  bool peel_first : 1;
  bool peel_last : 1;
  int distance;
  DVEntry() : direction(ALL), distance(0) {}
};

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

  // Returns true if independence can be proven and false if it can't be proven
  bool ZIVTest(SENode* source, SENode* destination, DVEntry* dv_entry);

  // Takes the form a*i + c1, a*i + c2
  // When c1 and c2 are loop invariant and a is constant
  // distance = (c1 - c2)/a
  //              < if distance > 0
  // direction =  = if distance = 0
  //              > if distance < 0
  // Returns true if independence is proven and false if it can't be proven
  bool StrongSIVTest(SENode* source, SENode* destination, SENode* coeff,
                     DVEntry* dv_entry);

  // Takes the form a1*i + c1, a2*i + c2
  // when a1 = 0
  // distance = (c2 - c1) / a2
  // Returns true if independence is proven and false if it can't be proven
  bool WeakZeroSourceSIVTest(SENode* source, SENode* destination,
                             SENode* coefficient, DVentry* dv_entry);

  // Takes the form a1*i + c1, a2*i + c2
  // when a2 = 0
  // distance = (c2 - c1) / a1
  // Returns true if independence is proven and false if it can't be proven
  bool WeakZeroDestinationSIVTest(SENode* source, SENode* destination,
                                  SENode* coefficient, DVentry* dv_entry);

  // Takes the form a1*i + c1, a2*i + c2
  // When a1 = -a2
  // distance = (c2 - c1) / 2*a1
  // Returns true if independence is proven and false if it can't be proven
  bool WeakCrossingSIVTest(SENode* source, SENode* destination,
                           SENode* coefficient, DVentry* dv_entry);

  // Takes the form a1*i + c1, a2*i + c2
  // Where a1 and a2 are constant and different
  // Returns true if independence is proven and false if it can't be proven
  bool WeakSIVTest(SENode* source, SENode* destination, SENode* src_coeff,
                   SENode* dest_coeff, DVEntry* dv_entry);

  bool BannerjeeGCDtest(SENode* source, SENode* destination, DVEntry* dv_entry);

  bool DeltaTest(SENode* source, SENode* direction);

  // Finds the lower bound of the loop as an SENode* and stores it in
  // |lower_bound|
  // Returns true if successful
  bool GetLowerBound(SENode** lower_bound);

  // Finds the upper bound of the loop as an SENode* and stores it in
  // |upper_bound|
  // Returns true if successful
  bool GetUpperBound(SENode** upper_bound);

  // Finds the lower and upper bounds of the loop as SENode* and stores them in
  // |lower_bound| and |upper_bound| respectively
  // Returns true isf successful
  bool GetLoopLowerUpperBounds(SENode** lower_bound, SENode** upper_bound);

  // Finds the loop bounds as upper_bound - lower_bound and stores the
  // resulting SENode in |loop_bounds|
  // Returns true if successful
  bool GetTripCount(SENode* loop_bounds);
};

}  // namespace ir
}  // namespace spvtools

#endif  // LIBSPIRV_OPT_LOOP_DEPENDENCE_H__
