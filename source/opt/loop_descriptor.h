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

#ifndef LIBSPIRV_OPT_LOOP_DESCRIPTORS_H_
#define LIBSPIRV_OPT_LOOP_DESCRIPTORS_H_

#include <cstdint>
#include <map>
#include <vector>
#include "opt/module.h"
#include "opt/pass.h"

namespace spvtools {
namespace opt {

// A class to represent a loop.
class Loop {
 public:
  Loop()
      : ir_context(nullptr),
        dom_analysis(nullptr),
        loop_start_(nullptr),
        loop_continue_(nullptr),
        loop_merge_(nullptr),
        parent_(nullptr) {}

  Loop(ir::BasicBlock* begin, ir::BasicBlock* continue_target,
       ir::BasicBlock* merge_target, ir::IRContext* context,
       opt::DominatorAnalysis* analysis)
      : ir_context(context),
        dom_analysis(analysis),
        loop_start_(begin),
        loop_continue_(continue_target),
        loop_merge_(merge_target),
        parent_(nullptr) {}

  // Get the BasicBlock containing the original OpLoopMerge instruction.
  inline ir::BasicBlock* GetStartBB() { return loop_start_; }

  // Get the BasicBlock which is the start of the body of the loop.
  inline ir::BasicBlock* GetContinueBB() { return loop_continue_; }

  // Get the BasicBlock which marks the end of the loop.
  inline ir::BasicBlock* GetMergeBB() { return loop_merge_; }

  // Return true if this loop contains any nested loops.
  inline bool HasNestedLoops() const { return nested_loops_.size() != 0; }

  // Return the number of nested loops this loop contains.
  inline size_t GetNumNestedLoops() const { return nested_loops_.size(); }

  // Add a nested loop to this loop.
  inline void AddNestedLoop(Loop* nested) { nested_loops_.push_back(nested); }

  void SetParent(Loop* parent) { parent_ = parent; }

  Loop* GetParent() { return parent_; }
  // Return true if this loop is itself nested within another loop.
  inline bool IsNested() const { return parent_ != nullptr; }

  struct LoopVariable {
    ir::Instruction* def;
    ir::Instruction* step_instruction;
    uint32_t value;
    bool is_invariant;
  };

 private:
  ir::IRContext* ir_context;

  // The loop is constructed using the dominator analysis and it keeps a pointer
  // to that analysis for later reference.
  opt::DominatorAnalysis* dom_analysis;

  // The block which marks the start of the loop.
  ir::BasicBlock* loop_start_;

  // The block which begins the body of the loop.
  ir::BasicBlock* loop_continue_;

  // The block which marks the end of the loop.
  ir::BasicBlock* loop_merge_;

  Loop* parent_;

  // Nested child loops of this loop.
  std::vector<Loop*> nested_loops_;

  // If we fail to extract all the information about the loop or run into
  // unexpected instructions/form, we should mark the loop as an invalid target
  // for optimisation to preserve correctness.
  //  bool optimisation_is_valid;

  // Induction variable.
  std::unique_ptr<LoopVariable> induction_variable;

  // A set of all the basic blocks which comprise the loop structure. Will be
  // computed only when needed on demand.
  std::set<const ir::BasicBlock*> loop_basic_blocks;
  void FindInductionVariable();
  bool GetConstant(const ir::Instruction* inst, uint32_t* value) const;

  // Returns an OpVariable instruction or null from a load_inst.
  ir::Instruction* GetVariable(const ir::Instruction* load_inst);

  // Populates the set of basic blocks in the loop.
  void FindLoopBasicBlocks();

  bool IsLoopInvariant(const ir::Instruction* variable_inst);

  bool IsConstantOnEntryToLoop(const ir::Instruction* variable_inst) const;
};

class LoopDescriptor {
 public:
  // Creates a loop object for all loops found in |f|.
  explicit LoopDescriptor(const ir::Function* f);

  // Return the number of loops found in the function.
  size_t NumLoops() const { return loops_.size(); }

  // Return the loop at a particular |index|. The |index| must be in bounds,
  // check with NumLoops before calling.
  inline Loop& GetLoop(size_t index) {
    assert(loops_.size() > index &&
           "Index out of range (larger than loop count)");
    return loops_[index];
  }

 private:
  void PopulateList(const ir::Function* f);

  // A list of all the loops in the function.
  std::vector<Loop> loops_;
};

}  // namespace opt
}  // namespace spvtools

#endif  // LIBSPIRV_OPT_LOOP_DESCRIPTORS_H_
