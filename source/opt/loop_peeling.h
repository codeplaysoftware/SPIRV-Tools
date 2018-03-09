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
#include <vector>

#include "opt/ir_context.h"
#include "opt/loop_descriptor.h"
#include "opt/loop_utils.h"
#include "opt/pass.h"
#include "opt/scalar_analysis.h"

namespace spvtools {
namespace opt {

// Utility class to perform the actual peeling of a given loop.
class LoopPeeling {
 public:
  explicit LoopPeeling(ir::Loop* loop)
      : context_(loop->GetContext()),
        loop_utils_(loop->GetContext(), loop),
        loop_(loop),
        extra_induction_variable_(nullptr) {
    assert(loop_->IsLCSSA() && "Loop is not in LCSSA form");
    GetIteratingExitValue();
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

  // Returns the first loop (peeled loop for PeelBefore).
  ir::Loop* GetBeforeLoop() { return new_loop_; }
  // Returns the second loop (peeled loop for PeelAfter).
  ir::Loop* GetAfterLoop() { return loop_; }
  // Returns the induction variable build and use to peel the loop.
  ir::Instruction* GetExtraInductionVariable() {
    return extra_induction_variable_;
  }

 private:
  ir::IRContext* context_;
  LoopUtils loop_utils_;
  // The original loop.
  ir::Loop* loop_;
  // Peeled loop.
  ir::Loop* new_loop_;
  // This is set to true when the exit and back-edge branch instruction is the
  // same.
  bool do_while_form_;

  ir::Instruction* extra_induction_variable_;

  // Map between loop iterators and exit values.
  std::unordered_map<uint32_t, ir::Instruction*> exit_value_;

  // Duplicate |loop_| and place the new loop before the cloned loop.
  // |loop_| must be in LCSSA form and have a merge block with a using
  // incoming
  // branch (i.e. must not contain a break).
  void DuplicateLoop();

  // Insert an induction variable into the first loop as a simplified counter.
  // Fixme(Victor): with a scalar evolution, this can removed.
  void InsertIterator(ir::Instruction* factor);

  // Fixes the exit condition of the before loop. The function calls
  // |condition_builder| to get the condition to use in the conditional branch
  // of the loop exit. The loop will be exited if the condition evaluate to
  // true.
  void FixExitCondition(
      const std::function<uint32_t(ir::BasicBlock*)>& condition_builder);

  // Connects iterating values so that loop like
  // int z = 0;
  // for (int i = 0; i++ < M; i += cst1) {
  //   if (cond)
  //     z += cst2;
  // }
  //
  // Becomes:
  //
  // int z = 0;
  // int i = 0;
  // for (; i++ < M; i += cst1) {
  //   if (cond)
  //     z += cst2;
  // }
  // for (; i++ < M; i += cst1) {
  //   if (cond)
  //     z += cst2;
  // }
  void ConnectIterators(const LoopUtils::LoopCloningResult& clone_results);

  // Gathers all operations involved in the update of |iterator| into
  // |operations|.
  void GetIteratorUpdateOperations(
      const ir::Loop* loop, ir::Instruction* iterator,
      std::unordered_set<ir::Instruction*>* operations);

  // Gathers exiting iterator values. The function builds a map between each
  // iterating value in the loop (a phi instruction in the loop header) and its
  // SSA value when it exit the loop. If no exit value can be accurately found,
  // it is map to nullptr (see comment on CanPeelLoop).
  void GetIteratingExitValue();
};

// Implements a loop peeling optimization.
// For each loop, the pass will try to peel it if there is conditions that
// are true for the "N" first or last iterations of the loop.
// To avoid code size explosion, too large loops will not be peeled.
class LoopPeelingPass : public Pass {
  enum class PeelDirection {
    None,    // Cannot be peeled
    Before,  // Can be peeled before
    Last     // Can be peeled last
  };

  class LoopPeelingInfo {
   public:
    using LoopPeelDirection = std::pair<PeelDirection, uint32_t>;

    LoopPeelingInfo(ir::Loop* loop, size_t loop_max_iterations,
                    opt::ScalarEvolutionAnalysis* scev_analysis)
        : context_(loop->GetContext()),
          loop_(loop),
          scev_analysis_(scev_analysis),
          loop_max_iterations_(loop_max_iterations) {}

    LoopPeelDirection GetPeelingInfo(ir::BasicBlock* bb);

   private:
    ir::IRContext* context_;
    ir::Loop* loop_;
    opt::ScalarEvolutionAnalysis* scev_analysis_;
    // SENode* max_iterations_;
    // SENode* rec_expr_;
    size_t loop_max_iterations_;

    uint32_t GetFirstLoopInvariantOperand(ir::Instruction* condition) const;
    uint32_t GetFirstNonLoopInvariantOperand(ir::Instruction* condition) const;

    bool DivideNodes(SENode* lhs, SENode* rhs, int64_t* result) const;
    SENode* GetLastIterationValue(SERecurrentNode* rec) const;

    SENode* GetIterationValueAt(SERecurrentNode* rec, SENode* x) const;

    LoopPeelDirection HandleEqual(SpvOp opcode, SENode* lhs, SENode* rhs) const;
    LoopPeelDirection HandleGreaterThan(SpvOp opcode, bool handle_ge,
                                        SENode* lhs, SENode* rhs) const;
    LoopPeelDirection HandleLessThan(SpvOp opcode, bool handle_le, SENode* lhs,
                                     SENode* rhs) const;

    static LoopPeelDirection GetNoneDirection() {
      return LoopPeelDirection{LoopPeelingPass::PeelDirection::None, 0};
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
