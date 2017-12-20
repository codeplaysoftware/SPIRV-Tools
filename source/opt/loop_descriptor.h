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

#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "opt/module.h"
#include "opt/pass.h"
#include "opt/tree_iterator.h"

namespace spvtools {
namespace ir {
class CFG;
class LoopDescriptor;

// A class to represent and manipulate a loop.
class Loop {
  // The type used to represent nested child loops.
  using ChildrenList = std::vector<Loop*>;

 public:
  using iterator = ChildrenList::iterator;
  using const_iterator = ChildrenList::const_iterator;
  using BasicBlockListTy = std::unordered_set<const BasicBlock*>;

  Loop()
      : ir_context_(nullptr),
        dom_analysis_(nullptr),
        loop_header_(nullptr),
        loop_continue_(nullptr),
        loop_merge_(nullptr),
        loop_preheader_(nullptr),
        parent_(nullptr) {}

  Loop(IRContext* context, opt::DominatorAnalysis* analysis, BasicBlock* header,
       BasicBlock* continue_target, BasicBlock* merge_target);

  // Iterators which allows access to the nested loops.
  inline iterator begin() { return nested_loops_.begin(); }
  inline iterator end() { return nested_loops_.end(); }
  inline const_iterator begin() const { return cbegin(); }
  inline const_iterator end() const { return cend(); }
  inline const_iterator cbegin() const { return nested_loops_.begin(); }
  inline const_iterator cend() const { return nested_loops_.end(); }

  // Get the header (first basic block of the loop). This block contains the
  // OpLoopMerge instruction.
  inline BasicBlock* GetHeaderBlock() { return loop_header_; }
  inline const BasicBlock* GetHeaderBlock() const { return loop_header_; }

  // Get the latch basic block (basic block that holds the back-edge).
  inline BasicBlock* GetLatchBlock() { return loop_continue_; }
  inline const BasicBlock* GetLatchBlock() const { return loop_continue_; }

  // Get the BasicBlock which marks the end of the loop.
  inline BasicBlock* GetMergeBlock() { return loop_merge_; }
  inline const BasicBlock* GetMergeBlock() const { return loop_merge_; }

  // Get or create the loop pre-header.
  inline BasicBlock* GetPreHeaderBlock() { return loop_preheader_; }

  // Get or create the loop pre-header.
  inline const BasicBlock* GetPreHeaderBlock() const { return loop_preheader_; }

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

  inline Loop* GetParent() { return parent_; }
  inline const Loop* GetParent() const { return parent_; }

  inline bool HasParent() const { return parent_; }

  // Return true if this loop is itself nested within another loop.
  inline bool IsNested() const { return parent_ != nullptr; }

  // Returns the set of all basic blocks contained within the loop. Will be all
  // BasicBlocks dominated by the header which are not also dominated by the
  // loop merge block.
  inline const BasicBlockListTy& GetBlocks() const {
    return loop_basic_blocks_;
  }

  inline IRContext* GetContext() const { return ir_context_; }

  // Returns true if |bb| is inside this loop.
  inline bool IsInsideLoop(const BasicBlock* bb) const {
    return loop_basic_blocks_.count(bb);
  }

  // Add a Basic Block to this loop.
  void AddBasicBlockToLoop(const BasicBlock* bb) {
    for (Loop* loop = this; loop != nullptr; loop = loop->parent_) {
      loop_basic_blocks_.insert(bb);
    }
  }

  // Returns true is the parent basic block
  inline bool IsLoopInvariant(Instruction* inst) const {
    const BasicBlock* parent_block = inst->context()->get_instr_block(inst);
    if (!parent_block) return true;
    return IsInsideLoop(parent_block);
  }

 private:
  IRContext* ir_context_;

  // The loop is constructed using the dominator analysis and it keeps a pointer
  // to that analysis for later reference.
  opt::DominatorAnalysis* dom_analysis_;

  // The block which marks the start of the loop.
  BasicBlock* loop_header_;

  // The block which begins the body of the loop.
  BasicBlock* loop_continue_;

  // The block which marks the end of the loop.
  BasicBlock* loop_merge_;

  // The block immediately before the loop header.
  BasicBlock* loop_preheader_;

  // A parent of a loop is the loop which contains it as a nested child loop.
  Loop* parent_;

  // Nested child loops of this loop.
  ChildrenList nested_loops_;

  // A set of all the basic blocks which comprise the loop structure. Will be
  // computed only when needed on demand.
  BasicBlockListTy loop_basic_blocks_;

  // Sets the parent loop of this loop, that is, a loop which contains this loop
  // as a nested child loop.
  inline void SetParent(Loop* parent) { parent_ = parent; }

  // Set the loop preheader if it exist.
  void SetLoopPreheader();

  // This is only to allow LoopDescriptor::dummy_top_loop_ to add top level
  // loops as child.
  friend class LoopDescriptor;
};

class LoopDescriptor {
 public:
  // Iterator interface (depth first postorder traversal).
  using iterator = opt::PostOrderTreeDFIterator<Loop>;
  using const_iterator = opt::PostOrderTreeDFIterator<const Loop>;

  // Creates a loop object for all loops found in |f|.
  explicit LoopDescriptor(const Function* f);

  // Return the number of loops found in the function.
  inline size_t NumLoops() const { return loops_.size(); }

  // Return the loop at a particular |index|. The |index| must be in bounds,
  // check with NumLoops before calling.
  inline Loop& GetLoopByIndex(size_t index) const {
    assert(loops_.size() > index &&
           "Index out of range (larger than loop count)");
    return *loops_[index].get();
  }

  // Return the loop descriptor which has |header_id| as loop header id.
  inline Loop* operator[](uint32_t header_id) const {
    return FindLoopForBasicBlock(header_id);
  }

  // Return the loop descriptor which has |header| as loop header.
  inline Loop* operator[](BasicBlock* bb) const { return (*this)[bb->id()]; }

  inline iterator begin() { return iterator::begin(&dummy_top_loop_); }
  inline iterator end() { return iterator::end(&dummy_top_loop_); }
  inline const_iterator begin() const { return cbegin(); }
  inline const_iterator end() const { return cend(); }
  inline const_iterator cbegin() const {
    return const_iterator::begin(&dummy_top_loop_);
  }
  inline const_iterator cend() const {
    return const_iterator::end(&dummy_top_loop_);
  }

 private:
  using LoopContainerType = std::vector<std::unique_ptr<Loop>>;

  void PopulateList(const Function* f);

  // Return the loop descriptor which has |header_id| as loop header id.
  inline Loop* FindLoopForBasicBlock(uint32_t header_id) const {
    std::unordered_map<uint32_t, Loop*>::const_iterator it =
        basic_block_to_loop_.find(header_id);
    return it != basic_block_to_loop_.end() ? it->second : nullptr;
  }

  // A list of all the loops in the function.
  LoopContainerType loops_;
  // Dummy root: this "loop" is only there to help iterators creation.
  Loop dummy_top_loop_;
  std::unordered_map<uint32_t, Loop*> basic_block_to_loop_;
};

}  // namespace ir
}  // namespace spvtools

#endif  // LIBSPIRV_OPT_LOOP_DESCRIPTORS_H_
