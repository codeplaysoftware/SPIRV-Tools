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

#include "opt/loop_descriptor.h"
#include <iostream>
#include <type_traits>
#include <utility>
#include <vector>

#include "opt/cfg.h"
#include "opt/ir_builder.h"
#include "opt/iterator.h"
#include "opt/loop_descriptor.h"
#include "opt/make_unique.h"
#include "opt/tree_iterator.h"

namespace spvtools {
namespace ir {

Loop::Loop(IRContext* context, opt::DominatorAnalysis* dom_analysis,
           BasicBlock* header, BasicBlock* continue_target,
           BasicBlock* merge_target)
    : loop_header_(header),
      loop_continue_(continue_target),
      loop_merge_(merge_target),
      loop_preheader_(nullptr),
      parent_(nullptr) {
  assert(context);
  assert(dom_analysis);
  loop_preheader_ = FindLoopPreheader(context, dom_analysis);
  AddBasicBlockToLoop(header);
  AddBasicBlockToLoop(continue_target);
}

BasicBlock* Loop::FindLoopPreheader(IRContext* ir_context,
                                    opt::DominatorAnalysis* dom_analysis) {
  CFG* cfg = ir_context->cfg();
  opt::DominatorTree& dom_tree = dom_analysis->GetDomTree();
  opt::DominatorTreeNode* header_node = dom_tree.GetTreeNode(loop_header_);

  // The loop predecessor.
  BasicBlock* loop_pred = nullptr;

  auto header_pred = cfg->preds(loop_header_->id());
  for (uint32_t p_id : header_pred) {
    opt::DominatorTreeNode* node = dom_tree.GetTreeNode(p_id);
    if (node && !dom_tree.Dominates(header_node, node)) {
      // The predecessor is not part of the loop, so potential loop preheader.
      if (loop_pred && node->bb_ != loop_pred) {
        // If we saw 2 distinct predecessors that are outside the loop, we don't
        // have a loop preheader.
        return nullptr;
      }
      loop_pred = node->bb_;
    }
  }
  // Safe guard against invalid code, SPIR-V spec forbids loop with the entry
  // node as header.
  assert(loop_pred && "The header node is the entry block ?");

  // So we have a unique basic block that can enter this loop.
  // If this loop is the unique successor of this block, then it is a loop
  // preheader.
  bool is_preheader = true;
  uint32_t loop_header_id = loop_header_->id();
  loop_pred->ForEachSuccessorLabel(
      [&is_preheader, loop_header_id](const uint32_t id) {
        if (id != loop_header_id) is_preheader = false;
      });
  if (is_preheader) return loop_pred;
  return nullptr;
}

static inline bool DominatesAnExit(
    ir::BasicBlock* bb, const std::unordered_set<ir::BasicBlock*>& exits,
    const opt::DominatorTree& dom_tree) {
  for (ir::BasicBlock* e_bb : exits)
    if (dom_tree.Dominates(bb, e_bb)) return true;
  return false;
}

void LoopUtils::CreateLoopDedicateExits() {
  ir::Function* function = loop_->GetHeaderBlock()->GetParent();
  ir::CFG& cfg = *context_->cfg();

  // Gather the set of basic block that are not in this loop and have at least
  // one predecessor in the loop and one not in the loop.
  std::unordered_set<ir::BasicBlock*> exit_bb_set;
  for (uint32_t bb_id : loop_->GetBlocks()) {
    ir::BasicBlock* bb = cfg.block(bb_id);
    bb->ForEachSuccessorLabel([&exit_bb_set, &cfg, this](uint32_t succ) {
      if (!loop_->IsInsideLoop(succ)) {
        ir::BasicBlock* exit = cfg.block(succ);
        for (uint32_t pred : cfg.preds(exit->id()))
          if (!loop_->IsInsideLoop(pred)) {
            exit_bb_set.insert(exit);
            return;
          }
      }
    });
  }

  // For each block, we create a new one that gather all branches from
  // the loop and fall into the block.
  for (ir::BasicBlock* non_dedicate : exit_bb_set) {
    // Create the dedicate exit basic block.
    function->AddBasicBlock(std::unique_ptr<ir::BasicBlock>(
        new ir::BasicBlock(std::unique_ptr<ir::Instruction>(new ir::Instruction(
            context_, SpvOpLabel, 0, context_->TakeNextUniqueId(), {})))));
    ir::BasicBlock& exit = *(--function->end());

    // Patch the phi nodes.
    opt::InstructionBuilder<> builder(context_, exit.begin());
    non_dedicate->ForEachPhiInst([&builder, &exit, this](Instruction* phi) {
      // New phi operands for this instruction.
      std::vector<uint32_t> new_phi_op;
      // Phi operands for the dedicated exit block.
      std::vector<uint32_t> exit_phi_op;
      for (uint32_t i = 0; i < phi->NumInOperands(); i += 2) {
        uint32_t def_id = phi->GetSingleWordInOperand(i);
        uint32_t incoming_id = phi->GetSingleWordInOperand(i + 1);
        if (loop_->IsInsideLoop(incoming_id)) {
          exit_phi_op.push_back(def_id);
          exit_phi_op.push_back(incoming_id);
        } else {
          new_phi_op.push_back(def_id);
          new_phi_op.push_back(incoming_id);
        }
      }

      // Build the new phi instruction dedicated exit block.
      Instruction* exit_phi = builder.AddPhi(phi->type_id(), exit_phi_op);
      // Build the new incoming branch.
      new_phi_op.push_back(exit_phi->result_id());
      new_phi_op.push_back(exit.id());
      // Rewrite operands.
      uint32_t idx = 0;
      for (; idx < new_phi_op.size(); idx++)
        phi->SetInOperand(idx, {new_phi_op[idx]});
      // Remove extra operands, from last to first (more efficient).
      for (uint32_t j = phi->NumInOperands() - 1; j >= idx; j--)
        phi->RemoveInOperand(j);
    });
    // now jump from our dedicate basic block to the old exit.
    builder.AddBranch(non_dedicate->id());
  }

  // If we modified something, we need to rebuild the analysis.
  // FIXME: This could be done on the fly.
  if (!exit_bb_set.empty()) {
    context_->InvalidateAnalysesExceptFor(
        ir::IRContext::Analysis::kAnalysisNone);
  }
}

// Utility class to rewrite uses in terms of phi nodes to achieve a LCSSA form.
// It works by inserting SSA nodes where needed to make an out-of-loop use of a
// def dependent on the exiting phi node.
class LCSSARewriter {
 public:
  LCSSARewriter(ir::IRContext* context, const ir::Instruction& def_insn)
      : context_(context),
        cfg_(context_->cfg()),
        def_insn_(def_insn),
        insn_type_(def_insn.type_id()) {}

  // Rewrites the use of |def_insn_| by the instruction |user| at the index
  // |operand_index| in terms of phi instruction.
  // This recursively builds new phi instructions from |user| to the loop exit
  // blocks' phis.
  // The use of |def_insn_| in |user| is replaced by the relevant phi
  // instruction at the end of the operation.
  // This operation does not update the def/use manager, instead it records what
  // needs to be updated. The actual update is performed by UpdateManagers.
  void RewriteUse(ir::BasicBlock* bb, ir::Instruction* user,
                  uint32_t operand_index) {
    assert((user->opcode() != SpvOpPhi || bb != GetParent(user)) &&
           "The root basic block must be the incoming edge if |user| is a phi "
           "instruction");
    assert(
        (user->opcode() == SpvOpPhi || bb == GetParent(user)) &&
        "The root basic block must be the instruction parent if |user| is not "
        "phi instruction");

    const ir::Instruction& new_def = GetOrBuildIncoming(bb->id());

    user->SetOperand(operand_index, {new_def.result_id()});
    rewrited.insert(user);
  }

  // Notifies the addition of a phi node built to close the loop.
  // FIXME: we should be able to register the phi for its bb.
  inline void RegisterPhi(ir::BasicBlock* bb, ir::Instruction* phi) {
    bb_to_phi[bb->id()] = phi;
    rewrited.insert(phi);
  }

  // In-place update of some managers (avoid full invalidation).
  inline void UpdateManagers() {
    opt::analysis::DefUseManager* def_use_mgr = context_->get_def_use_mgr();
    // Register all new definitions.
    for (ir::Instruction* insn : rewrited) {
      def_use_mgr->AnalyzeInstDef(insn);
    }
    // Register all new uses.
    for (ir::Instruction* insn : rewrited) {
      def_use_mgr->AnalyzeInstUse(insn);
    }
  }

 private:
  // Return the basic block that |instr| belongs to.
  ir::BasicBlock* GetParent(ir::Instruction* instr) {
    return context_->get_instr_block(instr);
  }

  // Return the new def to use for the basic block |bb_id|.
  // If |bb_id| does not have a suitable def to use then we:
  //   - return the common def used by all predecessors;
  //   - if there is no common def, then we build a new phi instr at the
  //     beginning of |bb_id| and return this new instruction.
  const ir::Instruction& GetOrBuildIncoming(uint32_t bb_id) {
    assert(cfg_->block(bb_id) != nullptr && "Unknown basic block");

    ir::Instruction*& incoming_phi = bb_to_phi[bb_id];
    if (incoming_phi) {
      return *incoming_phi;
    }

    // FIXME: test with loop... (handles back edge).

    // Process parents, they will returns their suitable phi.
    // If they are all the same, this means this basic block is dominated by a
    // common block, so we won't need to build a phi instruction.
    std::vector<uint32_t> incomings;
    for (uint32_t pred_id : cfg_->preds(bb_id)) {
      incomings.push_back(GetOrBuildIncoming(pred_id).result_id());
      incomings.push_back(pred_id);
    }
    uint32_t first_id = incomings.front();
    size_t idx = 0;
    for (; idx < incomings.size(); idx += 2)
      if (first_id != incomings[idx]) break;

    if (idx >= incomings.size()) {
      incoming_phi = bb_to_phi[incomings[1]];
      return *incoming_phi;
    }

    // We have at least 2 definitions to merge, so we need a phi instruction.
    ir::BasicBlock* block = cfg_->block(bb_id);

    opt::InstructionBuilder<> builder(context_, block->begin());
    incoming_phi = builder.AddPhi(insn_type_, incomings);

    rewrited.insert(incoming_phi);

    return *incoming_phi;
  }

  ir::IRContext* context_;
  ir::CFG* cfg_;
  const ir::Instruction& def_insn_;
  uint32_t insn_type_;
  std::unordered_map<uint32_t, ir::Instruction*> bb_to_phi;
  std::unordered_set<ir::Instruction*> rewrited;
};

void LoopUtils::MakeLoopClosedSSA() {
  CreateLoopDedicateExits();

  ir::Function* function = loop_->GetHeaderBlock()->GetParent();
  ir::CFG& cfg = *context_->cfg();
  opt::DominatorTree& dom_tree =
      context_->GetDominatorAnalysis(function, cfg)->GetDomTree();

  opt::analysis::DefUseManager* def_use_manager = context_->get_def_use_mgr();
  std::unordered_set<ir::BasicBlock*> exit_bb;
  for (uint32_t bb_id : loop_->GetBlocks()) {
    ir::BasicBlock* bb = cfg.block(bb_id);
    bb->ForEachSuccessorLabel([&exit_bb, &cfg, this](uint32_t succ) {
      if (!loop_->IsInsideLoop(succ)) exit_bb.insert(cfg.block(succ));
    });
  }

  for (uint32_t bb_id : loop_->GetBlocks()) {
    std::unordered_set<ir::BasicBlock*> processed_exit;
    ir::BasicBlock* bb = cfg.block(bb_id);
    // If bb does not dominate an exit block, then it cannot have escaping defs.
    if (!DominatesAnExit(bb, exit_bb, dom_tree)) continue;
    for (ir::Instruction& inst : *bb) {
      LCSSARewriter rewriter(context_, inst);
      def_use_manager->ForEachUse(
          &inst, [&rewriter, &exit_bb, &processed_exit, &inst, &dom_tree, &cfg,
                  this](ir::Instruction* use, uint32_t operand_index) {
            if (loop_->IsInsideLoop(use)) return;

            ir::BasicBlock* use_parent = context_->get_instr_block(use);
            assert(use_parent);
            // If the use is a Phi instruction and the incoming block is coming
            // from the loop, then that's consistent with LCSSA form.
            if (use->opcode() == SpvOpPhi && exit_bb.count(use_parent)) {
              return;
            }

            for (ir::BasicBlock* e_bb : exit_bb) {
              if (processed_exit.count(e_bb)) continue;
              processed_exit.insert(e_bb);

              // If the current exit basic block does not dominate |use| then
              // |inst| does not escape through |e_bb|.
              if (!dom_tree.Dominates(e_bb, use_parent)) continue;

              opt::InstructionBuilder<> builder(context_, e_bb->begin());
              const std::vector<uint32_t>& preds = cfg.preds(e_bb->id());
              std::vector<uint32_t> incoming;
              incoming.reserve(preds.size() * 2);
              for (uint32_t pred_id : preds) {
                incoming.push_back(inst.result_id());
                incoming.push_back(pred_id);
              }
              rewriter.RegisterPhi(e_bb,
                                   builder.AddPhi(inst.type_id(), incoming));
            }

            if (use->opcode() == SpvOpPhi) {
              use_parent = context_->get_instr_block(
                  use->GetSingleWordOperand(operand_index + 1));
            }
            // Rewrite the use. Note that this call does not invalidate the
            // def/use manager. So this operation is safe.
            rewriter.RewriteUse(use_parent, use, operand_index);
          });
      rewriter.UpdateManagers();
    }
  }

  context_->InvalidateAnalysesExceptFor(
      ir::IRContext::Analysis::kAnalysisDefUse |
      ir::IRContext::Analysis::kAnalysisCFG |
      ir::IRContext::Analysis::kAnalysisDominatorAnalysis);
}

LoopDescriptor::LoopDescriptor(const Function* f) { PopulateList(f); }

void LoopDescriptor::PopulateList(const Function* f) {
  IRContext* context = f->GetParent()->context();

  opt::DominatorAnalysis* dom_analysis =
      context->GetDominatorAnalysis(f, *context->cfg());

  loops_.clear();

  // Post-order traversal of the dominator tree to find all the OpLoopMerge
  // instructions.
  opt::DominatorTree& dom_tree = dom_analysis->GetDomTree();
  for (opt::DominatorTreeNode& node :
       ir::make_range(dom_tree.post_begin(), dom_tree.post_end())) {
    Instruction* merge_inst = node.bb_->GetLoopMergeInst();
    if (merge_inst) {
      // The id of the merge basic block of this loop.
      uint32_t merge_bb_id = merge_inst->GetSingleWordOperand(0);

      // The id of the continue basic block of this loop.
      uint32_t continue_bb_id = merge_inst->GetSingleWordOperand(1);

      // The merge target of this loop.
      BasicBlock* merge_bb = context->cfg()->block(merge_bb_id);

      // The continue target of this loop.
      BasicBlock* continue_bb = context->cfg()->block(continue_bb_id);

      // The basic block containing the merge instruction.
      BasicBlock* header_bb = context->get_instr_block(merge_inst);

      // Add the loop to the list of all the loops in the function.
      loops_.emplace_back(MakeUnique<Loop>(context, dom_analysis, header_bb,
                                           continue_bb, merge_bb));
      Loop* current_loop = loops_.back().get();

      // We have a bottom-up construction, so if this loop has nested-loops,
      // they are by construction at the tail of the loop list.
      for (auto itr = loops_.rbegin() + 1; itr != loops_.rend(); ++itr) {
        Loop* previous_loop = itr->get();

        // If the loop already has a parent, then it has been processed.
        if (previous_loop->HasParent()) continue;

        // If the current loop does not dominates the previous loop then it is
        // not nested loop.
        if (!dom_analysis->Dominates(header_bb,
                                     previous_loop->GetHeaderBlock()))
          continue;
        // If the current loop merge dominates the previous loop then it is
        // not nested loop.
        if (dom_analysis->Dominates(merge_bb, previous_loop->GetHeaderBlock()))
          continue;

        current_loop->AddNestedLoop(previous_loop);
      }
      opt::DominatorTreeNode* dom_merge_node = dom_tree.GetTreeNode(merge_bb);
      for (opt::DominatorTreeNode& loop_node :
           make_range(node.df_begin(), node.df_end())) {
        // Check if we are in the loop.
        if (dom_tree.Dominates(dom_merge_node, &loop_node)) continue;
        current_loop->AddBasicBlockToLoop(loop_node.bb_);
        basic_block_to_loop_.insert(
            std::make_pair(loop_node.bb_->id(), current_loop));
      }
    }
  }
  for (std::unique_ptr<Loop>& loop : loops_) {
    if (!loop->HasParent()) dummy_top_loop_.nested_loops_.push_back(loop.get());
  }
}

}  // namespace ir
}  // namespace spvtools
