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

struct DistanceVector {
  enum Directions {
    NONE = 0,
    LT = 1,
    EQ = 2,
    LE = 3,
    GT = 4,
    NE = 5,
    GE = 6,
    ALL = 7
  };
  Directions direction;
  bool peel_first;
  bool peel_last;
  int64_t distance;
  DistanceVector()
      : direction(Directions::ALL),
        peel_first(false),
        peel_last(false),
        distance(0) {}
};

class LoopDependenceAnalysis {
 public:
  LoopDependenceAnalysis(ir::IRContext* context, const ir::Loop& loop)
      : context_(context),
        loop_(loop),
        scalar_evolution_(context),
        debug_stream_(nullptr){};

  // Finds the dependence between |source| and |destination|.
  // |source| should be an OpLoad.
  // |destination| should be an OpStore.
  // Any direction and distance information found will be stored in
  // |distance_vector|.
  // Returns true if independence is found, false otherwise.
  bool GetDependence(const ir::Instruction* source,
                     const ir::Instruction* destination,
                     DistanceVector* distance_vector);

  // Sets the ostream for debug information for the analysis.
  // Set to nullptr to disable debug information.
  void SetDebugStream(std::ostream& debug_stream) {
    debug_stream_ = &debug_stream;
  }

 private:
  ir::IRContext* context_;

  // The loop we are analysing the dependence of.
  const ir::Loop& loop_;

  // The ScalarEvolutionAnalysis used by this analysis to store and perform much
  // of its logic.
  ScalarEvolutionAnalysis scalar_evolution_;

  // The ostream debug information for the analysis to print to.
  std::ostream* debug_stream_;

  // Returns true if independence can be proven and false if it can't be proven.
  bool ZIVTest(SENode* source, SENode* destination,
               DistanceVector* distance_vector);

  // Takes the form a*i + c1, a*i + c2
  // When c1 and c2 are loop invariant and a is constant
  // distance = (c1 - c2)/a
  //              < if distance > 0
  // direction =  = if distance = 0
  //              > if distance < 0
  // Returns true if independence is proven and false if it can't be proven.
  bool StrongSIVTest(SENode* source, SENode* destination, SENode* coeff,
                     DistanceVector* distance_vector);

  // Takes for form a*i + c1, a*i + c2
  // where c1 and c2 are loop invariant and a is constant.
  // c1 and/or c2 contain one or more SEValueUnknown nodes.
  bool SymbolicStrongSIVTest(SENode* source, SENode* destination,
                             DistanceVector* distance_vector);

  // Takes the form a1*i + c1, a2*i + c2
  // where a1 = 0
  // distance = (c1 - c2) / a2
  // Returns true if independence is proven and false if it can't be proven.
  bool WeakZeroSourceSIVTest(SENode* source, SERecurrentNode* destination,
                             SENode* coefficient,
                             DistanceVector* distance_vector);

  // Takes the form a1*i + c1, a2*i + c2
  // where a2 = 0
  // distance = (c2 - c1) / a1
  // Returns true if independence is proven and false if it can't be proven.
  bool WeakZeroDestinationSIVTest(SERecurrentNode* source, SENode* destination,
                                  SENode* coefficient,
                                  DistanceVector* distance_vector);

  // Takes the form a1*i + c1, a2*i + c2
  // where a1 = -a2
  // distance = (c2 - c1) / 2*a1
  // Returns true if independence is proven and false if it can't be proven.
  bool WeakCrossingSIVTest(SENode* source, SENode* destination,
                           SENode* coefficient,
                           DistanceVector* distance_vector);

  // Finds the lower bound of the loop as an SENode* and returns the resulting
  // SENode. The lower bound is evaluated as the bound with the lesser signed
  // value.
  // If the operations can not be completed a nullptr is returned.
  SENode* GetLowerBound();

  // Finds the upper bound of the loop as an SENode* and returns the resulting
  // SEnode. The upper bound is evaluated as the bound with the greater signed
  // value.
  // If the operations can not be completed a nullptr is returned.
  SENode* GetUpperBound();

  // Finds the lower and upper bounds of the loop as SENode* and returns a pair
  // of the resulting SENodes.
  // Either or both of the pointers in the std::pair may be nullptr if the
  // bounds could not be found.
  std::pair<SENode*, SENode*> GetLoopLowerUpperBounds();

  // Returns true if |value| is between |bound_one| and |bound_two| (inclusive).
  bool IsWithinBounds(int64_t value, int64_t bound_one, int64_t bound_two);

  // Returns true if |distance| is provably within the loop bounds.
  // This method is able to handle a small number of symbolic cases not handled
  // by IsWithinBounds.
  bool IsProvablyOutwithLoopBounds(SENode* distance);

  // Finds the loop bounds as upper_bound - lower_bound and returns the
  // resulting SENode.
  // If the operations can not be completed a nullptr is returned.
  SENode* GetTripCount();

  // Finds the value of the induction variable at the first trip of the loop and
  // returns the resulting SENode.
  // If the operation can not be completed a nullptr is returned.
  SENode* GetFirstTripInductionNode();

  // Finds the value of the induction variable at the final trip of the loop and
  // returns the resulting SENode.
  // If the operation can not be completed a nullptr is returned.
  SENode* GetFinalTripInductionNode(SENode* induction_coefficient);

  // Finds the number of induction variables in |node|.
  // Returns -1 on failure.
  int64_t CountInductionVariables(SENode* node);

  // Finds the number of induction variables shared between |source| and
  // |destination|.
  // Returns -1 on failure.
  int64_t CountInductionVariables(SENode* source, SENode* destination);

  // Prints |debug_msg| and "\n" to the ostream pointed to by |debug_stream_|.
  // Won't print anything if |debug_stream_| is nullptr.
  void PrintDebug(std::string debug_msg);
};

}  // namespace ir
}  // namespace spvtools

#endif  // LIBSPIRV_OPT_LOOP_DEPENDENCE_H__
