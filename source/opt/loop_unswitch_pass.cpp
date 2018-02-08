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

#include "loop_unswitch_pass.h"
#include "dominator_tree.h"
#include "fold.h"
#include "ir_builder.h"
#include "loop_descriptor.h"

#include "filter_iterator.h"

#include <type_traits>

namespace spvtools {
namespace opt {
namespace {

constexpr uint32_t kBranchCondTrueLabIdInIdx = 1;
constexpr uint32_t kBranchCondFalseLabIdInIdx = 2;

}  // anonymous namespace

namespace {

class LoopUnswitch {
 public:
  LoopUnswitch(ir::IRContext* context, ir::Function* function, ir::Loop* loop,
               ir::LoopDescriptor* loop_desc)
      : function_(function),
        loop_(loop),
        loop_desc_(*loop_desc),
        switch_block_(nullptr),
        dom_tree_(context->GetDominatorAnalysis(function, *context->cfg())
                      ->GetDomTree()),
        context_(context),
        def_use_mgr_(context->get_def_use_mgr()) {}

  bool CanUnswitchLoop() {
    if (switch_block_) return true;
    if (loop_->IsSafeToClone()) return false;

    ir::CFG& cfg = *context_->cfg();

    for (uint32_t bb_id : loop_->GetBlocks()) {
      ir::BasicBlock* bb = cfg.block(bb_id);
      if (bb->terminator()->opcode() == SpvOpBranchConditional) {
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
    // Create the dedicate exit basic block.
    ir::BasicBlock* bb = &*ip.InsertBefore(std::unique_ptr<ir::BasicBlock>(
        new ir::BasicBlock(std::unique_ptr<ir::Instruction>(new ir::Instruction(
            context_, SpvOpLabel, 0, context_->TakeNextId(), {})))));
    bb->SetParent(function_);
    def_use_mgr_->AnalyzeInstDef(bb->GetLabelInst());
    context_->set_instr_block(bb->GetLabelInst(), bb);

    if (ir::Loop* loop = loop_desc_[&*ip]) {
      loop_->AddBasicBlock(bb);
      loop_desc_.SetBasicBlockToLoop(bb->id(), loop);
    }

    return bb;
  }

  // Unswitches |loop_|.
  void PerformUnswitch() {
    assert(CanUnswitchLoop() &&
           "Cannot unswitch if there is not constant condition");
    assert(loop_->GetPreHeaderBlock() && "This loop has no pre-header block");
    assert(loop_->IsLCSSA() && "This loop is not in a LCSSA form");

    cloned_loop_.clear();
    ir::CFG& cfg = *context_->cfg();

    //////////////////////////////////////////////////////////////////////////////
    // Step 1: Create the if merge block for structured modules.
    //    To do so, the |loop_| merge block will become the if's one and we
    //    create a merge for the loop. This will limit the amount of duplicated
    //     code the structured control flow imposes.
    //    For non structured program, the new loop will be connected to
    //     the old loop's exit blocks.
    //////////////////////////////////////////////////////////////////////////////

    // Get the merge block if it exists.
    ir::BasicBlock* if_merge_block = loop_->GetMergeBlock();
    // Merge block, it is only created if the loop has a unique exit block. We
    // have this guaranty for structured loops, for compute loop it will
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
      def_use_mgr_->AnalyzeInstDef(loop_merge_block->GetLabelInst());
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
      // Copy the predecessor list (will get invalidate otherwise).
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
      // Update the dominator tree.
      DominatorTreeNode* loop_merge_dtn =
          dom_tree_.GetOrInsertNode(loop_merge_block);
      DominatorTreeNode* if_merge_block_dtn =
          dom_tree_.GetOrInsertNode(if_merge_block);
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
    //         for the constant if.
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

    // Update the CFG.
    cfg.RegisterBlock(loop_pre_header);
    def_use_mgr_->AnalyzeInstDef(loop_pre_header->GetLabelInst());
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
        dom_tree_.GetOrInsertNode(loop_pre_header);
    DominatorTreeNode* if_block_dtn = dom_tree_.GetTreeNode(if_block);
    loop_pre_header_dtn->parent_ = if_block_dtn;
    assert(if_block_dtn->children_.size() == 1 &&
           "A loop preheader should only have the header block as child in the "
           "dominator tree");
    loop_pre_header_dtn->children_.push_back(if_block_dtn->children_[0]);
    if_block_dtn->children_.clear();
    if_block_dtn->children_.push_back(loop_pre_header_dtn);

    // Make domination queries valid.
    dom_tree_.ResetDFNumbering();

    //////////////////////////////////////////////////////////
    // Step 3: We have the landing pads, duplicate |loop_|. //
    //////////////////////////////////////////////////////////

    std::list<std::unique_ptr<ir::BasicBlock>> ordered_loop_bb;
    std::unique_ptr<ir::Loop> loop_false_br(CloneLoop(&ordered_loop_bb));

    ///////////////////////////////////////////////////////////
    // Step 4: Connect convergent edges to the landing pads. //
    ///////////////////////////////////////////////////////////

    std::unordered_set<uint32_t> if_merging_blocks;
    if (loop_->GetHeaderBlock()->GetLoopMergeInst()) {
      if_merging_blocks.insert(if_merge_block->id());
    } else {
      loop_->GetExitBlocks(context_, &if_merging_blocks);
    }
    for (uint32_t merge_bb_id : if_merging_blocks) {
      ir::BasicBlock* merge = context_->cfg()->block(merge_bb_id);
      // LCSSA, so we only care about phi instructions.
      merge->ForEachPhiInst([this](ir::Instruction* phi) {
        uint32_t num_in_operands = phi->NumInOperands();
        for (uint32_t i = 0; i < num_in_operands; i++) {
          ir::Operand new_operand = phi->GetInOperand(i);
          // not all the incoming value comes from the loop.
          ValueMapTy::iterator new_value =
              value_map_.find(new_operand.words[0]);
          if (new_value != value_map_.end()) {
            new_operand.words[0] = new_value->second;
          }
          phi->AddOperand(std::move(new_operand));
        }
      });
    }

    ////////////////////////////////////
    // Step 5: Specializes the loops. //
    ////////////////////////////////////

    ir::Instruction* iv_condition = &*switch_block_->tail();
    ir::Instruction* condition =
        def_use_mgr_->GetDef(iv_condition->GetOperand(0).words[0]);

    analysis::ConstantManager* cst_mgr = context_->get_constant_mgr();
    const analysis::Type* cond_type =
        context_->get_type_mgr()->GetType(condition->type_id());
    ir::Instruction* cst_false =
        cst_mgr->GetDefiningInstruction(cst_mgr->GetConstant(cond_type, {0}));
    ir::Instruction* cst_true =
        cst_mgr->GetDefiningInstruction(cst_mgr->GetConstant(cond_type, {1}));

    {
      std::unordered_set<uint32_t> dead_blocks;
      std::unordered_set<uint32_t> unreachable_merges;
      SimplifyLoop(loop_false_br.get(), condition, cst_false,
                   [this](uint32_t id) {
                     BlockMapTy::iterator it = new_to_old_bb_.find(id);
                     return it != new_to_old_bb_.end() ? it->second : nullptr;
                   },
                   &dead_blocks);
      // We tagged dead blocks, create the loop before we invalidate any basic
      // block.
      PopulateLoopNest(*loop_false_br.get(), dead_blocks, &unreachable_merges);
      CleanUpCFG(*loop_false_br.get(), ir::make_range(ordered_loop_bb).begin(),
                 dead_blocks, unreachable_merges);
      function_->AddBasicBlocks(ordered_loop_bb.begin(), ordered_loop_bb.end(),
                                ++FindBasicBlockPosition(if_block));
    }
    {
      std::unordered_set<uint32_t> dead_blocks;
      std::unordered_set<uint32_t> unreachable_merges;
      SimplifyLoop(loop_, condition, cst_true,
                   [&cfg](uint32_t id) { return cfg.block(id); }, &dead_blocks);
      // Might not be unreachable, but we don't want this block to be touched.
      unreachable_merges.insert(if_merge_block->id());
      CleanUpCFG(*loop_, function_->begin(), dead_blocks, unreachable_merges);
    }

    /////////////////////////////////////
    // Finally: connect the new loops. //
    /////////////////////////////////////

    // Delete the old jump
    if_block->tail().Erase();
    opt::InstructionBuilder(context_, if_block)
        .AddConditionalBranch(
            condition->result_id(), loop_->GetPreHeaderBlock()->id(),
            loop_false_br->GetPreHeaderBlock()->id(),
            if_merge_block ? if_merge_block->id() : kInvalidId);
    context_->InvalidateAnalysesExceptFor(
        ir::IRContext::Analysis::kAnalysisLoopAnalysis);
    std::cout << *function_;
  }

  const std::vector<ir::Loop*>& GetClonedLoops() const { return cloned_loop_; }

 private:
  ir::Function* function_;
  ir::Loop* loop_;
  ir::LoopDescriptor& loop_desc_;
  ir::BasicBlock* switch_block_;
  DominatorTree& dom_tree_;
  ir::IRContext* context_;
  analysis::DefUseManager* def_use_mgr_;
  std::vector<ir::Loop*> cloned_loop_;

  using ValueMapTy = std::unordered_map<uint32_t, uint32_t>;
  using BlockMapTy = std::unordered_map<uint32_t, ir::BasicBlock*>;

  ValueMapTy value_map_;
  // Mapping between original loop blocks to the cloned one and oposite.
  BlockMapTy old_to_new_bb_;
  BlockMapTy new_to_old_bb_;

  uint32_t TakeNextId() { return context_->TakeNextId(); }

  template <template <typename...> class ContainerType>
  void CleanUpCFG(
      const ir::Loop& loop,
      ir::UptrContainerIterator<ir::BasicBlock, ContainerType> bb_it,
      const std::unordered_set<uint32_t>& dead_blocks,
      const std::unordered_set<uint32_t>& unreachable_merges) {
    ir::CFG& cfg = *context_->cfg();
    while (bb_it != bb_it.end()) {
      if (!loop.IsInsideLoop(bb_it->id())) {
        ++bb_it;
        continue;
      }
      if (unreachable_merges.count(bb_it->id()) &&
          dead_blocks.count(bb_it->id())) {
        if (bb_it->begin() != bb_it->tail() ||
            bb_it->terminator()->opcode() != SpvOpUnreachable) {
          // Make unreachable, but leave the label.
          bb_it->KillAllInsts(false);
          opt::InstructionBuilder(context_, &*bb_it).AddUnreachable();
          cfg.RemoveNonExistingEdges(bb_it->id());
        }
        ++bb_it;
      } else if (dead_blocks.count(bb_it->id())) {
        cfg.ForgetBlock(&*bb_it);
        // Kill this block.
        bb_it->KillAllInsts(true);
        bb_it = bb_it.Erase();
      } else {
        cfg.RemoveNonExistingEdges(bb_it->id());
        ++bb_it;
      }
    }
  }

  bool GetConstCondition(const ir::Instruction* cInst, bool* condVal) {
    bool condIsConst;
    switch (cInst->opcode()) {
      case SpvOpConstantFalse: {
        *condVal = false;
        condIsConst = true;
      } break;
      case SpvOpConstantTrue: {
        *condVal = true;
        condIsConst = true;
      } break;
      default: { condIsConst = false; } break;
    }
    return condIsConst;
  }

  // Tags |killed_bb| as dead and add all successors label to the work list.
  void KillBasicBlock(ir::BasicBlock* killed_bb,
                      std::unordered_set<uint32_t>* dead_blocks,
                      std::unordered_set<ir::Instruction*>* work_list) {
    dead_blocks->insert(killed_bb->id());
    std::cout << "Kill " << killed_bb->id() << "\n";
    def_use_mgr_->ForEachUser(
        killed_bb->GetLabelInst(), [work_list](ir::Instruction* i) {
          // Capture merge and phi instructions only.
          if (!i->IsBranch()) {
            std::cout << "Push non branch use " << i->result_id() << "\n";
            work_list->insert(i);
          }
        });
    killed_bb->ForEachSuccessorLabel([work_list, this](uint32_t sid) {
      std::cout << "Push dead candidate " << sid << "\n";
      work_list->insert(def_use_mgr_->GetDef(sid));
    });
  }

  // Simplifies |loop| assuming the instruction |to_version_insn| takes the
  // value |cst_value|.
  // Requirements:
  //   - |loop| must be is the LCSSA form;
  //   - |cst_value| must be constant.
  // The set |dead_blocks| will contains dead basic blocks, and the list
  // |ordered_loop_bb| will contains live basic blocks in reverse post-order.
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
    uint32_t cst_value_id = cst_value->result_id();
    // auto id_mapping = [to_version_insn_id, cst_value_id](uint32_t id) {
    //   if (to_version_insn_id == id) return cst_value_id;
    //   return id;
    // };

    opt::DominatorTreeNode* merge_dtn = dom_tree_.GetTreeNode(
        new_to_old_block_mapping(loop->GetMergeBlock()->id()));
    std::function<bool(uint32_t)> ignore_node_and_children;
    if (loop_->GetHeaderBlock()->GetLoopMergeInst()) {
      ignore_node_and_children = [merge_dtn, new_to_old_block_mapping,
                                  this](uint32_t bb_id) {
        ir::BasicBlock* bb = new_to_old_block_mapping(bb_id);
        if (!bb) return true;
        opt::DominatorTreeNode* dtn = dom_tree_.GetTreeNode(bb);
        return dom_tree_.Dominates(merge_dtn, dtn);
      };
    } else {
      ignore_node_and_children = [loop](uint32_t bb_id) {
        return !loop->IsInsideLoop(bb_id);
      };
    }

    std::unordered_set<ir::Instruction*> work_list;
    std::unordered_set<uint32_t> visited;

    def_use_mgr_->ForEachUse(
        to_version_insn, [cst_value_id, &work_list, &ignore_node_and_children,
                          this](ir::Instruction* i, uint32_t operand_index) {
          ir::BasicBlock* bb = context_->get_instr_block(i);
          if (!ignore_node_and_children(bb->id())) {
            i->SetOperand(operand_index, {cst_value_id});
            work_list.insert(i);
          }
        });

    while (!work_list.empty()) {
      ir::Instruction* inst = *work_list.begin();
      ir::BasicBlock* bb = context_->get_instr_block(inst);
      ir::BasicBlock* bb_map = new_to_old_block_mapping(bb->id());
      std::cout << "bb (" << (bb_map ? bb_map->id() : 0) << " ): " << bb->id()
                << " -> " << inst->result_id() << "\n";
      work_list.erase(work_list.begin());

      // if the basic block is known as dead, ignore the instruction.
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
          KillBasicBlock(bb, dead_blocks, &work_list);
        }
        continue;
      }

      if (inst->opcode() == SpvOpLoopMerge) {
        if (dead_blocks->count(inst->GetSingleWordInOperand(1))) {
          def_use_mgr_->ClearInst(inst);
          context_->KillInst(inst);
        }
        continue;
      }

      if (inst->IsBranch()) {
        uint32_t live_target = 0;
        switch (inst->opcode()) {
          case SpvOpBranchConditional: {
            const ir::Instruction* cond =
                def_use_mgr_->GetDef(inst->GetOperand(0).words[0]);
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
              std::cout << "Push dead candidate " << dead_target << "\n";
              work_list.insert(def_use_mgr_->GetDef(dead_target));
            }
            break;
          }
          case SpvOpSwitch: {
            ir::Instruction::iterator op_it = inst->begin();
            const ir::Instruction* condition =
                def_use_mgr_->GetDef(op_it->words[0]);
            if (!condition || !condition->IsConstant()) break;
            //  FIXME: more abstraction required ?
            const ir::Operand& cst = condition->GetOperand(1);
            ++op_it;
            uint32_t default_target = op_it->words[0];
            ++op_it;
            std::vector<uint32_t> possibly_dead;
            while (op_it != inst->end()) {
              const ir::Operand& literal = *op_it;
              ++op_it;
              uint32_t target = op_it->words[0];
              ++op_it;
              if (literal == cst) {
                live_target = target;
              } else {
                possibly_dead.push_back(target);
              }
            }
            if (live_target != kInvalidId) {
              live_target = default_target;
            } else {
              possibly_dead.push_back(default_target);
            }
            for (uint32_t id : possibly_dead) {
              if (id != live_target) {
                cfg.RemoveEdge(bb->id(), id);
                std::cout << "Push dead candidate " << id << "\n";
                work_list.insert(def_use_mgr_->GetDef(id));
              }
            }
          }
          default:
            break;
        }
        if (live_target != kInvalidId) {
          bb->tail().Erase();
          // Check for the presence of the merge block.
          if (bb->begin() != bb->end() &&
              bb->tail()->opcode() == SpvOpSelectionMerge)
            bb->tail().Erase();
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
          def_use_mgr_->AnalyzeInstUse(inst);
        }
        // Let the general case handle the phi folding.
      }

      // General case, try to fold or forget about this use.
      ir::Instruction* folded = opt::FoldInstruction(inst);
      assert(!folded || &*bb->tail() != inst && "Terminator was folded");
      if (folded) {
        std::unordered_set<ir::Instruction*> modifed_instructions;
        def_use_mgr_->ReplaceAllUseOf(inst->result_id(), folded->result_id(),
                                      &modifed_instructions);
        std::cout << "Remap " << inst->result_id() << " into "
                  << folded->result_id() << "\n";
        work_list.insert(modifed_instructions.begin(),
                         modifed_instructions.end());
        std::for_each(modifed_instructions.begin(), modifed_instructions.end(),
                      [&, this](ir::Instruction* i) {
                        std::cout << "\t" << i->result_id() << " in "
                                  << context_->get_instr_block(i)->id() << "\n";
                      });
        context_->KillInst(inst);
      }
    }
  }

  // Clone the current loop and remap instructions. Newly created blocks will be
  // added to the |ordered_loop_bb| list, correctly ordered to be inserted into
  // a function. If the loop is structured, the merge construct will also be
  // cloned.
  // The function preserves the loop analysis.
  std::unique_ptr<ir::Loop> CloneLoop(
      std::list<std::unique_ptr<ir::BasicBlock>>* ordered_loop_bb) {
    std::unique_ptr<ir::Loop> new_loop = MakeUnique<ir::Loop>();
    if (loop_->HasParent()) new_loop->SetParent(loop_->GetParent());

    std::function<bool(uint32_t)> ignore_node_and_children;
    if (loop_->GetHeaderBlock()->GetLoopMergeInst()) {
      ignore_node_and_children = [this](uint32_t bb_id) {
        return dom_tree_.StrictlyDominates(loop_->GetMergeBlock()->id(), bb_id);
      };
    } else {
      ignore_node_and_children = [this](uint32_t bb_id) {
        return !loop_->IsInsideLoop(bb_id);
      };
    }

    ir::CFG& cfg = *context_->cfg();

    opt::DominatorTreeNode* dtn_root =
        dom_tree_.GetTreeNode(loop_->GetPreHeaderBlock());
    opt::DominatorTreeNode::df_iterator dom_it = dtn_root->df_begin();

    // Clone and place blocks in a SPIR-V compliant order (dominators first).
    while (dom_it != dtn_root->df_end()) {
      // For each basic block in the loop, we clone it and register the mapping
      // between old and new ids.
      uint32_t old_bb_id = dom_it->id();
      // If we are out of our ROI, skip the node and the children.
      if (ignore_node_and_children(old_bb_id)) {
        dom_it.SkipChildren();
        continue;
      }
      ir::BasicBlock* old_bb = dom_it->bb_;
      ++dom_it;
      ir::BasicBlock* new_bb = old_bb->Clone(context_);
      new_bb->SetParent(function_);
      new_bb->GetLabelInst()->SetResultId(TakeNextId());
      def_use_mgr_->AnalyzeInstDef(new_bb->GetLabelInst());
      context_->set_instr_block(new_bb->GetLabelInst(), new_bb);
      ordered_loop_bb->emplace_back(new_bb);

      // Keep track of the new basic block, we will need it later on.
      old_to_new_bb_[old_bb->id()] = new_bb;
      new_to_old_bb_[new_bb->id()] = old_bb;
      value_map_[old_bb->id()] = new_bb->id();
      // value_map_[new_bb->id()] = old_bb->id();
      if (loop_->IsInsideLoop(old_bb)) new_loop->AddBasicBlock(new_bb);

      for (auto& inst : *new_bb) {
        if (inst.HasResultId()) {
          uint32_t old_result_id = inst.result_id();
          inst.SetResultId(TakeNextId());
          value_map_[old_result_id] = inst.result_id();

          // Only look at the defs for now, uses are not updated yet.
          def_use_mgr_->AnalyzeInstDef(&inst);
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
        def_use_mgr_->AnalyzeInstUse(&insn);
        context_->set_instr_block(&insn, bb);
      }
      cfg.RegisterBlock(bb);
    }

    PopulateLoopDesc(new_loop.get(), loop_, true, {}, {});

    return new_loop;
  }

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

  // Populates the loop nest according to the original loop nest.
  // Any killed loop in the cloned loop will not appear and OpLoopMerge inst
  // will be killed.
  void PopulateLoopNest(const ir::Loop& new_loop,
                        const std::unordered_set<uint32_t>& dead_blocks,
                        std::unordered_set<uint32_t>* unreachable_merges) {
    (void)new_loop;
    std::unordered_map<ir::Loop*, ir::Loop*> loop_mapping;
    // {
    //   if (!IsLoopDead(old_to_new_bb_.at(loop_->GetHeaderBlock()->id()),
    //                   old_to_new_bb_.at(loop_->GetLatchBlock()->id()),
    //                   dead_blocks)) {
    //     // Check that the loop was not killed.
    //     if (loop_->GetParent())
    //       loop_->GetParent()->AddNestedLoop(new_loop.get());
    //     PopulateLoopDesc(new_loop.get(), loop_, false, dead_blocks,
    //                      unreachable_merges);
    //     loop_mapping[loop_] = new_loop.release();
    //   }
    // }
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
      PopulateLoopDesc(cloned.get(), &sub_loop,
                       sub_loop.begin() == sub_loop.end(), dead_blocks,
                       unreachable_merges);
    }

    std::for_each(
        loop_mapping.begin(), loop_mapping.end(),
        [this](
            std::unordered_map<ir::Loop*, ir::Loop*>::iterator::reference it) {
          cloned_loop_.push_back(it.second);
          if (!it.second->GetParent())
            loop_desc_.AddLoops(std::unique_ptr<ir::Loop>(it.second));
        });
  }

  // Populates |new_loop| descriptor according to |old_loop|'s one.
  void PopulateLoopDesc(ir::Loop* new_loop, ir::Loop* old_loop,
                        bool set_loop_blocks,
                        const std::unordered_set<uint32_t>& dead_blocks,
                        std::unordered_set<uint32_t>* unreachable_merges) {
    new_loop->SetHeaderBlock(
        old_to_new_bb_.at(old_loop->GetHeaderBlock()->id()));
    if (old_loop->GetLatchBlock())
      new_loop->SetLatchBlock(
          old_to_new_bb_.at(old_loop->GetLatchBlock()->id()));
    if (old_loop->GetMergeBlock()) {
      ir::BasicBlock* bb = old_to_new_bb_.at(old_loop->GetMergeBlock()->id());
      // The merge block might be unreachable, in which case it will be tagged
      // as dead. We need it, so mark it as must be kept.
      if (dead_blocks.count(bb->id())) unreachable_merges->insert(bb->id());
      new_loop->SetMergeBlock(bb);
    }
    if (old_loop->GetPreHeaderBlock())
      new_loop->SetPreHeaderBlock(
          old_to_new_bb_.at(old_loop->GetPreHeaderBlock()->id()));
    if (set_loop_blocks) {
      for (uint32_t bb_id : old_loop->GetBlocks()) {
        ir::BasicBlock* bb = old_to_new_bb_.at(bb_id);
        if (!dead_blocks.count(bb->id())) new_loop->AddBasicBlock(bb);
      }
    }
  }

  // Returns true if |insn| is constant within the loop.
  bool IsConditionLoopIV(ir::Instruction* insn) {
    assert(insn->IsBranch());
    assert(insn->opcode() != SpvOpBranch);
    ir::Instruction* condition =
        def_use_mgr_->GetDef(insn->GetOperand(0).words[0]);
    return !loop_->IsInsideLoop(condition);
  }
};

}  // namespace

Pass::Status LoopUnswitchPass::Process(ir::IRContext* context) {
  InitializeProcessing(context);

  bool modified = false;
  ir::Module* module = context->module();

  // Process each function in the module
  for (ir::Function& f : *module) {
    modified |= ProcessFunction(&f);
  }

  return modified ? Status::SuccessWithChange : Status::SuccessWithoutChange;
}

bool LoopUnswitchPass::ProcessFunction(ir::Function* f) {
  bool modified = false;
  ir::LoopDescriptor& loop_descriptor = *context()->GetLoopDescriptor(f);
  std::list<ir::Loop*> work_list;
  for (ir::Loop& loop : loop_descriptor) work_list.push_back(&loop);

  while (!work_list.empty()) {
    ir::Loop* current_loop = work_list.front();
    LoopUnswitch unswitcher(context(), f, current_loop, &loop_descriptor);
    work_list.pop_front();
    if (unswitcher.CanUnswitchLoop()) {
      modified = true;
      unswitcher.PerformUnswitch();
      auto& created_loops = unswitcher.GetClonedLoops();
      work_list.insert(work_list.begin(), created_loops.begin(),
                       created_loops.end());
      work_list.push_front(current_loop);
    }
  }

  return modified;
}

}  // namespace opt
}  // namespace spvtools
