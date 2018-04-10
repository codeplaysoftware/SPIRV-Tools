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

#include "register_pressure.h"

#include <iterator>

#include "cfg.h"
#include "def_use_manager.h"
#include "function.h"
#include "ir_context.h"
#include "iterator.h"

namespace spvtools {
namespace opt {

namespace {
class ExcludePhiDefinedInBlock {
 public:
  ExcludePhiDefinedInBlock(ir::IRContext* context, const ir::BasicBlock* bb)
      : context_(context), bb_(bb) {}

  bool operator()(ir::Instruction* insn) const {
    return insn->HasResultId() && !(insn->opcode() == SpvOpPhi &&
                                    context_->get_instr_block(insn) == bb_);
  }

 private:
  ir::IRContext* context_;
  const ir::BasicBlock* bb_;
};

class ComputeRegisterLiveness {
 public:
  ComputeRegisterLiveness(RegisterLiveness* reg_pressure, ir::Function* f)
      : reg_pressure_(reg_pressure),
        context_(reg_pressure->GetContext()),
        function_(f),
        cfg_(*reg_pressure->GetContext()->cfg()),
        def_use_manager_(*reg_pressure->GetContext()->get_def_use_mgr()),
        dom_tree_(
            reg_pressure->GetContext()->GetDominatorAnalysis(f)->GetDomTree()),
        loop_desc_(*reg_pressure->GetContext()->GetLoopDescriptor(f)) {}

  void Compute() {
    cfg_.ForEachBlockInPostOrder(
        &*function_->begin(),
        [this](ir::BasicBlock* bb) { ComputePartialLiveness(bb); });
    DoLoopLivenessUnification();
    EvaluateRegisterRequirements();
  }

  void operator()(ir::BasicBlock* bb);

 private:
  RegisterLiveness* reg_pressure_;
  ir::IRContext* context_;
  ir::Function* function_;
  ir::CFG& cfg_;
  opt::analysis::DefUseManager& def_use_manager_;
  opt::DominatorTree& dom_tree_;
  ir::LoopDescriptor& loop_desc_;

  // Wrapper for CreatesRegisterUsage(ir::Instruction*).
  bool CreatesRegisterUsage(uint32_t id) const {
    return CreatesRegisterUsage(def_use_manager_.GetDef(id));
  }

  // Returns true if |insn| requires a register.
  bool CreatesRegisterUsage(ir::Instruction* insn) const {
    if (!insn->HasResultId()) return false;
    if (insn->opcode() == SpvOpUndef) return false;
    if (ir::IsConstantInst(insn->opcode())) return false;
    if (insn->opcode() == SpvOpLabel) return false;
    return true;
  }

  void ComputePhiUses(const ir::BasicBlock& bb,
                      RegisterLiveness::RegionRegisterLiveness::LiveSet* live) {
    uint32_t bb_id = bb.id();
    bb.ForEachSuccessorLabel([live, bb_id, this](uint32_t sid) {
      ir::BasicBlock* succ_bb = cfg_.block(sid);
      succ_bb->ForEachPhiInst([live, bb_id, this](const ir::Instruction* phi) {
        for (uint32_t i = 0; i < phi->NumInOperands(); i += 2) {
          if (phi->GetSingleWordInOperand(i + 1) == bb_id) {
            ir::Instruction* insn_op =
                def_use_manager_.GetDef(phi->GetSingleWordInOperand(i));
            if (CreatesRegisterUsage(insn_op)) {
              live->insert(insn_op);
              break;
            }
          }
        }
      });
    });
  }

  void ComputePartialLiveness(ir::BasicBlock* bb) {
    assert(reg_pressure_->Get(bb) == nullptr &&
           "Basic block already processed");

    RegisterLiveness::RegionRegisterLiveness* live_inout =
        reg_pressure_->GetOrInsert(bb->id());
    ComputePhiUses(*bb, &live_inout->live_out_);

    const ir::BasicBlock* cbb = bb;
    cbb->ForEachSuccessorLabel([&live_inout, bb, this](uint32_t sid) {
      // Skip back edges.
      if (dom_tree_.Dominates(sid, bb->id())) {
        return;
      }

      ir::BasicBlock* succ_bb = cfg_.block(sid);
      RegisterLiveness::RegionRegisterLiveness* succ_live_inout =
          reg_pressure_->Get(succ_bb);
      assert(succ_live_inout &&
             "Successor liveness analysis was not performed");

      ExcludePhiDefinedInBlock predicate(context_, succ_bb);
      auto filter = ir::MakeFilterIteratorRange(
          succ_live_inout->live_in_.begin(), succ_live_inout->live_in_.end(),
          predicate);
      live_inout->live_out_.insert(filter.begin(), filter.end());
    });

    live_inout->live_in_ = live_inout->live_out_;
    for (ir::Instruction& insn : ir::make_range(bb->rbegin(), bb->rend())) {
      if (insn.opcode() == SpvOpPhi) {
        live_inout->live_in_.insert(&insn);
        break;
      }
      live_inout->live_in_.erase(&insn);
      insn.ForEachInId([live_inout, this](uint32_t* id) {
        ir::Instruction* insn_op = def_use_manager_.GetDef(*id);
        if (CreatesRegisterUsage(insn_op)) {
          live_inout->live_in_.insert(insn_op);
        }
      });
    }
  }

  void DoLoopLivenessUnification() {
    for (const ir::Loop* loop : *loop_desc_.GetDummyRootLoop()) {
      DoLoopLivenessUnification(*loop);
    }
  }

  void DoLoopLivenessUnification(const ir::Loop& loop) {
    auto blocks_in_loop = ir::MakeFilterIteratorRange(
        loop.GetBlocks().begin(), loop.GetBlocks().end(),
        [&loop, this](uint32_t bb_id) {
          return bb_id != loop.GetHeaderBlock()->id() &&
                 loop_desc_[bb_id] == &loop;
        });

    RegisterLiveness::RegionRegisterLiveness* header_live_inout =
        reg_pressure_->Get(loop.GetHeaderBlock());
    assert(header_live_inout &&
           "Liveness analysis was not performed for the current block");

    ExcludePhiDefinedInBlock predicate(context_, loop.GetHeaderBlock());
    auto live_loop = ir::MakeFilterIteratorRange(
        header_live_inout->live_in_.begin(), header_live_inout->live_in_.end(),
        predicate);

    for (uint32_t bb_id : blocks_in_loop) {
      ir::BasicBlock* bb = cfg_.block(bb_id);

      RegisterLiveness::RegionRegisterLiveness* live_inout =
          reg_pressure_->Get(bb);
      live_inout->live_in_.insert(live_loop.begin(), live_loop.end());
      live_inout->live_out_.insert(live_loop.begin(), live_loop.end());
    }

    for (const ir::Loop* inner_loop : loop) {
      RegisterLiveness::RegionRegisterLiveness* live_inout =
          reg_pressure_->Get(inner_loop->GetHeaderBlock());
      live_inout->live_in_.insert(live_loop.begin(), live_loop.end());
      live_inout->live_out_.insert(live_loop.begin(), live_loop.end());

      DoLoopLivenessUnification(*inner_loop);
    }
  }

  // Get the number of required registers for this each basic block.
  RegisterLiveness::RegisterClass ClassifyRegister(ir::Instruction* insn) {
    assert(insn->HasResultId() && "Instruction does not use a register");
    analysis::Type* type = context_->get_type_mgr()->GetType(insn->type_id());

    RegisterLiveness::RegisterClass reg_class{type, false};

    context_->get_decoration_mgr()->WhileEachDecoration(
        insn->result_id(), SpvDecorationUniform,
        [&reg_class](const ir::Instruction&) {
          reg_class.is_uniform_ = true;
          return false;
        });
    return reg_class;
  }

  // Get the number of required registers for this each basic block.
  void EvaluateRegisterRequirements() {
    for (ir::BasicBlock& bb : *function_) {
      RegisterLiveness::RegionRegisterLiveness* live_inout =
          reg_pressure_->Get(bb.id());
      assert(live_inout != nullptr && "Basic block not processed");

      size_t reg_count = live_inout->live_out_.size();
      for (ir::Instruction* insn : live_inout->live_out_) {
        RegisterLiveness::RegisterClass reg_class = ClassifyRegister(insn);
        live_inout->AddRegisterClass(reg_class);
      }
      live_inout->used_registers_ = reg_count;

      std::unordered_set<uint32_t> die_in_block;
      for (ir::Instruction& insn : ir::make_range(bb.rbegin(), bb.rend())) {
        if (insn.opcode() == SpvOpPhi) {
          break;
        }
        if (!CreatesRegisterUsage(&insn)) {
          continue;
        }

        insn.ForEachInId(
            [live_inout, &die_in_block, &reg_count, this](uint32_t* id) {
              ir::Instruction* op_insn = def_use_manager_.GetDef(*id);
              if (live_inout->live_out_.count(op_insn)) {
                // already taken into account.
                return;
              }
              if (!die_in_block.count(*id)) {
                RegisterLiveness::RegisterClass reg_class =
                    ClassifyRegister(def_use_manager_.GetDef(*id));
                live_inout->AddRegisterClass(reg_class);
                reg_count++;
                die_in_block.insert(*id);
              }
            });
        if (insn.HasResultId() && die_in_block.count(insn.result_id())) {
          reg_count--;
        }
        live_inout->used_registers_ =
            std::max(live_inout->used_registers_, reg_count);
      }
    }
  }
};
}  // namespace

void RegisterLiveness::Analyze(ir::Function* f) {
  block_pressure_.clear();
  ComputeRegisterLiveness(this, f).Compute();
}

void RegisterLiveness::ComputeLoopRegisterPressure(
    const ir::Loop& loop, RegionRegisterLiveness* reg_pressure) const {
  reg_pressure->live_out_.clear();
  reg_pressure->registers_classes_.clear();

  const RegionRegisterLiveness* header_live_inout = Get(loop.GetHeaderBlock());
  reg_pressure->live_in_ = header_live_inout->live_in_;

  std::unordered_set<uint32_t> exit_blocks;
  loop.GetExitBlocks(&exit_blocks);

  for (uint32_t bb_id : exit_blocks) {
    const RegionRegisterLiveness* live_inout = Get(bb_id);
    reg_pressure->live_out_.insert(live_inout->live_in_.begin(),
                                   live_inout->live_in_.end());
  }
}

void RegisterLiveness::SimluateFusion(
    const ir::Loop&, const ir::Loop& l2,
    RegionRegisterLiveness* simulation_resut) const {
  ComputeLoopRegisterPressure(l2, simulation_resut);
}

void RegisterLiveness::SimluateFission(const ir::Loop&,
                                       const std::unordered_set<ir::Instruction*>&,
                                       const std::unordered_set<ir::Instruction*>&,
                                       RegionRegisterLiveness*,
                                       RegionRegisterLiveness*) const {}

}  // namespace opt
}  // namespace spvtools
