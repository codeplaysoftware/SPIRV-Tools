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
      if (bb->CountSuccessor() > 1) {
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
  // position |ip|. This function preserves the loop analysis.
  ir::BasicBlock* CreateBasicBlock(ir::Function::iterator ip) {
    // Create the dedicate exit basic block.
    ir::BasicBlock& bb = *ip.InsertBefore(std::unique_ptr<ir::BasicBlock>(
        new ir::BasicBlock(std::unique_ptr<ir::Instruction>(new ir::Instruction(
            context_, SpvOpLabel, 0, context_->TakeNextId(), {})))));
    bb.SetParent(function_);
    if (ir::Loop* loop = loop_desc_[&*ip]) {
      loop_->AddBasicBlock(&bb);
      loop_desc_.SetBasicBlockToLoop(bb.id(), loop);
    }

    return &bb;
  }

  // Unswitches |loop_|.
  void PerformUnswitch() {
    assert(CanUnswitchLoop() &&
           "Cannot unswitch if there is not constant condition");
    assert(loop_->GetPreHeaderBlock() && "This loop has no pre-header block");
    assert(loop_->IsLCSSA() && "This loop is not in a LCSSA form");

    cloned_loop_.clear();

    //////////////////////////////////////////////////////////////////////////////
    // Step 1: Create the if merge block for structured modules. //
    //    To do so, the |loop_| merge block will become the if's one and we //
    //    create a merge for the loop. This will limit the amount of duplicated
    //    // code the structured control flow imposes.
    //    // For non structured program, the new loop will be connected to
    //    // the old loop's exit blocks.
    //    //
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
      ir::CFG& cfg = *context_->cfg();
      // Add the instruction and update managers.
      opt::InstructionBuilder builder(
          context_, loop_merge_block,
          ir::IRContext::kAnalysisDefUse |
              ir::IRContext::kAnalysisInstrToBlockMapping);
      builder.AddBranch(if_merge_block->id());
      builder.SetInsertPoint(&*loop_merge_block->begin());
      cfg.RegisterBlock(loop_merge_block);
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
        ir::BasicBlock* p_bb = cfg.block(pid);
        p_bb->ForEachSuccessorLabel(
            [if_merge_block, loop_merge_block](uint32_t* id) {
              if (*id == if_merge_block->id()) *id = loop_merge_block->id();
            });
        cfg.AddEdge(pid, loop_merge_block->id());
      }
      cfg.RemoveNonExistingEdges(if_merge_block->id());

      loop_->SetMergeBlock(loop_merge_block);
    }

    ///////////////////////////////////////////////////////////////////////////////
    // Step 2: if |loop_|'s preheader is a loop header, build a header block for
    // //
    //         the constant if. //
    ///////////////////////////////////////////////////////////////////////////////

    ir::BasicBlock* if_block = nullptr;
    // If this preheader is the parent loop header,
    // we need to create a dedicated block for the if.
    if (loop_->GetPreHeaderBlock()->IsLoopHeader()) {
      ir::BasicBlock* parent_loop_header = loop_->GetPreHeaderBlock();
      if_block = CreateBasicBlock(++FindBasicBlockPosition(if_block));
      parent_loop_header->tail()->SetInOperand(0, {if_block->id()});
      // FIXME: CFG update.
      loop_->GetHeaderBlock()->ForEachPhiInst(
          [parent_loop_header, if_block](ir::Instruction* phi) {
            phi->ForEachInId([parent_loop_header, if_block](uint32_t* id) {
              if (*id == parent_loop_header->id()) {
                *id = if_block->id();
              }
            });
          });
    } else {
      if_block = loop_->GetPreHeaderBlock();
      // Delete the old jump, it will be added at the end of the function..
      if_block->tail().Erase();
    }

    //////////////////////////////////////////////////////////
    // Step 3: We have the landing pads, duplicate |loop_|. //
    //////////////////////////////////////////////////////////

    std::list<std::unique_ptr<ir::BasicBlock>> ordered_loop_bb;
    ValueMapTy value_map;
    ir::Loop* loop_false_br = CloneLoop(&ordered_loop_bb, &value_map);
    cloned_loop_.push_back(loop_false_br);

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
      merge->ForEachPhiInst([&value_map](ir::Instruction* phi) {
        uint32_t num_in_operands = phi->NumInOperands();
        for (uint32_t i = 0; i < num_in_operands; i++) {
          ir::Operand new_operand = phi->GetInOperand(i);
          // not all the incoming value comes from the loop.
          ValueMapTy::iterator new_value = value_map.find(new_operand.words[0]);
          if (new_value != value_map.end()) {
            new_operand.words[0] = new_value->second;
          }
          phi->AddOperand(std::move(new_operand));
        }
      });
    }

    ////////////////////////////////////
    // Step 5: Specialized the loops. //
    ////////////////////////////////////

    ir::Instruction* iv_condition = &*switch_block_->tail();
    ir::Instruction* condition =
        def_use_mgr_->GetDef(iv_condition->GetOperand(0).words[0]);

#if 0
    analysis::ConstantManager* cst_mgr = context_->get_constant_mgr();
    ir::Instruction* cst_false = cst_mgr->GetDefiningInstruction(
        cst_mgr->GetConstant(cst_mgr->GetType(iv_condition), {0}));
    ir::Instruction* cst_true = cst_mgr->GetDefiningInstruction(
        cst_mgr->GetConstant(cst_mgr->GetType(iv_condition), {1}));

    {
      std::unordered_set<uint32_t> dead_blocks;
      SimplifyLoop(loop_false_br, iv_condition, cst_false, &dead_blocks,
                   &ordered_loop_bb);
    }
    {
      std::unordered_set<uint32_t> dead_blocks;
      SimplifyLoop(loop_, iv_condition, cst_true, &dead_blocks,
                   &ordered_loop_bb);
    }
#endif
    /////////////////////////////////////
    // Finally: connect the new loops. //
    /////////////////////////////////////

    opt::InstructionBuilder(context_, if_block,
                            ir::IRContext::kAnalysisDefUse |
                                ir::IRContext::kAnalysisInstrToBlockMapping)
        .AddConditionalBranch(
            condition->result_id(), loop_->GetPreHeaderBlock()->id(),
            loop_false_br->GetPreHeaderBlock()->id(),
            if_merge_block ? if_merge_block->id() : kInvalidId);
    // FIXME: Update mangers.
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

  uint32_t TakeNextId() { return context_->TakeNextId(); }

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

  struct InIdOperandPredicate {
    bool operator()(const ir::Instruction::iterator& it) const {
      switch (it->type) {
        case SPV_OPERAND_TYPE_RESULT_ID:
        case SPV_OPERAND_TYPE_TYPE_ID:
          break;
        default:
          if (spvIsIdType(it->type)) return true;
          break;
      }
      return false;
    }
  };

  // Tags |killed_bb| as dead (by inserting it into the |dead_blocks| set) and
  // update the dominator tree. The function also insert all nodes dominated by
  // |killed_bb|.
  void KillBasicBlock(uint32_t killed_bb,
                      std::unordered_set<uint32_t>* dead_blocks) {
    DominatorTreeNode* dtn = dom_tree_.GetTreeNode(killed_bb);
    for (const DominatorTreeNode* node :
         ir::make_range(dtn->begin(), dtn->end())) {
      dead_blocks->insert(node->id());
    }
    // Leave the dominator tree valid.
    dom_tree_.RecursivelyDeleteTreeNode(dtn);
  }

  // Simplifies |loop| assuming the instruction |to_version_insn| takes the
  // value |cst_value|.
  // Requirements:
  //   - |loop| must be is the LCSSA form;
  //   - |cst_value| must be constant.
  // The set |dead_blocks| will contains dead basic blocks, and the list
  // |ordered_loop_bb| will contains live basic blocks in reverse post-order.
  void SimplifyLoop(ir::Loop* loop, ir::Instruction* to_version_insn,
                    ir::Instruction* cst_value,
                    std::unordered_set<uint32_t>* dead_blocks) {
    // Version the |loop| body.
    // Do a DFS that takes into account the specialized value of
    // to_version_insn. As we go we:
    //  - Fold instructions;
    //  - Hoist any loop invariant;
    //  - Only keep reachable basic block.
    std::unordered_set<uint32_t> visited;
    ir::CFG& cfg = *context_->cfg();
    uint32_t to_version_insn_id = to_version_insn->result_id();
    uint32_t cst_value_id = cst_value->result_id();
    auto id_mapping = [to_version_insn_id, cst_value_id](uint32_t id) {
      if (to_version_insn_id == id) return cst_value_id;
      return id;
    };

    using InstructionInIdIterator =
        ir::FilterIterator<ir::Instruction::iterator, InIdOperandPredicate>;
    // <Basic Block, next id to visit>
    std::stack<std::pair<ir::BasicBlock*, InstructionInIdIterator>> work_list;

    assert(loop->GetPreHeaderBlock()->terminator()->opcode() == SpvOpBranch);

    {
      visited.insert(loop->GetPreHeaderBlock()->id());
      work_list.emplace(
          std::make_pair(cfg.block(loop->GetPreHeaderBlock()->id()),
                         InstructionInIdIterator(
                             loop->GetPreHeaderBlock()->terminator()->begin(),
                             loop->GetPreHeaderBlock()->terminator()->end())));
    }

    // FIXME: take care of dead insn
    //        take care of phi nodes
    while (work_list.size()) {
      InstructionInIdIterator& next = work_list.top().second;

      // Ignore already visited successors.
      for (; !next.IsEnd(); ++next) {
        // Exit block will be processed after.
        if (!loop->IsInsideLoop(next->words[0])) continue;
        if (!visited.count(next->words[0])) break;
      }

      if (next.IsEnd()) {
        work_list.pop();
        continue;
      }

      uint32_t succ_bb_id = next->words[0];
      // Set the iterator for the next time we process this basic block.
      ++next;

      // Things to do here:
      //   - For each instruction:
      //     - Fold if all become constant;
      //     - Move to the preheader if it becomes LI (can that happen ?);
      //   - If the branch condition depends on the unswitch condition, fold the
      //     next basic block with this one if possible;
      //   - Push this basic block and the successor iterator to the work list.
      ir::BasicBlock* succ_bb = cfg.block(succ_bb_id);
      visited.insert(succ_bb_id);

      ir::BasicBlock::iterator insn_it = succ_bb->begin();
      while (insn_it != succ_bb->end()) {
        bool changed = false;
        insn_it->ForEachInId(
            [to_version_insn, cst_value, &changed](uint32_t* old_id) {
              // If the operand is using the invariant condition, add it to the
              // work list so we can simplify the loop.
              if (*old_id == to_version_insn->result_id()) {
                *old_id = cst_value->result_id();
                changed = true;
              }
            });
        // If we changed the uses, update the manager.
        if (changed) {
          def_use_mgr_->AnalyzeInstDefUse(&*insn_it);
        }
        ir::Instruction* folded = opt::FoldInstruction(&*insn_it, id_mapping);
        if (folded != &*insn_it) {
          def_use_mgr_->ReplaceAllUseOf(insn_it->result_id(),
                                        folded->result_id());
          insn_it = insn_it.Erase();
        } else {
          ++insn_it;
        }
      }

      ir::Instruction* br = succ_bb->terminator();
      uint32_t live_target = kInvalidId;
      if (br->IsBranch()) {
        switch (br->opcode()) {
          case SpvOpBranchConditional: {
            const ir::Instruction* cond =
                def_use_mgr_->GetDef(br->GetOperand(0).words[0]);
            if (!cond) break;
            bool branch_cond = false;
            if (GetConstCondition(cond, &branch_cond)) {
              uint32_t true_label =
                  br->GetSingleWordInOperand(kBranchCondTrueLabIdInIdx);
              uint32_t false_label =
                  br->GetSingleWordInOperand(kBranchCondFalseLabIdInIdx);
              live_target = branch_cond ? true_label : false_label;
              KillBasicBlock(!branch_cond ? true_label : false_label,
                             dead_blocks);
            }
            break;
          }
          case SpvOpSwitch: {
            ir::Instruction::iterator op_it = br->begin();
            const ir::Instruction* condition =
                def_use_mgr_->GetDef(op_it->words[0]);
            if (!condition || !condition->IsConstant()) break;
            //  FIXME: more abstraction required ?
            const ir::Operand& cst = condition->GetOperand(1);
            ++op_it;
            uint32_t default_target = op_it->words[0];
            ++op_it;
            std::vector<uint32_t> possibly_dead;
            while (op_it != br->end()) {
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
            }
            for (uint32_t id : possibly_dead) {
              if (id != live_target)
                KillBasicBlock(default_target, dead_blocks);
            }
          }
          default:
            break;
        }
        if (live_target != kInvalidId) {
          succ_bb->tail().Erase();
          // Check for the presence of the merge block.
          if (succ_bb->tail()->opcode() == SpvOpSelectionMerge)
            succ_bb->tail().Erase();
          opt::InstructionBuilder builder(
              context_, succ_bb,
              ir::IRContext::kAnalysisDefUse |
                  ir::IRContext::kAnalysisInstrToBlockMapping);
          builder.AddBranch(live_target);
        }

        work_list.emplace(std::make_pair(
            succ_bb, InstructionInIdIterator(br->begin(), br->end())));
      } else {
        // No successors, add it to our list of live basic
        // block.
        cfg.RemoveNonExistingEdges(succ_bb->id());
        // Patch phi instructions if needed.
        succ_bb->ForEachPhiInst([dead_blocks, this](ir::Instruction* phi) {
          // New phi operands for this instruction.
          std::vector<uint32_t> phi_op;
          for (uint32_t i = 0; i < phi->NumInOperands(); i += 2) {
            uint32_t def_id = phi->GetSingleWordInOperand(i);
            uint32_t incoming_id = phi->GetSingleWordInOperand(i + 1);
            if (!dead_blocks->count(incoming_id)) {
              phi_op.push_back(def_id);
              phi_op.push_back(incoming_id);
            }
          }
          if (phi->NumInOperands() != phi_op.size()) {
            assert(phi->NumInOperands() != phi_op.size());
            // Rewrite operands.
            uint32_t idx = 0;
            for (; idx < phi_op.size(); idx++)
              phi->SetInOperand(idx, {phi_op[idx]});
            // Remove extra operands, from last to first (more efficient).
            for (uint32_t j = phi->NumInOperands() - 1; j >= idx; j--)
              phi->RemoveInOperand(j);
            // Update the def/use manager for this |phi|.
            def_use_mgr_->AnalyzeInstUse(phi);
          }
        });
      }
    }
  }

  // Clone the current loop and remap instructions. Newly created blocks will be
  // added to the |ordered_loop_bb| list, correctly ordered to be inserted into
  // a function. If the loop is structured, the merge construct will also be
  // cloned.
  // The function preserves the loop analysis.
  ir::Loop* CloneLoop(
      std::list<std::unique_ptr<ir::BasicBlock>>* ordered_loop_bb,
      ValueMapTy* value_map_ptr) {
    ValueMapTy& value_map = *value_map_ptr;
    std::unique_ptr<ir::Loop> new_loop = MakeUnique<ir::Loop>();
    if (loop_->HasParent()) new_loop->SetParent(loop_->GetParent());

    std::function<bool(uint32_t)> ignore_node_and_children;
    if (loop_->GetHeaderBlock()->GetLoopMergeInst()) {
      ignore_node_and_children = [this](uint32_t bb_id) {
        return dom_tree_.Dominates(loop_->GetMergeBlock()->id(), bb_id);
      };
    } else {
      ignore_node_and_children = [this](uint32_t bb_id) {
        return !loop_->IsInsideLoop(bb_id);
      };
    }

    ir::CFG& cfg = *context_->cfg();

    BlockMapTy old_to_new_bb;

    opt::DominatorTreeNode* dtn_root =
        dom_tree_.GetTreeNode(loop_->GetHeaderBlock());
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
      new_bb->GetLabelInst()->SetResultId(TakeNextId());
      def_use_mgr_->AnalyzeInstDef(new_bb->GetLabelInst());
      ordered_loop_bb->emplace_back(new_bb);

      // Keep track of the new basic block, we will need it later on.
      old_to_new_bb[old_bb->id()] = new_bb;
      value_map[old_bb->id()] = new_bb->id();
      new_loop->AddBasicBlock(new_bb);

      for (auto& inst : *new_bb) {
        if (inst.HasResultId()) {
          uint32_t old_result_id = inst.result_id();
          inst.SetResultId(TakeNextId());
          value_map[old_result_id] = inst.result_id();

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
        insn.ForEachInId([&value_map](uint32_t* old_id) {
          // If the operand is defined in the loop, remap the id.
          ValueMapTy::iterator id_it = value_map.find(*old_id);
          if (id_it != value_map.end()) {
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

    // Set the merge block if required.
    if (loop_->GetHeaderBlock()->GetLoopMergeInst()) {
      new_loop->SetMergeBlock(old_to_new_bb[loop_->GetMergeBlock()->id()]);
    }

    std::unordered_map<ir::Loop*, ir::Loop*> loop_mapping;
    loop_mapping[loop_] = new_loop.get();
    for (ir::Loop& sub_loop :
         ir::make_range(++opt::TreeDFIterator<ir::Loop>(loop_),
                        opt::TreeDFIterator<ir::Loop>())) {
      std::unique_ptr<ir::Loop> cloned = MakeUnique<ir::Loop>();
      cloned->SetParent(loop_mapping[sub_loop.GetParent()]);
      cloned->SetHeaderBlock(old_to_new_bb[sub_loop.GetHeaderBlock()->id()]);
      if (sub_loop.GetLatchBlock())
        cloned->SetLatchBlock(old_to_new_bb[sub_loop.GetLatchBlock()->id()]);
      if (sub_loop.GetMergeBlock())
        cloned->SetMergeBlock(old_to_new_bb[sub_loop.GetMergeBlock()->id()]);
      if (sub_loop.GetPreHeaderBlock())
        cloned->SetPreHeaderBlock(
            old_to_new_bb[sub_loop.GetPreHeaderBlock()->id()]);
      // Populate only the leaves, when we add a block to a loop with a
      // registered parents they will get automatically notified.
      if (sub_loop.begin() == sub_loop.end()) {
        for (uint32_t bb_id : sub_loop.GetBlocks()) {
          sub_loop.AddBasicBlock(old_to_new_bb[bb_id]);
        }
      }
    }

    return loop_desc_.AddLoops(std::move(new_loop));
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
