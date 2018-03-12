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

#ifndef SOURCE_OPT_LOOP_PEELING_H_
#define SOURCE_OPT_LOOP_PEELING_H_

#include <algorithm>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "opt/ir_context.h"
#include "opt/loop_descriptor.h"
#include "opt/loop_utils.h"
#include "opt/pass.h"
#include "opt/scalar_analysis.h"

namespace spvtools {
namespace opt {

// Utility class to perform the peeling of a given loop.
// The loop peeling transformation make a certain amount of a loop iterations to
// be executed either before (peel before) or after (peel after) the transformed
// loop.
//
// For peeling cases the transformation does the following steps:
//   - It clones the loop and inserts the cloned loop before the original loop;
//   - It connects all iterating values of the cloned loop with the
//     corresponding original loop values so that the second loop starts with
//     the appropriate values.
//   - It inserts a new induction variable "i" is inserted into the cloned that
//     starts with the value 0 and increment by step of one.
//
// The last step is specific to each case:
//   - Peel before: the transformation is to peel the "N" first iterations.
//     The exit condition of the cloned loop is changed so that the loop
//     exits when "i < N" becomes false.
//   - Peel after: the transformation is to peel the "N" last iterations,
//     then the exit condition of the cloned loop is changed so that the loop
//     exits when "i + N < max_iteration" becomes false, where "max_iteration"
//     is the upper bound of the loop.
//
// To be peelable:
//   - The loop must be in LCSSA form;
//   - The loop must not contain any breaks;
//   - The loop must not have any ambiguous iterators updates (see
//     "CanPeelLoop").
// The method "CanPeelLoop" checks that those constrained are met.
//
// Fixme(Victor): Allow the utility it accept an canonical induction variable
// rather than automatically create one.
// Fixme(Victor): When possible, evaluate the initial value of the second loop
// iterating values rather than using the exit value of the first loop.
class LoopPeeling {
 public:
  explicit LoopPeeling(ir::Loop* loop)
      : context_(loop->GetContext()),
        loop_utils_(loop->GetContext(), loop),
        loop_(loop),
        canonical_induction_variable_(nullptr) {
    GetIteratingExitValues();
  }

  // Returns true if the loop can be peeled.
  // To be peelable, all operation involved in the update of the loop iterators
  // must not dominates the exit condition. This restriction is a work around to
  // not miss compile code like:
  //
  //   for (int i = 0; i + 1 < N; i++) {}
  //   for (int i = 0; ++i < N; i++) {}
  //
  // The increment will happen before the test on the exit condition leading to
  // very look-a-like code.
  //
  // This restriction will not apply if a loop rotate is applied before (i.e.
  // becomes a do-while loop).
  bool CanPeelLoop() {
    ir::CFG& cfg = *context_->cfg();

    if (!loop_->IsLCSSA()) {
      return false;
    }
    if (!loop_->GetMergeBlock()) {
      return false;
    }
    if (cfg.preds(loop_->GetMergeBlock()->id()).size() != 1) {
      return false;
    }

    return !std::any_of(exit_value_.cbegin(), exit_value_.cend(),
                        [](std::pair<uint32_t, ir::Instruction*> it) {
                          return it.second == nullptr;
                        });
  }

  // Moves the execution of the |factor| first iterations of the loop into a
  // dedicated loop.
  void PeelBefore(ir::Instruction* factor);

  // Moves the execution of the |factor| last iterations of the loop into a
  // dedicated loop.
  void PeelAfter(ir::Instruction* factor, ir::Instruction* iteration_count);

  // Returns the cloned loop.
  ir::Loop* GetClonedLoop() { return cloned_loop_; }
  // Returns the original loop.
  ir::Loop* GetOriginalLoop() { return loop_; }

 private:
  ir::IRContext* context_;
  LoopUtils loop_utils_;
  // The original loop.
  ir::Loop* loop_;
  // The cloned loop.
  ir::Loop* cloned_loop_;
  // This is set to true when the exit and back-edge branch instruction is the
  // same.
  bool do_while_form_;

  // The canonical induction variable of the cloned loop. The induction variable
  // is initialized to 0 and incremented by step of 1.
  ir::Instruction* canonical_induction_variable_;

  // Map between loop iterators and exit values. Loop iterators
  std::unordered_map<uint32_t, ir::Instruction*> exit_value_;

  // Duplicate |loop_| and place the new loop before the cloned loop. Iterating
  // values from the cloned loop are then connected to the original loop as
  // initializer.
  void DuplicateAndConnectLoop();

  // Insert the canonical induction variable into the first loop as a simplified
  // counter.
  void InsertCanonicalInductionVariable(ir::Instruction* factor);

  // Fixes the exit condition of the before loop. The function calls
  // |condition_builder| to get the condition to use in the conditional branch
  // of the loop exit. The loop will be exited if the condition evaluate to
  // true.
  void FixExitCondition(
      const std::function<uint32_t(ir::BasicBlock*)>& condition_builder);

  // Gathers all operations involved in the update of |iterator| into
  // |operations|.
  void GetIteratorUpdateOperations(
      const ir::Loop* loop, ir::Instruction* iterator,
      std::unordered_set<ir::Instruction*>* operations);

  // Gathers exiting iterator values. The function builds a map between each
  // iterating value in the loop (a phi instruction in the loop header) and its
  // SSA value when it exit the loop. If no exit value can be accurately found,
  // it is map to nullptr (see comment on CanPeelLoop).
  void GetIteratingExitValues();
};

// Implements a loop peeling optimization.
// For each loop, the pass will try to peel it if there is conditions that
// are true for the "N" first or last iterations of the loop.
// To avoid code size explosion, too large loops will not be peeled.
class LoopPeelingPass : public Pass {
  enum class PeelDirection {
    None,    // Cannot be peeled
    Before,  // Can be peeled before
    After    // Can be peeled last
  };

  class LoopPeelingInfo {
   public:
    using Direction = std::pair<PeelDirection, uint32_t>;

    LoopPeelingInfo(ir::Loop* loop, size_t loop_max_iterations,
                    opt::ScalarEvolutionAnalysis* scev_analysis)
        : context_(loop->GetContext()),
          loop_(loop),
          scev_analysis_(scev_analysis),
          loop_max_iterations_(loop_max_iterations) {}

    Direction GetPeelingInfo(ir::BasicBlock* bb);

   private:
    // Abstraction of <, >, <= and >=
    enum class CompareOp {
      None,  // default/invalid
      LT,    // <
      GT,    // >
      LE,    // <=
      GE     // >=
    };

    ir::IRContext* context_;
    ir::Loop* loop_;
    opt::ScalarEvolutionAnalysis* scev_analysis_;
    size_t loop_max_iterations_;

    uint32_t GetFirstLoopInvariantOperand(ir::Instruction* condition) const;
    uint32_t GetFirstNonLoopInvariantOperand(ir::Instruction* condition) const;

    SEConstantNode* Divide(SENode* dividend, SENode* divisor) const;

    SENode* GetLastIterationValue(SERecurrentNode* rec) const;

    SENode* GetIterationValueAt(SERecurrentNode* rec, SENode* x) const;

    Direction HandleEqual(SENode* lhs, SENode* rhs) const;
    Direction HandleInequality(CompareOp cmp_op, SENode* lhs,
                               SERecurrentNode* rhs) const;

    static Direction GetNoneDirection() {
      return Direction{LoopPeelingPass::PeelDirection::None, 0};
    }
  };

 public:
  const char* name() const override { return "loop-peeling"; }

  // Processes the given |module|. Returns Status::Failure if errors occur when
  // processing. Returns the corresponding Status::Success if processing is
  // succesful to indicate whether changes have been made to the modue.
  Pass::Status Process(ir::IRContext* context) override;

 private:
  unsigned code_grow_threshold_;

  // Peel profitable loops in |f|.
  bool ProcessFunction(ir::Function* f);
  // Peel |loop| if profitable.
  bool ProcessLoop(ir::Loop* loop);
};

}  // namespace opt
}  // namespace spvtools

#endif  // SOURCE_OPT_LOOP_PEELING_H_
