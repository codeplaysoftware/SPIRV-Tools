
#include "opt/loop_fission.h"
#include "opt/register_pressure.h"

namespace spvtools {
namespace opt {

class LoopFissionImpl {
 public:
  LoopFissionImpl(ir::IRContext* context, ir::Loop* loop)
      : context_(context), loop_(loop), load_used_in_condition_(false) {}

  bool BuildRelatedSets();

  bool CanPerformSplit();

  ir::Loop* SplitLoop();

  // Checks if |inst| is safe to move. We can only move instructions which don't
  // have any side effects and OpLoads and OpStores.
  bool MovableInstruction(const ir::Instruction& inst) const;

 private:
  // Traverse the def use chain of |inst| and add the users and uses of |inst|
  // which are in the same loop to the |returned_set|.
  void TraverseUseDef(ir::Instruction* inst,
                      std::set<ir::Instruction*>* returned_set,
                      bool ignore_phi_users = false, bool report_loads = false);

  // We group the instructions in the block into two different groups, the
  // instructions to be kept in the original loop and the ones to be cloned into
  // the new loop. As the cloned loop is attached to the preheader it will be
  // the first loop and the second loop will be the original.
  std::set<ir::Instruction*> first_loop_instructions_;
  std::set<ir::Instruction*> second_loop_instructions_;

  // We need a set of all the instructions to be seen so we can break any
  // recursion and also so we can ignore certain instructions by preemptively
  // adding them to this set.
  std::set<ir::Instruction*> seen_instructions_;

  // A map of instructions to their relative position in the function.
  std::map<ir::Instruction*, size_t> instruction_order_;

  ir::IRContext* context_;

  ir::Loop* loop_;

  // This is set to true by TraverseUseDef when traversing the instructions
  // related to the loop condition and any if conditions should any of those
  // instructions be a load.
  bool load_used_in_condition_;
};

bool LoopFissionImpl::MovableInstruction(const ir::Instruction& inst) const {
  return inst.opcode() == SpvOp::SpvOpLoad ||
         inst.opcode() == SpvOp::SpvOpStore ||
         inst.opcode() == SpvOp::SpvOpSelectionMerge ||
         inst.opcode() == SpvOp::SpvOpPhi || inst.IsOpcodeCodeMotionSafe();
}

void LoopFissionImpl::TraverseUseDef(ir::Instruction* inst,
                                     std::set<ir::Instruction*>* returned_set,
                                     bool ignore_phi_users, bool report_loads) {
  assert(returned_set && "Set to be returned cannot be null.");

  opt::analysis::DefUseManager* def_use = context_->get_def_use_mgr();
  std::set<ir::Instruction*>& inst_set = *returned_set;

  // We create this functor to traverse the use def chain to build the
  // grouping of related instructions. The lambda captures the std::function
  // to allow it to recurse.
  std::function<void(ir::Instruction*)> traverser_functor;
  traverser_functor = [this, def_use, &inst_set, &traverser_functor,
                       ignore_phi_users, report_loads](ir::Instruction* user) {
    if (!user || seen_instructions_.count(user) != 0 ||
        !context_->get_instr_block(user) ||
        !loop_->IsInsideLoop(context_->get_instr_block(user))) {
      return;
    }

    if (user->opcode() == SpvOp::SpvOpLoopMerge ||
        user->opcode() == SpvOp::SpvOpLabel)
      return;

    // If the |report_loads| flag is set, set the class field
    // load_used_in_condition_ to false. This is used to check that none of the
    // condition checks in the loop rely on loads.
    if (user->opcode() == SpvOp::SpvOpLoad && report_loads) {
      load_used_in_condition_ = true;
    }

    seen_instructions_.insert(user);
    if (!user->IsBranch()) {
      inst_set.insert(user);
    }

    auto traverse_operand = [&traverser_functor, def_use](const uint32_t* id) {
      traverser_functor(def_use->GetDef(*id));
    };

    user->ForEachInOperand(traverse_operand);

    // For the first traversal we want to ignore the users of the phi.
    if (ignore_phi_users && user->opcode() == SpvOp::SpvOpPhi) return;

    def_use->ForEachUser(user, traverser_functor);

    auto traverse_use = [&traverser_functor](ir::Instruction* use, uint32_t) {
      traverser_functor(use);
    };

    def_use->ForEachUse(user, traverse_use);

  };

  // We start the traversal of the use def graph by invoking the above
  // lambda with the loop instruction which has not already been found in a
  // traversal.
  traverser_functor(inst);
}

bool LoopFissionImpl::BuildRelatedSets() {
  std::vector<std::set<ir::Instruction*>> sets{};

  // We want to ignore all the instructions stemming from the loop condition
  // instruction.
  ir::BasicBlock* condition_block = loop_->FindConditionBlock();
  ir::Instruction* condition = &*condition_block->tail();

  std::set<ir::Instruction*> tmp_set{};
  TraverseUseDef(condition, &tmp_set, true, true);

  for (uint32_t block_id : loop_->GetBlocks()) {
    ir::BasicBlock* block = context_->cfg()->block(block_id);

    for (ir::Instruction& inst : *block) {
      // Ignore all instructions related to control flow.
      if (inst.opcode() == SpvOp::SpvOpSelectionMerge ||
          inst.opcode() == SpvOp::SpvOpBranchConditional) {
        TraverseUseDef(&inst, &tmp_set, true, true);
        continue;
      }
    }
  }

  for (uint32_t block_id : loop_->GetBlocks()) {
    ir::BasicBlock* block = context_->cfg()->block(block_id);

    for (ir::Instruction& inst : *block) {
      if (inst.opcode() == SpvOp::SpvOpLoad ||
          inst.opcode() == SpvOp::SpvOpStore) {
        instruction_order_[&inst] = instruction_order_.size();
      }

      if (seen_instructions_.count(&inst) != 0) {
        continue;
      }

      std::set<ir::Instruction*> inst_set{};
      TraverseUseDef(&inst, &inst_set);

      if (!inst_set.empty()) sets.push_back(std::move(inst_set));
    }
  }

  // If we have one or zero sets return false to indicate that due to
  // insufficient instructions we couldn't split the loop into two groups and
  // thus the loop can't be split any further.
  if (sets.size() < 2) {
    return false;
  }

  for (size_t index = 0; index < sets.size() / 2; ++index) {
    first_loop_instructions_.insert(sets[index].begin(), sets[index].end());
  }
  for (size_t index = sets.size() / 2; index < sets.size(); ++index) {
    second_loop_instructions_.insert(sets[index].begin(), sets[index].end());
  }

  return true;
}

bool LoopFissionImpl::CanPerformSplit() {
  // Return false if any of the condition instructions in the loop depend on a
  // load.
  if (load_used_in_condition_) {
    return false;
  }

  std::vector<const ir::Loop*> loops;
  ir::Loop* parent_loop = loop_;

  while (parent_loop) {
    loops.push_back(parent_loop);
    parent_loop = parent_loop->GetParent();
  }

  LoopDependenceAnalysis analysis{context_, loops};

  std::vector<ir::Instruction*> set_one_stores{};
  std::vector<ir::Instruction*> set_one_loads{};

  for (ir::Instruction* inst : first_loop_instructions_) {
    if (inst->opcode() == SpvOp::SpvOpStore) {
      set_one_stores.push_back(inst);
    } else if (inst->opcode() == SpvOp::SpvOpLoad) {
      set_one_loads.push_back(inst);
    }

    if (!MovableInstruction(*inst)) return false;
  }

  const size_t loop_depth = loop_->GetDepth();

  for (ir::Instruction* inst : second_loop_instructions_) {
    if (!MovableInstruction(*inst)) return false;

    // Look at the dependency between the.
    if (inst->opcode() == SpvOp::SpvOpLoad) {
      for (ir::Instruction* store : set_one_stores) {
        DistanceVector vec{loop_depth};

        // If the store actually should appear after the load, return false.
        // This means the store has been placed in the wrong grouping.
        if (instruction_order_[store] > instruction_order_[inst]) {
          return false;
        }
        // If not independent check the distance vector.
        if (!analysis.GetDependence(store, inst, &vec)) {
          for (DistanceEntry& entry : vec.GetEntries()) {
            // A distance greater than zero means that the store in the first
            // loop has a dependency on the load in the second loop.
            if (entry.distance > 0) return false;
          }
        }
      }
    } else if (inst->opcode() == SpvOp::SpvOpStore) {
      for (ir::Instruction* load : set_one_loads) {
        DistanceVector vec{loop_depth};

        // If the load actually should appear after the store, return false.
        if (instruction_order_[load] > instruction_order_[inst]) {
          return false;
        }

        // If not independent check the distance vector.
        if (!analysis.GetDependence(inst, load, &vec)) {
          for (DistanceEntry& entry : vec.GetEntries()) {
            // A distance less than zero means the load in the first loop is
            // dependent on the store instruction in the second loop.
            if (entry.distance < 0) return false;
          }
        }
      }
    }
  }
  return true;
}

ir::Loop* LoopFissionImpl::SplitLoop() {
  // Clone the loop.
  LoopUtils util{context_, loop_};
  LoopUtils::LoopCloningResult clone_results;
  ir::Loop* second_loop = util.CloneAndAttachLoopToHeader(&clone_results);

  // Update the OpLoopMerge in the cloned loop.
  second_loop->UpdateLoopMergeInst();

  // Add the loop_ to the module.
  ir::Function::iterator it =
      util.GetFunction()->FindBlock(loop_->GetOrCreatePreHeaderBlock()->id());
  util.GetFunction()->AddBasicBlocks(clone_results.cloned_bb_.begin(),
                                     clone_results.cloned_bb_.end(), ++it);
  loop_->SetPreHeaderBlock(second_loop->GetMergeBlock());
  //  second_loop->GetOrCreatePreHeaderBlock();

  std::vector<ir::Instruction*> instructions_to_kill{};
  for (uint32_t id : loop_->GetBlocks()) {
    ir::BasicBlock* block = context_->cfg()->block(id);

    for (ir::Instruction& inst : *block) {
      // If the instruction belongs to the second instruction group, kill
      // it.
      if (first_loop_instructions_.count(&inst) == 1 &&
          second_loop_instructions_.count(&inst) == 0) {
        instructions_to_kill.push_back(&inst);
        if (inst.opcode() == SpvOp::SpvOpPhi) {
          context_->ReplaceAllUsesWith(
              inst.result_id(), clone_results.value_map_[inst.result_id()]);
        }
      }
    }
  }

  for (uint32_t id : second_loop->GetBlocks()) {
    ir::BasicBlock* block = context_->cfg()->block(id);
    for (ir::Instruction& inst : *block) {
      ir::Instruction* old_inst = clone_results.ptr_map_[&inst];
      // If the instruction belongs to the first instruction group, kill it.
      if (first_loop_instructions_.count(old_inst) == 0 &&
          second_loop_instructions_.count(old_inst) == 1) {
        instructions_to_kill.push_back(&inst);
      }
    }
  }

  for (ir::Instruction* i : instructions_to_kill) {
    context_->KillInst(i);
  }

  return second_loop;
}

LoopFissionPass::LoopFissionPass(const size_t register_threshold_to_split)
    : split_multiple_times_(true) {
  // Split if the number of registers in the loop exceeds
  // |register_threshold_to_split|.
  split_criteria_ =
      [register_threshold_to_split](
          const RegisterLiveness::RegionRegisterLiveness& liveness) {
        return liveness.used_registers_ > register_threshold_to_split;
      };
}

LoopFissionPass::LoopFissionPass() : split_multiple_times_(false) {
  // Split by default.
  split_criteria_ = [](const RegisterLiveness::RegionRegisterLiveness&) {
    return true;
  };
}

bool LoopFissionPass::ShouldSplitLoop(const ir::Loop& loop, ir::IRContext* c) {
  LivenessAnalysis* analysis = c->GetLivenessAnalysis();

  RegisterLiveness::RegionRegisterLiveness liveness{};

  ir::Function* function = loop.GetHeaderBlock()->GetParent();
  analysis->Get(function)->ComputeLoopRegisterPressure(loop, &liveness);

  return split_criteria_(liveness);
}

Pass::Status LoopFissionPass::Process(ir::IRContext* c) {
  bool changed = false;

  for (ir::Function& f : *c->module()) {
    // We collect all the inner most loops in the function and run the loop
    // splitting util on each. The reason we do this is to allow us to iterate
    // over each, as calling creating new loops will invalidate the iterator.
    std::vector<ir::Loop*> inner_most_loops{};
    ir::LoopDescriptor& loop_descriptor = *c->GetLoopDescriptor(&f);
    for (ir::Loop& loop : loop_descriptor) {
      if (!loop.HasChildren() && ShouldSplitLoop(loop, c)) {
        inner_most_loops.push_back(&loop);
      }
    }

    // List of new loops which meet the criteria to be split again.
    std::vector<ir::Loop*> new_loops_to_split{};

    while (!inner_most_loops.empty()) {
      for (ir::Loop* loop : inner_most_loops) {
        LoopFissionImpl impl{c, loop};

        // Group the instructions in the loop into two different sets of related
        // instructions. If we can't group the instructions into the two sets
        // then we can't split the loop any further.
        if (!impl.BuildRelatedSets()) {
          continue;
        }

        if (impl.CanPerformSplit()) {
          ir::Loop* second_loop = impl.SplitLoop();
          changed = true;

          c->InvalidateAnalysesExceptFor(ir::IRContext::kAnalysisLoopAnalysis);
          //          c->InvalidateAnalyses(ir::IRContext::kAnalysisRegisterPressure);
          // If the newly created loop meets the criteria to be split, split it
          // again.
          if (ShouldSplitLoop(*second_loop, c))
            new_loops_to_split.push_back(second_loop);

          // If the original loop (now split) still meets the criteria to be
          // split, split it again.
          if (ShouldSplitLoop(*loop, c)) new_loops_to_split.push_back(loop);
        }
      }
      if (split_multiple_times_) {
        inner_most_loops = std::move(new_loops_to_split);
      } else {
        break;
      }
    }
  }

  return changed ? Pass::Status::SuccessWithChange
                 : Pass::Status::SuccessWithoutChange;
}

}  // namespace opt
}  // namespace spvtools
