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
#include <memory>
#include <vector>

#include "opt/module.h"
#include "opt/pass.h"

namespace spvtools {
namespace ir {
class CFG;
}  // namespace ir

namespace opt {

struct InductionVariable {
  InductionVariable()
      : def_(nullptr),
        init_value_(0),
        step_amount_(0),
        end_value_(0),
        end_condition_(nullptr) {}

  InductionVariable(ir::Instruction* d, int32_t init_value, int32_t step_amount,
                    int32_t end_val, ir::Instruction* condition)
      : def_(d),
        init_value_(init_value),
        step_amount_(step_amount),
        end_value_(end_val),
        end_condition_(condition) {}
  ir::Instruction* def_;
  int32_t init_value_;
  int32_t step_amount_;
  int32_t end_value_;
  ir::Instruction* end_condition_;
};

// A class to represent and manipulate a loop.
class Loop {
  // The type used to represent nested child loops.
  using ChildrenList = std::vector<Loop*>;

 public:
  using iterator = ChildrenList::iterator;
  using const_iterator = ChildrenList::const_iterator;
  using BasicBlockListTy = std::set<const ir::BasicBlock*>;

  Loop()
      : ir_context_(nullptr),
        dom_analysis_(nullptr),
        loop_header_(nullptr),
        loop_continue_(nullptr),
        loop_merge_(nullptr),
        loop_preheader_(nullptr),
        parent_(nullptr),
        induction_variable_() {}

  Loop(ir::BasicBlock* header, ir::BasicBlock* continue_target,
       ir::BasicBlock* merge_target, ir::IRContext* context,
       opt::DominatorAnalysis* analysis);

  // Iterators which allows access to the nested loops.
  iterator begin() { return nested_loops_.begin(); }
  iterator end() { return nested_loops_.end(); }
  const_iterator begin() const { return cbegin(); }
  const_iterator end() const { return cend(); }
  const_iterator cbegin() const { return nested_loops_.begin(); }
  const_iterator cend() const { return nested_loops_.end(); }

  // Get the header (first basic block of the loop). This block contains the
  // OpLoopMerge instruction.
  inline ir::BasicBlock* GetHeaderBlock() { return loop_header_; }
  inline const ir::BasicBlock* GetHeaderBlock() const { return loop_header_; }

  // Get the latch basic block (basic block that holds the back-edge).
  inline ir::BasicBlock* GetLatchBlock() { return loop_continue_; }
  inline const ir::BasicBlock* GetLatchBlock() const { return loop_continue_; }

  // Get the BasicBlock which marks the end of the loop.
  inline ir::BasicBlock* GetMergeBlock() { return loop_merge_; }
  inline const ir::BasicBlock* GetMergeBlock() const { return loop_merge_; }

  // Get the BasicBlock which immediately precedes the loop header.
  inline const ir::BasicBlock* GetPreheaderBlock() const {
    return loop_preheader_;
  }

  // Return true if this loop contains any nested loops.
  inline bool HasNestedLoops() const { return nested_loops_.size() != 0; }

  // Return the depth of this loop in the loop nest.
  // The outer-most loop has a depth of 1.
  inline size_t GetDepth() const {
    size_t lvl = 1;
    for (const Loop* loop = GetParent(); loop; loop = loop->GetParent()) lvl++;
    return lvl;
  }

  // Add a nested loop to this loop.
  inline void AddNestedLoop(Loop* nested) {
    assert(!nested->GetParent() && "The loop has another parent.");
    nested_loops_.push_back(nested);
    nested->SetParent(this);
  }

  Loop* GetParent() { return parent_; }
  const Loop* GetParent() const { return parent_; }

  // Return true if this loop is itself nested within another loop.
  inline bool IsNested() const { return parent_ != nullptr; }

  // Gets or if unitialised, sets, the induction variable for the loop.
  InductionVariable* GetInductionVariable();

  // Returns the set of all basic blocks contained within the loop. Will be all
  // BasicBlocks dominated by the header which are not also dominated by the
  // loop merge block.
  const BasicBlockListTy& GetBlocks() const { return loop_basic_blocks_; }

  ir::IRContext* GetContext() const { return ir_context_; }

 private:
  ir::IRContext* ir_context_;

  // The loop is constructed using the dominator analysis and it keeps a pointer
  // to that analysis for later reference.
  opt::DominatorAnalysis* dom_analysis_;

  // The block which marks the start of the loop.
  ir::BasicBlock* loop_header_;

  // The block which begins the body of the loop.
  ir::BasicBlock* loop_continue_;

  // The block which marks the end of the loop.
  ir::BasicBlock* loop_merge_;

  // The block immediately before the loop header.
  ir::BasicBlock* loop_preheader_;

  // A parent of a loop is the loop which contains it as a nested child loop.
  Loop* parent_;

  // Nested child loops of this loop.
  ChildrenList nested_loops_;

  // Induction variable.
  // FIXME: That's only apply for some canonical form.
  //        Plus, only the Phi insn is really needed as other information should
  //        be trivial to recover.
  InductionVariable induction_variable_;

  // A set of all the basic blocks which comprise the loop structure. Will be
  // computed only when needed on demand.
  BasicBlockListTy loop_basic_blocks_;

  // Sets the parent loop of this loop, that is, a loop which contains this loop
  // as a nested child loop.
  void SetParent(Loop* parent) { parent_ = parent; }

  void FindInductionVariable();
  bool GetConstant(const ir::Instruction* inst, uint32_t* value) const;
  bool GetInductionInitValue(const ir::Instruction* variable_inst,
                             uint32_t* value) const;
  ir::Instruction* GetInductionStepOperation(
      const ir::Instruction* variable_inst) const;

  // Returns an OpVariable instruction or null from a load_inst.
  ir::Instruction* GetVariable(const ir::Instruction* load_inst);

  // Populates the set of basic blocks in the loop.
  void FindLoopBasicBlocks();

  bool IsLoopInvariant(const ir::Instruction* variable_inst);

  bool IsConstantOnEntryToLoop(const ir::Instruction* variable_inst) const;
};

class LoopDescriptor {
 public:
  using LoopContainerType = std::vector<std::unique_ptr<Loop>>;
  using iterator = LoopContainerType::iterator;
  // Creates a loop object for all loops found in |f|.
  explicit LoopDescriptor(const ir::Function* f);

  // Return the number of loops found in the function.
  size_t NumLoops() const { return loops_.size(); }

  // Return the loop at a particular |index|. The |index| must be in bounds,
  // check with NumLoops before calling.
  inline Loop& GetLoop(size_t index) const {
    assert(loops_.size() > index &&
           "Index out of range (larger than loop count)");
    return *loops_[index].get();
  }

  iterator begin() { return loops_.begin(); }
  iterator end() { return loops_.end(); }

 private:
  void PopulateList(const ir::Function* f);

  // A list of all the loops in the function.
  LoopContainerType loops_;
};

}  // namespace opt
}  // namespace spvtools

#endif  // LIBSPIRV_OPT_LOOP_DESCRIPTORS_H_
