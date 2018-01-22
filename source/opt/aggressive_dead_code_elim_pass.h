// Copyright (c) 2017 The Khronos Group Inc.
// Copyright (c) 2017 Valve Corporation
// Copyright (c) 2017 LunarG Inc.
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

#ifndef LIBSPIRV_OPT_AGGRESSIVE_DCE_PASS_H_
#define LIBSPIRV_OPT_AGGRESSIVE_DCE_PASS_H_

#include <algorithm>
#include <map>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "basic_block.h"
#include "def_use_manager.h"
#include "mem_pass.h"
#include "module.h"

namespace spvtools {
namespace opt {

// See optimizer.hpp for documentation.
class AggressiveDCEPass : public MemPass {
  using cbb_ptr = const ir::BasicBlock*;

 public:
  using GetBlocksFunction =
      std::function<std::vector<ir::BasicBlock*>*(const ir::BasicBlock*)>;

  AggressiveDCEPass();
  const char* name() const override { return "eliminate-dead-code-aggressive"; }
  Status Process(ir::IRContext* c) override;

  ir::IRContext::Analysis GetPreservedAnalyses() override {
    return ir::IRContext::kAnalysisDefUse;
  }

 private:
  // Return true if |varId| is a variable of |storageClass|. |varId| must either
  // be 0 or the result of an instruction.
  bool IsVarOfStorage(uint32_t varId, uint32_t storageClass);

  // Return true if |varId| is variable of function storage class or is
  // private variable and privates can be optimized like locals (see
  // privates_like_local_).
  bool IsLocalVar(uint32_t varId);

  // Return true if |inst| is marked live.
  bool IsLive(const ir::Instruction* inst) const {
    return live_insts_.find(inst) != live_insts_.end();
  }

  // Returns true if |inst| is dead.
  bool IsDead(ir::Instruction* inst);

  // Adds entry points and execution modes to the worklist for processing with
  // the first function.
  void InitializeModuleScopeLiveInstructions();

  // Add |inst| to worklist_ and live_insts_.
  void AddToWorklist(ir::Instruction* inst) {
    if (live_insts_.insert(inst).second) worklist_.push(inst);
  }

  // Add all store instruction which use |ptrId|, directly or indirectly,
  // to the live instruction worklist.
  void AddStores(uint32_t ptrId);

  // Initialize extensions whitelist
  void InitExtensions();

  // Return true if all extensions in this module are supported by this pass.
  bool AllExtensionsSupported() const;

  // Returns true if the target of |inst| is dead.  An instruction is dead if
  // its result id is used in decoration or debug instructions only. |inst| is
  // assumed to be OpName, OpMemberName or an annotation instruction.
  bool IsTargetDead(ir::Instruction* inst);

  // If |varId| is local, mark all stores of varId as live.
  void ProcessLoad(uint32_t varId);

  // If |bp| is structured header block, returns true and sets |mergeInst| to
  // the merge instruction, |branchInst| to the branch and |mergeBlockId| to the
  // merge block if they are not nullptr.  Any of |mergeInst|, |branchInst| or
  // |mergeBlockId| may be a null pointer.  Returns false if |bp| is a null
  // pointer.
  bool IsStructuredHeader(ir::BasicBlock* bp, ir::Instruction** mergeInst,
                          ir::Instruction** branchInst, uint32_t* mergeBlockId);

  // Initialize block2headerBranch_ and branch2merge_ using |structuredOrder|
  // to order blocks.
  void ComputeBlock2HeaderMaps(std::list<ir::BasicBlock*>& structuredOrder);

  // Add branch to |labelId| to end of block |bp|.
  void AddBranch(uint32_t labelId, ir::BasicBlock* bp);

  // Add all break and continue branches in the loop associated with
  // |mergeInst| to worklist if not already live
  void AddBreaksAndContinuesToWorklist(ir::Instruction* mergeInst);

  // Eliminates dead debug2 and annotation instructions. Marks dead globals for
  // removal (e.g. types, constants and variables).
  bool ProcessGlobalValues();

  // Erases functions that are unreachable from the entry points of the module.
  bool EliminateDeadFunctions();

  // Removes |func| from the module and deletes all its instructions.
  void EliminateFunction(ir::Function* func);

  // For function |func|, mark all Stores to non-function-scope variables
  // and block terminating instructions as live. Recursively mark the values
  // they use. When complete, mark any non-live instructions to be deleted.
  // Returns true if the function has been modified.
  //
  // Note: This function does not delete useless control structures. All
  // existing control structures will remain. This can leave not-insignificant
  // sequences of ultimately useless code.
  // TODO(): Remove useless control constructs.
  bool AggressiveDCE(ir::Function* func);

  void Initialize(ir::IRContext* c);
  Pass::Status ProcessImpl();

  // True if current function has a call instruction contained in it
  bool call_in_func_;

  // True if current function is an entry point
  bool func_is_entry_point_;

  // True if current function is entry point and has no function calls.
  bool private_like_local_;

  // Live Instruction Worklist.  An instruction is added to this list
  // if it might have a side effect, either directly or indirectly.
  // If we don't know, then add it to this list.  Instructions are
  // removed from this list as the algorithm traces side effects,
  // building up the live instructions set |live_insts_|.
  std::queue<ir::Instruction*> worklist_;

  // Map from block to the branch instruction in the header of the most
  // immediate controlling structured if or loop.  A loop header block points
  // to its own branch instruction.  An if-selection block points to the branch
  // of an enclosing construct's header, if one exists.
  std::unordered_map<ir::BasicBlock*, ir::Instruction*> block2headerBranch_;

  // Maps basic block to their index in the structured order traversal.
  std::unordered_map<ir::BasicBlock*, uint32_t> structured_order_index_;

  // Map from branch to its associated merge instruction, if any
  std::unordered_map<ir::Instruction*, ir::Instruction*> branch2merge_;

  // Store instructions to variables of private storage
  std::vector<ir::Instruction*> private_stores_;

  // Live Instructions
  std::unordered_set<const ir::Instruction*> live_insts_;

  // Live Local Variables
  std::unordered_set<uint32_t> live_local_vars_;

  // List of instructions to delete. Deletion is delayed until debug and
  // annotation instructions are processed.
  std::vector<ir::Instruction*> to_kill_;

  // Extensions supported by this pass.
  std::unordered_set<std::string> extensions_whitelist_;
};

}  // namespace opt
}  // namespace spvtools

#endif  // LIBSPIRV_OPT_AGGRESSIVE_DCE_PASS_H_
