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

#include <functional>
#include <list>
#include <memory>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cfa.h"
#include "opt/basic_block.h"
#include "opt/dominator_tree.h"
#include "opt/fold.h"
#include "opt/function.h"
#include "opt/instruction.h"
#include "opt/ir_builder.h"
#include "opt/ir_context.h"
#include "opt/loop_descriptor.h"
#include "opt/loop_unswitch_pass.h"

#include "opt/loop_utils.h"

namespace spvtools {
namespace opt {
namespace {

constexpr uint32_t kBranchCondTrueLabIdInIdx = 1;
constexpr uint32_t kBranchCondFalseLabIdInIdx = 2;

}  // anonymous namespace

namespace {

// This class handle the unswitch procedure for a given loop.
// The unswitch will not happen if:
//  - The loop has any instruction that will prevent it;
//  - The loop invariant condition is not uniform.
class LoopUnswitch {
 public:
  LoopUnswitch(ir::IRContext* context, ir::Function* function, ir::Loop* loop,
               ir::LoopDescriptor* loop_desc)
      : function_(function),
        loop_(loop),
        loop_desc_(*loop_desc),
        context_(context),
        switch_block_(nullptr) {}

  // Returns true if the loop can be unswitched.
  // Can be unswitch if:
  //  - The loop has no instructions that prevents it (such as barrier);
  //  - The loop has one conditional branch or switch that do not depends on the
  //  loop;
  //  - The loop invariant condition is uniform;
  bool CanUnswitchLoop() {
    if (switch_block_) return true;
    if (loop_->IsSafeToClone()) return false;

    ir::CFG& cfg = *context_->cfg();

    for (uint32_t bb_id : loop_->GetBlocks()) {
      ir::BasicBlock* bb = cfg.block(bb_id);
      if (bb->terminator()->IsBranch() &&
          bb->terminator()->opcode() != SpvOpBranch) {
        if (IsConditionLoopIV(bb->terminator())) {
          switch_block_ = bb;
          break;
        }
      }
    }

    return switch_block_;
  }

  // Return the iterator to the basic block |bb|.
  ir::Function::iterator FindBasicBlockPosition(ir::BasicBlock* bb_to_find) {
    ir::Function::iterator it = std::find_if(
        function_->begin(), function_->end(),
        [bb_to_find](const ir::BasicBlock& bb) { return bb_to_find == &bb; });
    assert(it != function_->end() && "Basic Block not found");
    return it;
  }

  // Creates a new basic block and insert it into the function |fn| at the
  // position |ip|. This function preserves the def/use and instr to block
  // managers.
  ir::BasicBlock* CreateBasicBlock(ir::Function::iterator ip) {
    analysis::DefUseManager* def_use_mgr = context_->get_def_use_mgr();

    // Create the dedicated exit basic block.
    ir::BasicBlock* bb = &*ip.InsertBefore(std::unique_ptr<ir::BasicBlock>(
        new ir::BasicBlock(std::unique_ptr<ir::Instruction>(new ir::Instruction(
            context_, SpvOpLabel, 0, context_->TakeNextId(), {})))));
    bb->SetParent(function_);
    def_use_mgr->AnalyzeInstDef(bb->GetLabelInst());
    context_->set_instr_block(bb->GetLabelInst(), bb);

    return bb;
  }

  // Unswitches |loop_|.
  void PerformUnswitch() {
    assert(CanUnswitchLoop() &&
           "Cannot unswitch if there is not constant condition");
    assert(loop_->GetPreHeaderBlock() && "This loop has no pre-header block");
    assert(loop_->IsLCSSA() && "This loop is not in LCSSA form");

    ir::CFG& cfg = *context_->cfg();
    DominatorTree* dom_tree =
        &context_->GetDominatorAnalysis(function_, *context_->cfg())
             ->GetDomTree();
    analysis::DefUseManager* def_use_mgr = context_->get_def_use_mgr();

    //////////////////////////////////////////////////////////////////////////////
    // Step 1: Create the if merge block for structured modules.
    //    To do so, the |loop_| merge block will become the if's one and we
    //    create a merge for the loop. This will limit the amount of duplicated
    //    code the structured control flow imposes.
    //    For non structured program, the new loop will be connected to
    //    the old loop's exit blocks.
    //////////////////////////////////////////////////////////////////////////////

    // Get the merge block if it exists.
    ir::BasicBlock* if_merge_block = loop_->GetMergeBlock();
    // The merge block is only created if the loop has a unique exit block. We
    // have this guarantee for structured loops, for compute loop it will
    // trivially help maintain both a structured-like form and LCSAA.
    ir::BasicBlock* loop_merge_block =
        if_merge_block
            ? CreateBasicBlock(FindBasicBlockPosition(if_merge_block))
            : nullptr;
    if (loop_merge_block) {
      // Add the instruction and update managers.
      opt::InstructionBuilder builder(
          context_, loop_merge_block,
          ir::IRContext::kAnalysisDefUse |
              ir::IRContext::kAnalysisInstrToBlockMapping);
      builder.AddBranch(if_merge_block->id());
      builder.SetInsertPoint(&*loop_merge_block->begin());
      cfg.RegisterBlock(loop_merge_block);
      def_use_mgr->AnalyzeInstDef(loop_merge_block->GetLabelInst());
      // Update CFG.
      if_merge_block->ForEachPhiInst(
          [loop_merge_block, &builder, this](ir::Instruction* phi) {
            ir::Instruction* cloned = phi->Clone(context_);
            builder.AddInstruction(std::unique_ptr<ir::Instruction>(cloned));
            phi->SetInOperand(0, {cloned->result_id()});
            phi->SetInOperand(1, {loop_merge_block->id()});
            for (uint32_t j = phi->NumInOperands() - 1; j > 1; j--)
              phi->RemoveInOperand(j);
          });
      // Copy the predecessor list (will get invalidated otherwise).
      std::vector<uint32_t> preds = cfg.preds(if_merge_block->id());
      for (uint32_t pid : preds) {
        if (pid == loop_merge_block->id()) continue;
        ir::BasicBlock* p_bb = cfg.block(pid);
        p_bb->ForEachSuccessorLabel(
            [if_merge_block, loop_merge_block](uint32_t* id) {
              if (*id == if_merge_block->id()) *id = loop_merge_block->id();
            });
        cfg.AddEdge(pid, loop_merge_block->id());
      }
      cfg.RemoveNonExistingEdges(if_merge_block->id());
      // Update loop descriptor.
      if (ir::Loop* ploop = loop_->GetParent()) {
        ploop->AddBasicBlock(loop_merge_block);
        loop_desc_.SetBasicBlockToLoop(loop_merge_block->id(), ploop);
      }

      // Update the dominator tree.
      DominatorTreeNode* loop_merge_dtn =
          dom_tree->GetOrInsertNode(loop_merge_block);
      DominatorTreeNode* if_merge_block_dtn =
          dom_tree->GetOrInsertNode(if_merge_block);
      loop_merge_dtn->parent_ = if_merge_block_dtn->parent_;
      loop_merge_dtn->children_.push_back(if_merge_block_dtn);
      loop_merge_dtn->parent_->children_.push_back(loop_merge_dtn);
      if_merge_block_dtn->parent_->children_.erase(std::find(
          if_merge_block_dtn->parent_->children_.begin(),
          if_merge_block_dtn->parent_->children_.end(), if_merge_block_dtn));

      loop_->SetMergeBlock(loop_merge_block);
    }

    ////////////////////////////////////////////////////////////////////////////
    // Step 2: Build a new preheader for |loop_|, use the old one
    //         for the constant branch.
    ////////////////////////////////////////////////////////////////////////////

    ir::BasicBlock* if_block = loop_->GetPreHeaderBlock();
    // If this preheader is the parent loop header,
    // we need to create a dedicated block for the if.
    ir::BasicBlock* loop_pre_header =
        CreateBasicBlock(++FindBasicBlockPosition(if_block));
    opt::InstructionBuilder(context_, loop_pre_header,
                            ir::IRContext::kAnalysisDefUse |
                                ir::IRContext::kAnalysisInstrToBlockMapping)
        .AddBranch(loop_->GetHeaderBlock()->id());

    if_block->tail()->SetInOperand(0, {loop_pre_header->id()});

    // Update loop descriptor.
    if (ir::Loop* ploop = loop_desc_[if_block]) {
      ploop->AddBasicBlock(loop_pre_header);
      loop_desc_.SetBasicBlockToLoop(loop_pre_header->id(), ploop);
    }

    // Update the CFG.
    cfg.RegisterBlock(loop_pre_header);
    def_use_mgr->AnalyzeInstDef(loop_pre_header->GetLabelInst());
    cfg.AddEdge(if_block->id(), loop_pre_header->id());
    cfg.RemoveNonExistingEdges(loop_->GetHeaderBlock()->id());

    loop_->GetHeaderBlock()->ForEachPhiInst(
        [loop_pre_header, if_block](ir::Instruction* phi) {
          phi->ForEachInId([loop_pre_header, if_block](uint32_t* id) {
            if (*id == if_block->id()) {
              *id = loop_pre_header->id();
            }
          });
        });
    loop_->SetPreHeaderBlock(loop_pre_header);

    // Update the dominator tree.
    DominatorTreeNode* loop_pre_header_dtn =
        dom_tree->GetOrInsertNode(loop_pre_header);
    DominatorTreeNode* if_block_dtn = dom_tree->GetTreeNode(if_block);
    loop_pre_header_dtn->parent_ = if_block_dtn;
    assert(
        if_block_dtn->children_.size() == 1 &&
        "A loop preheader should only have the header block as a child in the "
        "dominator tree");
    loop_pre_header_dtn->children_.push_back(if_block_dtn->children_[0]);
    if_block_dtn->children_.clear();
    if_block_dtn->children_.push_back(loop_pre_header_dtn);

    // Make domination queries valid.
    dom_tree->ResetDFNumbering();

    // Compute an ordered list of basic to clone.
    ComputeLoopStructuredOrder();

    /////////////////////////////
    // Do the actual unswitch: //
    //   - Clone the loop      //
    //   - Connect exits       //
    //   - Specialize the loop //
    /////////////////////////////

    ir::Instruction* iv_condition = &*switch_block_->tail();
    SpvOp iv_opcode = iv_condition->opcode();
    ir::Instruction* condition =
        def_use_mgr->GetDef(iv_condition->GetOperand(0).words[0]);

    analysis::ConstantManager* cst_mgr = context_->get_constant_mgr();
    const analysis::Type* cond_type =
        context_->get_type_mgr()->GetType(condition->type_id());

    // Build the list of value for which we need to clone and specialize the
    // loop.
    std::vector<std::pair<ir::Instruction*, ir::BasicBlock*>> constant_branch;
    // Special case for the original loop
    ir::Instruction* original_loop_constant_value;
    ir::BasicBlock* original_loop_target;
    if (iv_opcode == SpvOpBranchConditional) {
      constant_branch.emplace_back(
          cst_mgr->GetDefiningInstruction(cst_mgr->GetConstant(cond_type, {0})),
          nullptr);
      original_loop_constant_value =
          cst_mgr->GetDefiningInstruction(cst_mgr->GetConstant(cond_type, {1}));
    } else {
      // We are looking to take the default branch, so we can't provide a
      // specific value.
      original_loop_constant_value = nullptr;
      for (uint32_t i = 2; i < iv_condition->NumInOperands(); i += 2) {
        constant_branch.emplace_back(
            cst_mgr->GetDefiningInstruction(cst_mgr->GetConstant(
                cond_type, iv_condition->GetInOperand(i).words)),
            nullptr);
      }
    }

    // Get the loop landing pads.
    std::unordered_set<uint32_t> if_merging_blocks;
    std::function<bool(uint32_t)> is_from_original_loop;
    if (loop_->GetHeaderBlock()->GetLoopMergeInst()) {
      if_merging_blocks.insert(if_merge_block->id());
      is_from_original_loop = [this](uint32_t id) {
        return loop_->GetMergeBlock()->id() == id;
      };
    } else {
      loop_->GetExitBlocks(&if_merging_blocks);
      is_from_original_loop = [this](uint32_t id) {
        return loop_->IsInsideLoop(id);
      };
    }

    for (auto& specialisation_pair : constant_branch) {
      ClearMappingState();
      ir::Instruction* specialisation_value = specialisation_pair.first;
      //////////////////////////////////////////////////////////
      // Step 3: Suplicate |loop_|.
      //////////////////////////////////////////////////////////

      std::list<std::unique_ptr<ir::BasicBlock>> ordered_loop_bb;
      std::unique_ptr<ir::Loop> cloned_loop(CloneLoop(&ordered_loop_bb));

      ////////////////////////////////////
      // Step 4: Specialize the loop.   //
      ////////////////////////////////////

      {
        std::unordered_set<uint32_t> dead_blocks;
        std::unordered_set<uint32_t> unreachable_merges;
        SimplifyLoop(cloned_loop.get(), condition, specialisation_value,
                     [this](uint32_t id) {
                       BlockMapTy::iterator it = new_to_old_bb_.find(id);
                       return it != new_to_old_bb_.end() ? it->second : nullptr;
                     },
                     &dead_blocks);

        // We tagged dead blocks, create the loop before we invalidate any basic
        // block.
        PopulateLoopNest(dead_blocks, &unreachable_merges);
        CleanUpCFG(&ordered_loop_bb, dead_blocks, unreachable_merges);
        specialisation_pair.second = cloned_loop->GetPreHeaderBlock();

        ///////////////////////////////////////////////////////////
        // Step 5: Connect convergent edges to the landing pads. //
        ///////////////////////////////////////////////////////////

        for (uint32_t merge_bb_id : if_merging_blocks) {
          ir::BasicBlock* merge = context_->cfg()->block(merge_bb_id);
          // We are in LCSSA so we only care about phi instructions.
          merge->ForEachPhiInst([is_from_original_loop, &dead_blocks,
                                 this](ir::Instruction* phi) {
            uint32_t num_in_operands = phi->NumInOperands();
            for (uint32_t i = 0; i < num_in_operands; i += 2) {
              uint32_t pred = phi->GetSingleWordInOperand(i + 1);
              if (is_from_original_loop(pred)) {
                pred = value_map_.at(pred);
                if (!dead_blocks.count(pred)) {
                  uint32_t incoming_value_id = phi->GetSingleWordInOperand(i);
                  // Not all the incoming value are coming from the loop.
                  ValueMapTy::iterator new_value =
                      value_map_.find(incoming_value_id);
                  if (new_value != value_map_.end()) {
                    incoming_value_id = new_value->second;
                  }
                  phi->AddOperand({SPV_OPERAND_TYPE_ID, {incoming_value_id}});
                  phi->AddOperand({SPV_OPERAND_TYPE_ID, {pred}});
                }
              }
            }
          });
        }
      }
      function_->AddBasicBlocks(ordered_loop_bb.begin(), ordered_loop_bb.end(),
                                ++FindBasicBlockPosition(if_block));
    }

    // Same as above but specialize the existing loop
    {
      std::unordered_set<uint32_t> dead_blocks;
      std::unordered_set<uint32_t> unreachable_merges;
      SimplifyLoop(loop_, condition, original_loop_constant_value,
                   [&cfg](uint32_t id) { return cfg.block(id); }, &dead_blocks);

      for (uint32_t merge_bb_id : if_merging_blocks) {
        ir::BasicBlock* merge = context_->cfg()->block(merge_bb_id);
        // LCSSA, so we only care about phi instructions.
        merge->ForEachPhiInst(
            [is_from_original_loop, &dead_blocks](ir::Instruction* phi) {
              uint32_t num_in_operands = phi->NumInOperands();
              uint32_t i = 0;
              while (i < num_in_operands) {
                uint32_t pred = phi->GetSingleWordInOperand(i + 1);
                if (is_from_original_loop(pred)) {
                  if (dead_blocks.count(pred)) {
                    phi->RemoveInOperand(i);
                    phi->RemoveInOperand(i);
                    continue;
                  }
                }
                i += 2;
              }
            });
      }
      if (if_merge_block) {
        bool has_live_pred = false;
        for (uint32_t pid : cfg.preds(if_merge_block->id())) {
          if (!dead_blocks.count(pid)) {
            has_live_pred = true;
            break;
          }
        }
        if (!has_live_pred) unreachable_merges.insert(if_merge_block->id());
      }
      original_loop_target = loop_->GetPreHeaderBlock();
      // We tagged dead blocks, prune the loop descriptor from any dead loops.
      // After this call, |loop_| can be nullptr (i.e. the unswitch killed this
      // loop).
      CleanLoopNest(dead_blocks, &unreachable_merges);

      CleanUpCFG(function_->GetBlocks(), dead_blocks, unreachable_merges);
    }

    /////////////////////////////////////
    // Finally: connect the new loops. //
    /////////////////////////////////////

    // Delete the old jump
    context_->KillInst(&*if_block->tail());
    opt::InstructionBuilder builder(context_, if_block);
    if (iv_opcode == SpvOpBranchConditional) {
      assert(constant_branch.size() == 1);
      builder.AddConditionalBranch(
          condition->result_id(), original_loop_target->id(),
          constant_branch[0].second->id(),
          if_merge_block ? if_merge_block->id() : kInvalidId);
    } else {
      std::vector<std::pair<std::vector<uint32_t>, uint32_t>> targets;
      for (auto& t : constant_branch) {
        targets.emplace_back(t.first->GetInOperand(0).words, t.second->id());
      }

      builder.AddSwitch(condition->result_id(), original_loop_target->id(),
                        targets,
                        if_merge_block ? if_merge_block->id() : kInvalidId);
    }

    switch_block_ = nullptr;

    context_->InvalidateAnalysesExceptFor(
        ir::IRContext::Analysis::kAnalysisLoopAnalysis);
  }

  // Returns true if the unswitch killed the original |loop_|.
  bool WasLoopKilled() const { return loop_ == nullptr; }

 private:
  using ValueMapTy = std::unordered_map<uint32_t, uint32_t>;
  using BlockMapTy = std::unordered_map<uint32_t, ir::BasicBlock*>;

  ir::Function* function_;
  ir::Loop* loop_;
  ir::LoopDescriptor& loop_desc_;
  ir::IRContext* context_;

  ir::BasicBlock* switch_block_;
  // The loop basic blocks in structured order
  std::list<ir::BasicBlock*> order_loop_blocks_;

  ValueMapTy value_map_;
  // Mapping between original loop blocks to the cloned one and vice versa.
  BlockMapTy old_to_new_bb_;
  BlockMapTy new_to_old_bb_;

  // Cleans up mapping stats between |loop_| and a cloned loop.
  void ClearMappingState() {
    value_map_.clear();
    old_to_new_bb_.clear();
    new_to_old_bb_.clear();
  }

  // Returns the next usable id for the context.
  uint32_t TakeNextId() { return context_->TakeNextId(); }

  // Removes any block that is tagged as dead, if the block is in
  // |unreachable_merges| then all block's instructions are replaced by a
  // OpUnreachable.
  template <typename Container>
  void CleanUpCFG(Container* container,
                  const std::unordered_set<uint32_t>& dead_blocks,
                  const std::unordered_set<uint32_t>& unreachable_merges) {
    ir::CFG& cfg = *context_->cfg();

    typename Container::iterator bb_it = container->begin();
    while (bb_it != container->end()) {
      ir::BasicBlock* bb = bb_it->get();

      if (unreachable_merges.count(bb->id())) {
        if (bb->begin() != bb->tail() ||
            bb->terminator()->opcode() != SpvOpUnreachable) {
          // Make unreachable, but leave the label.
          bb->KillAllInsts(false);
          opt::InstructionBuilder(context_, bb).AddUnreachable();
          cfg.RemoveNonExistingEdges(bb->id());
        }
        ++bb_it;
      } else if (dead_blocks.count(bb->id())) {
        cfg.ForgetBlock(bb);
        // Kill this block.
        bb->KillAllInsts(true);
        bb_it = container->erase(bb_it);
      } else {
        cfg.RemoveNonExistingEdges(bb->id());
        ++bb_it;
      }
    }
  }

  // Return true if |c_inst| is a Boolean constant and set |cond_val| with the
  // value that |c_inst|
  bool GetConstCondition(const ir::Instruction* c_inst, bool* cond_val) {
    bool cond_is_const;
    switch (c_inst->opcode()) {
      case SpvOpConstantFalse: {
        *cond_val = false;
        cond_is_const = true;
      } break;
      case SpvOpConstantTrue: {
        *cond_val = true;
        cond_is_const = true;
      } break;
      default: { cond_is_const = false; } break;
    }
    return cond_is_const;
  }

  // Simplifies |loop| assuming the instruction |to_version_insn| takes the
  // value |cst_value|.
  // Requirements:
  //   - |loop| must be in the LCSSA form;
  //   - |cst_value| must be constant.
  // The set |dead_blocks| will contain all the dead basic blocks, and the list
  // |ordered_loop_bb| will contain the live basic blocks in reverse post-order.
  void SimplifyLoop(
      ir::Loop* loop, ir::Instruction* to_version_insn,
      ir::Instruction* cst_value,
      const std::function<ir::BasicBlock*(uint32_t)>& new_to_old_block_mapping,
      std::unordered_set<uint32_t>* dead_blocks) {
    // Version the |loop| body.
    // Do a DFS that takes into account the specialized value of
    // to_version_insn. As we go we:
    //  - Fold instructions;
    //  - Hoist any loop invariant;
    //  - Only keep reachable basic block.
    ir::CFG& cfg = *context_->cfg();
    DominatorTree* dom_tree =
        &context_->GetDominatorAnalysis(function_, *context_->cfg())
             ->GetDomTree();
    analysis::DefUseManager* def_use_mgr = context_->get_def_use_mgr();

    opt::DominatorTreeNode* merge_dtn = dom_tree->GetTreeNode(
        new_to_old_block_mapping(loop->GetMergeBlock()->id()));
    std::function<bool(uint32_t)> ignore_node_and_children;
    if (loop_->GetHeaderBlock()->GetLoopMergeInst()) {
      ignore_node_and_children = [merge_dtn, new_to_old_block_mapping,
                                  dom_tree](uint32_t bb_id) {
        ir::BasicBlock* bb = new_to_old_block_mapping(bb_id);
        if (!bb) return true;
        opt::DominatorTreeNode* dtn = dom_tree->GetTreeNode(bb);
        return dom_tree->Dominates(merge_dtn, dtn);
      };
    } else {
      ignore_node_and_children = [loop](uint32_t bb_id) {
        return !loop->IsInsideLoop(bb_id);
      };
    }

    std::unordered_set<ir::Instruction*> work_list;
    std::unordered_set<uint32_t> visited;

    // To also handle switch, cst_value can be nullptr: this case means that
    // we are looking to branch to the target of the constant switch.
    if (cst_value) {
      uint32_t cst_value_id = cst_value->result_id();

      def_use_mgr->ForEachUse(
          to_version_insn, [cst_value_id, &work_list, &ignore_node_and_children,
                            this](ir::Instruction* i, uint32_t operand_index) {
            ir::BasicBlock* bb = context_->get_instr_block(i);
            if (!ignore_node_and_children(bb->id())) {
              i->SetOperand(operand_index, {cst_value_id});
              work_list.insert(i);
            }
          });
    } else {
      def_use_mgr->ForEachUse(
          to_version_insn, [&work_list, &ignore_node_and_children, this](
                               ir::Instruction* i, uint32_t operand_index) {
            ir::BasicBlock* bb = context_->get_instr_block(i);
            if (!ignore_node_and_children(bb->id()) &&
                i->opcode() == SpvOpSwitch) {
              i->SetOperand(operand_index, {0});
              work_list.insert(i);
            }
          });
    }

    while (!work_list.empty()) {
      ir::Instruction* inst = *work_list.begin();
      ir::BasicBlock* bb = context_->get_instr_block(inst);
      work_list.erase(work_list.begin());

      // If the basic block is known to be dead, ignore the instruction.
      if (dead_blocks->count(bb->id())) continue;

      if (inst->opcode() == SpvOpLabel) {
        bool has_live_pred = false;
        for (uint32_t pid : cfg.preds(inst->result_id())) {
          if (!dead_blocks->count(pid)) {
            has_live_pred = true;
            break;
          }
        }
        if (!has_live_pred) {
          dead_blocks->insert(bb->id());
          def_use_mgr->ForEachUser(bb->GetLabelInst(),
                                   [&work_list](ir::Instruction* i) {
                                     // Capture merge and phi instructions only.
                                     if (!i->IsBranch()) {
                                       work_list.insert(i);
                                     }
                                   });
          bb->ForEachSuccessorLabel([&work_list, def_use_mgr](uint32_t sid) {
            work_list.insert(def_use_mgr->GetDef(sid));
          });
        }
        continue;
      }

      if (inst->opcode() == SpvOpLoopMerge) {
        if (dead_blocks->count(inst->GetSingleWordInOperand(1))) {
          def_use_mgr->ClearInst(inst);
          context_->KillInst(inst);
        }
        continue;
      }

      if (inst->IsBranch()) {
        uint32_t live_target = 0;
        switch (inst->opcode()) {
          case SpvOpBranchConditional: {
            const ir::Instruction* cond =
                def_use_mgr->GetDef(inst->GetOperand(0).words[0]);
            if (!cond) break;
            bool branch_cond = false;
            if (GetConstCondition(cond, &branch_cond)) {
              uint32_t true_label =
                  inst->GetSingleWordInOperand(kBranchCondTrueLabIdInIdx);
              uint32_t false_label =
                  inst->GetSingleWordInOperand(kBranchCondFalseLabIdInIdx);
              live_target = branch_cond ? true_label : false_label;
              uint32_t dead_target = !branch_cond ? true_label : false_label;
              cfg.RemoveEdge(bb->id(), dead_target);
              work_list.insert(def_use_mgr->GetDef(dead_target));
            }
            break;
          }
          case SpvOpSwitch: {
            const ir::Instruction* condition =
                inst->GetSingleWordInOperand(0)
                    ? def_use_mgr->GetDef(inst->GetSingleWordInOperand(0))
                    : nullptr;
            uint32_t default_target = inst->GetSingleWordInOperand(1);
            if (condition) {
              if (!condition->IsConstant()) break;
              const ir::Operand& cst = condition->GetInOperand(0);
              for (uint32_t i = 2; i < inst->NumInOperands(); i += 2) {
                const ir::Operand& literal = inst->GetInOperand(i);
                if (literal == cst) {
                  live_target = inst->GetSingleWordInOperand(i + 1);
                  break;
                }
              }
            }
            if (!live_target) {
              live_target = default_target;
            }
            for (uint32_t i = 1; i < inst->NumInOperands(); i += 2) {
              uint32_t id = inst->GetSingleWordInOperand(i);
              if (id != live_target) {
                cfg.RemoveEdge(bb->id(), id);
                work_list.insert(def_use_mgr->GetDef(id));
              }
            }
          }
          default:
            break;
        }
        if (live_target != 0) {
          context_->KillInst(&*bb->tail());
          // Check for the presence of the merge block.
          if (bb->begin() != bb->end() &&
              bb->tail()->opcode() == SpvOpSelectionMerge)
            context_->KillInst(&*bb->tail());
          opt::InstructionBuilder builder(
              context_, bb,
              ir::IRContext::kAnalysisDefUse |
                  ir::IRContext::kAnalysisInstrToBlockMapping);
          builder.AddBranch(live_target);
        }
        continue;
      }

      if (inst->opcode() == SpvOpPhi) {
        // Patch phi instructions if needed, predecessors might have been
        // removed. New phi operands for this instruction.
        std::vector<uint32_t> phi_op;
        for (uint32_t i = 0; i < inst->NumInOperands(); i += 2) {
          uint32_t def_id = inst->GetSingleWordInOperand(i);
          uint32_t incoming_id = inst->GetSingleWordInOperand(i + 1);
          if (!dead_blocks->count(incoming_id)) {
            phi_op.push_back(def_id);
            phi_op.push_back(incoming_id);
          }
        }
        if (inst->NumInOperands() != phi_op.size()) {
          // Rewrite operands.
          uint32_t idx = 0;
          for (; idx < phi_op.size(); idx++)
            inst->SetInOperand(idx, {phi_op[idx]});
          // Remove extra operands, from last to first (more efficient).
          for (uint32_t j = inst->NumInOperands() - 1; j >= idx; j--)
            inst->RemoveInOperand(j);
          // Update the def/use manager for this |inst|.
          def_use_mgr->AnalyzeInstUse(inst);
        }

        if (inst->NumInOperands() == 2) {
          std::unordered_set<ir::Instruction*> to_update;
          def_use_mgr->ForEachUse(
              inst, [&work_list, ignore_node_and_children, inst, &to_update,
                     this](ir::Instruction* use, uint32_t operand) {
                use->SetOperand(operand, {inst->GetSingleWordInOperand(0)});
                to_update.insert(use);
                // Don't step out of the ROI.
                if (!ignore_node_and_children(
                        context_->get_instr_block(use)->id())) {
                  work_list.insert(use);
                }
              });
          context_->KillInst(inst);
          for (ir::Instruction* use : to_update)
            def_use_mgr->AnalyzeInstUse(use);
        }
        continue;
      }

      // General case, try to fold or forget about this use.
      if (FoldInstruction(inst)) {
        context_->AnalyzeUses(inst);
        def_use_mgr->ForEachUser(inst, [&work_list, ignore_node_and_children,
                                        this](ir::Instruction* use) {
          if (!ignore_node_and_children(context_->get_instr_block(use)->id()))
            work_list.insert(use);
        });
        if (inst->opcode() == SpvOpCopyObject) {
          std::unordered_set<ir::Instruction*> to_update;
          def_use_mgr->ForEachUse(
              inst, [&work_list, ignore_node_and_children, inst, &to_update,
                     this](ir::Instruction* use, uint32_t operand) {
                use->SetOperand(operand, {inst->GetSingleWordInOperand(0)});
                to_update.insert(use);
                // Don't step out of the ROI.
                if (!ignore_node_and_children(
                        context_->get_instr_block(use)->id())) {
                  work_list.insert(use);
                }
              });
          context_->KillInst(inst);
          for (ir::Instruction* use : to_update)
            def_use_mgr->AnalyzeInstUse(use);
        }
      }
    }
  }

  // Create the list of the loop's basic block in structured order.
  // The generated list is used by CloneLoop to clone the loop's basic block in
  // the appropriate order.
  void ComputeLoopStructuredOrder() {
    ir::CFG& cfg = *context_->cfg();
    DominatorTree* dom_tree =
        &context_->GetDominatorAnalysis(function_, *context_->cfg())
             ->GetDomTree();

    std::unordered_map<const ir::BasicBlock*, std::vector<ir::BasicBlock*>>
        block2structured_succs;

    std::function<bool(uint32_t)> ignore_node_and_children;
    if (loop_->GetHeaderBlock()->GetLoopMergeInst()) {
      ignore_node_and_children = [dom_tree, this](uint32_t bb_id) {
        return dom_tree->StrictlyDominates(loop_->GetMergeBlock()->id(), bb_id);
      };
    } else {
      ignore_node_and_children = [this](uint32_t bb_id) {
        return !loop_->IsInsideLoop(bb_id);
      };
    }

    loop_->GetPreHeaderBlock()->ForEachSuccessorLabel(
        [&cfg, &block2structured_succs, ignore_node_and_children,
         this](const uint32_t sbid) {
          if (!ignore_node_and_children(sbid))
            block2structured_succs[loop_->GetPreHeaderBlock()].push_back(
                cfg.block(sbid));
        });
    for (uint32_t blk_id : loop_->GetBlocks()) {
      const ir::BasicBlock* blk = cfg.block(blk_id);
      // If header, make merge block first successor.
      uint32_t mbid = blk->MergeBlockIdIfAny();
      if (mbid != 0) {
        block2structured_succs[blk].push_back(cfg.block(mbid));
        uint32_t cbid = blk->ContinueBlockIdIfAny();
        if (cbid != 0) {
          block2structured_succs[blk].push_back(cfg.block(cbid));
        }
      }

      blk->ForEachSuccessorLabel(
          [blk, &cfg, &block2structured_succs,
           ignore_node_and_children](const uint32_t sbid) {
            if (!ignore_node_and_children(sbid))
              block2structured_succs[blk].push_back(cfg.block(sbid));
          });
    }

    auto ignore_block = [](const ir::BasicBlock*) {};
    auto ignore_edge = [](const ir::BasicBlock*, const ir::BasicBlock*) {};
    auto get_structured_successors =
        [&block2structured_succs](const ir::BasicBlock* block) {
          return &(block2structured_succs[block]);
        };
    auto post_order = [this](const ir::BasicBlock* b) {
      order_loop_blocks_.push_front(const_cast<ir::BasicBlock*>(b));
    };

    spvtools::CFA<ir::BasicBlock>::DepthFirstTraversal(
        loop_->GetPreHeaderBlock(), get_structured_successors, ignore_block,
        post_order, ignore_edge);
  }

  // Clone the current loop and remap its instructions. Newly created blocks
  // will be added to the |ordered_loop_bb| list, correctly ordered to be
  // inserted into a function. If the loop is structured, the merge construct
  // will also be cloned. The function preserves the def/use, cfg and instr to
  // block analyses.
  std::unique_ptr<ir::Loop> CloneLoop(
      std::list<std::unique_ptr<ir::BasicBlock>>* ordered_loop_bb) {
    analysis::DefUseManager* def_use_mgr = context_->get_def_use_mgr();

    std::unique_ptr<ir::Loop> new_loop = MakeUnique<ir::Loop>();
    if (loop_->HasParent()) new_loop->SetParent(loop_->GetParent());

    ir::CFG& cfg = *context_->cfg();

    // Clone and place blocks in a SPIR-V compliant order (dominators first).
    for (ir::BasicBlock* old_bb : order_loop_blocks_) {
      // For each basic block in the loop, we clone it and register the mapping
      // between old and new ids.
      ir::BasicBlock* new_bb = old_bb->Clone(context_);
      new_bb->SetParent(function_);
      new_bb->GetLabelInst()->SetResultId(TakeNextId());
      def_use_mgr->AnalyzeInstDef(new_bb->GetLabelInst());
      context_->set_instr_block(new_bb->GetLabelInst(), new_bb);
      ordered_loop_bb->emplace_back(new_bb);

      old_to_new_bb_[old_bb->id()] = new_bb;
      new_to_old_bb_[new_bb->id()] = old_bb;
      value_map_[old_bb->id()] = new_bb->id();

      if (loop_->IsInsideLoop(old_bb)) new_loop->AddBasicBlock(new_bb);

      for (auto& inst : *new_bb) {
        if (inst.HasResultId()) {
          uint32_t old_result_id = inst.result_id();
          inst.SetResultId(TakeNextId());
          value_map_[old_result_id] = inst.result_id();

          // Only look at the defs for now, uses are not updated yet.
          def_use_mgr->AnalyzeInstDef(&inst);
        }
      }
    }

    // All instructions (including all labels) have been cloned,
    // remap instruction operands id with the new ones.
    for (std::unique_ptr<ir::BasicBlock>& bb_ref : *ordered_loop_bb) {
      ir::BasicBlock* bb = bb_ref.get();

      for (ir::Instruction& insn : *bb) {
        insn.ForEachInId([this](uint32_t* old_id) {
          // If the operand is defined in the loop, remap the id.
          ValueMapTy::iterator id_it = value_map_.find(*old_id);
          if (id_it != value_map_.end()) {
            *old_id = id_it->second;
          }
        });
        // Only look at what the instruction uses. All defs are register, so all
        // should be fine now.
        def_use_mgr->AnalyzeInstUse(&insn);
        context_->set_instr_block(&insn, bb);
      }
      cfg.RegisterBlock(bb);
    }

    std::unordered_set<uint32_t> dead_blocks;
    std::unordered_set<uint32_t> unreachable_merges;
    PopulateLoopDesc(new_loop.get(), loop_, dead_blocks, &unreachable_merges);

    return new_loop;
  }

  // Returns true if the header is not reachable or tagged as dead or if we
  // never loop back.
  bool IsLoopDead(ir::BasicBlock* header, ir::BasicBlock* latch,
                  const std::unordered_set<uint32_t>& dead_blocks) {
    if (!header || dead_blocks.count(header->id())) return true;
    if (!latch || dead_blocks.count(latch->id())) return true;
    for (uint32_t pid : context_->cfg()->preds(header->id())) {
      if (!dead_blocks.count(pid)) {
        // Seems reachable.
        return false;
      }
    }
    return true;
  }

  // Cleans the loop nest under |loop_| and reflect changes to the loop
  // descriptor. This will kill all descriptors for that represent dead loops.
  // If |loop_| is killed, it will be set to nullptr.
  // Any merge blocks that become unreachable will be added to
  // |unreachable_merges|.
  void CleanLoopNest(const std::unordered_set<uint32_t>& dead_blocks,
                     std::unordered_set<uint32_t>* unreachable_merges) {
    // This represent the pair of dead loop and nearest alive parent (nullptr if
    // no parent).
    std::unordered_map<ir::Loop*, ir::Loop*> dead_loops;
    auto get_parent = [&dead_loops](ir::Loop* loop) -> ir::Loop* {
      std::unordered_map<ir::Loop*, ir::Loop*>::iterator it =
          dead_loops.find(loop);
      if (it != dead_loops.end()) return it->second;
      return nullptr;
    };

    bool is_main_loop_dead = IsLoopDead(loop_->GetHeaderBlock(),
                                        loop_->GetLatchBlock(), dead_blocks);
    if (is_main_loop_dead)
      dead_loops[loop_] = loop_->GetParent();
    else
      dead_loops[loop_] = loop_->GetParent();
    // For each loop, check if we killed it. If we did, find a suitable parent
    // for its children.
    for (ir::Loop& sub_loop :
         ir::make_range(++opt::TreeDFIterator<ir::Loop>(loop_),
                        opt::TreeDFIterator<ir::Loop>())) {
      if (IsLoopDead(sub_loop.GetHeaderBlock(), sub_loop.GetLatchBlock(),
                     dead_blocks)) {
        dead_loops[&sub_loop] = get_parent(&sub_loop);
        continue;
      }
    }
    if (!is_main_loop_dead) dead_loops.erase(loop_);

    // Reassign all live loops to their new parents.
    for (auto& pair : dead_loops) {
      ir::Loop* loop = pair.first;
      ir::Loop* alive_parent = pair.second;
      for (ir::Loop* sub_loop : *loop) {
        if (!dead_loops.count(sub_loop)) {
          if (alive_parent) {
            sub_loop->SetParent(nullptr);
            // Register the loop as a direct child of |alive_parent|.
            alive_parent->AddNestedLoop(sub_loop);
          } else {
            if (sub_loop->HasParent()) {
              sub_loop->SetParent(nullptr);
              loop_desc_.SetAsTopLoop(sub_loop);
            }
          }
        }
      }
    }

    // Recompute the basic block to loop mapping, check for any unreachable
    // merges in the process.
    for (uint32_t bb_id : loop_->GetBlocks()) {
      ir::Loop* l = loop_desc_[bb_id];
      std::unordered_map<ir::Loop*, ir::Loop*>::iterator dead_it =
          dead_loops.find(l);
      if (dead_it != dead_loops.end()) {
        if (dead_it->second) {
          loop_desc_.SetBasicBlockToLoop(bb_id, dead_it->second);
        } else {
          loop_desc_.ForgetBasicBlock(bb_id);
        }
      } else {
        // The block is dead, but the loop it belongs to is not, check if this
        // is an unreachable merge.
        if (l->GetMergeBlock()->id() == bb_id)
          unreachable_merges->insert(bb_id);
      }
    }

    // Remove dead blocks from live loops.
    for (uint32_t bb_id : dead_blocks) {
      ir::Loop* l = loop_desc_[bb_id];
      if (l) l->RemoveBasicBlock(bb_id);
    }

    std::for_each(
        dead_loops.begin(), dead_loops.end(),
        [this](
            std::unordered_map<ir::Loop*, ir::Loop*>::iterator::reference it) {
          if (it.first == loop_) loop_ = nullptr;
          loop_desc_.RemoveLoop(it.first);
        });
  }

  // Populates the loop nest according to the original loop nest.
  // Any killed loop in the cloned loop will not appear and OpLoopMerge inst
  // will be killed.
  // |dead_blocks| contains the set of blocks that are no longer reachable.
  // |unreachable_merges| will contains dead merge blocks for live loops.
  void PopulateLoopNest(const std::unordered_set<uint32_t>& dead_blocks,
                        std::unordered_set<uint32_t>* unreachable_merges) {
    std::unordered_map<ir::Loop*, ir::Loop*> loop_mapping;
    auto get_parent = [&loop_mapping](ir::Loop* loop) -> ir::Loop* {
      for (ir::Loop* l = loop; l; l = l->GetParent()) {
        std::unordered_map<ir::Loop*, ir::Loop*>::iterator it =
            loop_mapping.find(l);
        if (it != loop_mapping.end()) return it->second;
      }
      return nullptr;
    };

    for (ir::Loop& sub_loop :
         ir::make_range(opt::TreeDFIterator<ir::Loop>(loop_),
                        opt::TreeDFIterator<ir::Loop>())) {
      if (IsLoopDead(old_to_new_bb_.at(sub_loop.GetHeaderBlock()->id()),
                     old_to_new_bb_.at(sub_loop.GetLatchBlock()->id()),
                     dead_blocks))
        continue;
      std::unique_ptr<ir::Loop> cloned = MakeUnique<ir::Loop>();
      if (ir::Loop* parent = get_parent(sub_loop.GetParent()))
        parent->AddNestedLoop(cloned.get());
      PopulateLoopDesc(cloned.get(), &sub_loop, dead_blocks,
                       unreachable_merges);
    }

    std::for_each(
        loop_mapping.begin(), loop_mapping.end(),
        [this](
            std::unordered_map<ir::Loop*, ir::Loop*>::iterator::reference it) {
          if (!it.second->GetParent())
            loop_desc_.AddLoops(std::unique_ptr<ir::Loop>(it.second));
        });
  }

  // Populates |new_loop| descriptor according to |old_loop|'s one.
  void PopulateLoopDesc(ir::Loop* new_loop, ir::Loop* old_loop,
                        const std::unordered_set<uint32_t>& dead_blocks,
                        std::unordered_set<uint32_t>* unreachable_merges) {
    for (uint32_t bb_id : old_loop->GetBlocks()) {
      ir::BasicBlock* bb = old_to_new_bb_.at(bb_id);
      if (!dead_blocks.count(bb->id())) new_loop->AddBasicBlock(bb);
    }
    new_loop->SetHeaderBlock(
        old_to_new_bb_.at(old_loop->GetHeaderBlock()->id()));
    if (old_loop->GetLatchBlock())
      new_loop->SetLatchBlock(
          old_to_new_bb_.at(old_loop->GetLatchBlock()->id()));
    if (old_loop->GetMergeBlock()) {
      ir::BasicBlock* bb = old_to_new_bb_.at(old_loop->GetMergeBlock()->id());
      // The merge block might be unreachable, in which case it will be tagged
      // as dead. We need it, so mark it as must be kept.
      if (dead_blocks.count(bb->id())) {
        unreachable_merges->insert(bb->id());
      }
      new_loop->SetMergeBlock(bb);
    }
    if (old_loop->GetPreHeaderBlock())
      new_loop->SetPreHeaderBlock(
          old_to_new_bb_.at(old_loop->GetPreHeaderBlock()->id()));
  }

  // Returns true if |insn| is constant within the loop.
  bool IsConditionLoopIV(ir::Instruction* insn) {
    assert(insn->IsBranch());
    assert(insn->opcode() != SpvOpBranch);
    analysis::DefUseManager* def_use_mgr = context_->get_def_use_mgr();

    ir::Instruction* condition =
        def_use_mgr->GetDef(insn->GetOperand(0).words[0]);
    return !loop_->IsInsideLoop(condition);
  }
};

}  // namespace

Pass::Status LoopUnswitchPass::Process(ir::IRContext* c) {
  InitializeProcessing(c);

  bool modified = false;
  ir::Module* module = c->module();

  // Process each function in the module
  for (ir::Function& f : *module) {
    modified |= ProcessFunction(&f);
  }

  return modified ? Status::SuccessWithChange : Status::SuccessWithoutChange;
}

bool LoopUnswitchPass::ProcessFunction(ir::Function* f) {
  bool modified = false;
  std::unordered_set<ir::Loop*> processed_loop;

  ir::LoopDescriptor& loop_descriptor = *context()->GetLoopDescriptor(f);

  std::list<ir::Loop*> work_list;
  for (ir::Loop& loop : loop_descriptor) work_list.push_back(&loop);

  bool change = true;
  while (change) {
    change = false;
    for (ir::Loop& loop :
         ir::make_range(++opt::TreeDFIterator<ir::Loop>(
                            loop_descriptor.GetDummyRootLoop()),
                        opt::TreeDFIterator<ir::Loop>())) {
      if (processed_loop.count(&loop)) continue;
      processed_loop.insert(&loop);

      LoopUnswitch unswitcher(context(), f, &loop, &loop_descriptor);
      while (!unswitcher.WasLoopKilled() && unswitcher.CanUnswitchLoop()) {
        if (loop.IsLCSSA()) {
          LoopUtils(context(), &loop).MakeLoopClosedSSA();
        }
        modified = true;
        change = true;
        unswitcher.PerformUnswitch();
      }
    }
  }

  return modified;
}

}  // namespace opt
}  // namespace spvtools
