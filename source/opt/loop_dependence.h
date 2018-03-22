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

#ifndef SOURCE_OPT_LOOP_DEPENDENCE_H_
#define SOURCE_OPT_LOOP_DEPENDENCE_H_

#include <algorithm>
#include <cstdint>
#include <map>
#include <ostream>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "opt/instruction.h"
#include "opt/ir_context.h"
#include "opt/loop_descriptor.h"
#include "opt/scalar_analysis.h"

namespace spvtools {
namespace opt {

class DistanceEntry {
 public:
  enum DependenceInformation {
    UNKNOWN = 0,
    DIRECTION = 1,
    DISTANCE = 2,
    PEEL = 3
  };
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
  DependenceInformation dependence_information;
  Directions direction;
  int64_t distance;
  bool peel_first;
  bool peel_last;
  DistanceEntry()
      : dependence_information(DependenceInformation::UNKNOWN),
        direction(Directions::ALL),
        distance(0),
        peel_first(false),
        peel_last(false) {}

  explicit DistanceEntry(Directions direction_)
      : dependence_information(DependenceInformation::DISTANCE),
        direction(direction_),
        distance(0),
        peel_first(false),
        peel_last(false) {}
  bool operator==(const DistanceEntry& rhs) const {
    return direction == rhs.direction && peel_first == rhs.peel_first &&
           peel_last == rhs.peel_last && distance == rhs.distance;
  }
  bool operator!=(const DistanceEntry& rhs) const { return !(*this == rhs); }
};

class DistanceVector {
 public:
  explicit DistanceVector(size_t size) : entries(size, DistanceEntry{}) {}
  explicit DistanceVector(std::vector<DistanceEntry> entries_)
      : entries(entries_) {}

  std::vector<DistanceEntry> entries;

  bool operator==(const DistanceVector& rhs) const {
    if (entries.size() != rhs.entries.size()) {
      return false;
    }
    for (size_t i = 0; i < entries.size(); ++i) {
      if (entries[i] != rhs.entries[i]) {
        return false;
      }
    }
    return true;
  }
  bool operator!=(const DistanceVector& rhs) const { return !(*this == rhs); }
};

class LoopDependenceAnalysis {
 public:
  LoopDependenceAnalysis(ir::IRContext* context,
                         std::vector<const ir::Loop*> loops)
      : context_(context),
        loops_(loops),
        scalar_evolution_(context),
        debug_stream_(nullptr) {}

  // Finds the dependence between |source| and |destination|.
  // |source| should be an OpLoad.
  // |destination| should be an OpStore.
  // Any direction and distance information found will be stored in
  // |distance_vector|.
  // Returns true if independence is found, false otherwise.
  bool GetDependence(const ir::Instruction* source,
                     const ir::Instruction* destination,
                     DistanceVector* distance_vector);

  // Returns true if |subscript_pair| represents a ZIV pair
  bool IsZIV(const std::pair<SENode*, SENode*>& subscript_pair);

  // Returns true if |subscript_pair| represents a SIV pair
  bool IsSIV(const std::pair<SENode*, SENode*>& subscript_pair);

  // Returns true if |subscript_pair| represents a MIV pair
  bool IsMIV(const std::pair<SENode*, SENode*>& subscript_pair);

  // Finds the lower bound of |loop| as an SENode* and returns the result.
  // The lower bound is the starting value of the loops induction variable
  SENode* GetLowerBound(const ir::Loop* loop);

  // Finds the upper bound of |loop| as an SENode* and returns the result.
  // The upper bound is the last value before the loop exit condition is met.
  SENode* GetUpperBound(const ir::Loop* loop);

  // Returns true if |value| is between |bound_one| and |bound_two| (inclusive).
  bool IsWithinBounds(int64_t value, int64_t bound_one, int64_t bound_two);

  // Finds the bounds of |loop| as upper_bound - lower_bound and returns the
  // resulting SENode.
  // If the operations can not be completed a nullptr is returned.
  SENode* GetTripCount(const ir::Loop* loop);

  // Returns the SENode* produced by building an SENode from the result of
  // calling GetInductionInitValue on |loop|.
  // If the operation can not be completed a nullptr is returned.
  SENode* GetFirstTripInductionNode(const ir::Loop* loop);

  // Returns the SENode* produced by building an SENode from the result of
  // GetFirstTripInductionNode + (GetTripCount - 1) * induction_coefficient.
  // If the operation can not be completed a nullptr is returned.
  SENode* GetFinalTripInductionNode(const ir::Loop* loop,
                                    SENode* induction_coefficient);

  std::set<const ir::Loop*> CollectLoops(
      const std::vector<SERecurrentNode*>& nodes);

  std::set<const ir::Loop*> CollectLoops(SENode* source, SENode* destination);

  // Returns true if |distance| is provably within the loop bounds.
  // This method is able to handle some symbolic cases which IsWithinBounds
  // can't handle.
  bool IsProvablyOutwithLoopBounds(const ir::Loop* loop, SENode* distance,
                                   SENode* coefficient);

  // Sets the ostream for debug information for the analysis.
  void SetDebugStream(std::ostream& debug_stream) {
    debug_stream_ = &debug_stream;
  }

  // Clears the stored ostream to stop debug information printing.
  void ClearDebugStream() { debug_stream_ = nullptr; }

  // Returns the ScalarEvolutionAnalysis used by this analysis.
  ScalarEvolutionAnalysis* GetScalarEvolution() { return &scalar_evolution_; }

  // Partitions the subscripts into independent subscripts and minimally coupled
  // sets of subscripts.
  // Returns the partitioning of subscript pairs. Sets of size 1 indicates an
  // independent subscript-pair and others indicate coupled sets.
  std::vector<std::set<std::pair<ir::Instruction*, ir::Instruction*>>>
  PartitionSubscripts(
      const std::vector<ir::Instruction*>& source_subscripts,
      const std::vector<ir::Instruction*>& destination_subscripts);

  // Returns the ir::Loop* matching the loop for |subscript_pair|.
  // |subscript_pair| must be an SIV pair.
  const ir::Loop* GetLoopForSubscriptPair(
      std::pair<SENode*, SENode*>* subscript_pair);

  // Returns the DistanceEntry matching the loop for |subscript_pair|.
  // |subscript_pair| must be an SIV pair.
  DistanceEntry* GetDistanceEntryForSubscriptPair(
      std::pair<SENode*, SENode*>* subscript_pair,
      DistanceVector* distance_vector);

  // Returns the DistanceEntry matching |loop|.
  DistanceEntry* GetDistanceEntryForLoop(const ir::Loop* loop,
                                         DistanceVector* distance_vector);

  // Returns a vector of Instruction* which form the subscripts of the array
  // access defined by the access chain |instruction|.
  std::vector<ir::Instruction*> GetSubscripts(
      const ir::Instruction* instruction);

  // Returns true if each loop in |loops| is in a form supported by this
  // analysis.
  // A loop is supported if it has a single induction variable and that
  // induction variable has a step of +1 or -1 per loop iteration.
  bool CheckSupportedLoops(std::vector<const ir::Loop*> loops);

  // Returns true if |loop| is in a form support by this analysis.
  // A loop is supported if it has a single induction variable and that
  // induction variable has a step of +1 or -1 per loop iteration.
  bool IsSupportedLoop(const ir::Loop* loop);

 private:
  ir::IRContext* context_;

  // The loop nest we are analysing the dependence of.
  std::vector<const ir::Loop*> loops_;

  // The ScalarEvolutionAnalysis used by this analysis to store and perform much
  // of its logic.
  ScalarEvolutionAnalysis scalar_evolution_;

  // The ostream debug information for the analysis to print to.
  std::ostream* debug_stream_;

  // Returns true if independence can be proven and false if it can't be proven.
  bool ZIVTest(SENode* source, SENode* destination);

  // Analyzes the subscript pair to find an applicable SIV test.
  // Returns true if independence and be proven and false if it can't be proven.
  bool SIVTest(std::pair<SENode*, SENode*>* subscript_pair,
               DistanceVector* distance_vector);

  // Takes the form a*i + c1, a*i + c2
  // When c1 and c2 are loop invariant and a is constant
  // distance = (c1 - c2)/a
  //              < if distance > 0
  // direction =  = if distance = 0
  //              > if distance < 0
  // Returns true if independence is proven and false if it can't be proven.
  bool StrongSIVTest(SENode* source, SENode* destination, SENode* coeff,
                     DistanceEntry* distance_entry);

  // Takes for form a*i + c1, a*i + c2
  // where c1 and c2 are loop invariant and a is constant.
  // c1 and/or c2 contain one or more SEValueUnknown nodes.
  bool SymbolicStrongSIVTest(SENode* source, SENode* destination,
                             SENode* coefficient,
                             DistanceEntry* distance_entry);

  // Takes the form a1*i + c1, a2*i + c2
  // where a1 = 0
  // distance = (c1 - c2) / a2
  // Returns true if independence is proven and false if it can't be proven.
  bool WeakZeroSourceSIVTest(SENode* source, SERecurrentNode* destination,
                             SENode* coefficient,
                             DistanceEntry* distance_entry);

  // Takes the form a1*i + c1, a2*i + c2
  // where a2 = 0
  // distance = (c2 - c1) / a1
  // Returns true if independence is proven and false if it can't be proven.
  bool WeakZeroDestinationSIVTest(SERecurrentNode* source, SENode* destination,
                                  SENode* coefficient,
                                  DistanceEntry* distance_entry);

  // Takes the form a1*i + c1, a2*i + c2
  // where a1 = -a2
  // distance = (c2 - c1) / 2*a1
  // Returns true if independence is proven and false if it can't be proven.
  bool WeakCrossingSIVTest(SENode* source, SENode* destination,
                           SENode* coefficient, DistanceEntry* distance_entry);

  // Uses the def_use_mgr to get the instruction referenced by
  // SingleWordInOperand(|id|) when called on |instruction|.
  ir::Instruction* GetOperandDefinition(const ir::Instruction* instruction,
                                        int id);

  // Perform the GCD test if both, the source and the destination nodes, are in
  // the form a0*i0 + a1*i1 + ... an*in + c.
  bool GCDMIVTest(SENode* source, SENode* destination);

  // Finds the number of induction variables in |node|.
  // Returns -1 on failure.
  int64_t CountInductionVariables(SENode* node);

  // Finds the number of induction variables shared between |source| and
  // |destination|.
  // Returns -1 on failure.
  int64_t CountInductionVariables(SENode* source, SENode* destination);

  // Takes the offset from the induction variable and subtracts the lower bound
  // from it to get the constant term added to the induction.
  // Returns the resuting constant term, or nullptr if it could not be produced.
  SENode* GetConstantTerm(const ir::Loop* loop, SERecurrentNode* induction);

  // Converts |value| to a std::string and returns the result.
  // This is required because Android does not compile std::to_string.
  template <typename valueT>
  std::string ToString(valueT value) {
    std::ostringstream string_stream;
    string_stream << value;
    return string_stream.str();
  }

  // Prints |debug_msg| and "\n" to the ostream pointed to by |debug_stream_|.
  // Won't print anything if |debug_stream_| is nullptr.
  void PrintDebug(std::string debug_msg);
};

}  // namespace opt
}  // namespace spvtools

#endif  // SOURCE_OPT_LOOP_DEPENDENCE_H__
