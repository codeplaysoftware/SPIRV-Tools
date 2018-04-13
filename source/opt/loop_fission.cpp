
#include "opt/loop_fission.h"

namespace spvtools {
namespace opt {

class LoopFissionUtils {
 public:
  LoopFissionUtils(ir::IRContext* context) : context_(context) {}

  void BuildRelatedSets(const ir::Loop& loop);

  bool CanPerformSplit(ir::Loop* loop);

  void SplitLoop(ir::Loop* loop);

 private:
  // We group the instructions in the block into two different groups.
  std::set<ir::Instruction*> group_1_;
  std::set<ir::Instruction*> group_2_;

  std::set<ir::Instruction*> seen_instructions_;

  ir::IRContext* context_;
  // Traverse the def use chain of |inst| and add the users and uses of |inst|
  // which are in the same loop to the |returned_set|.
  void TraverseUseDef(const ir::Loop& loop, ir::Instruction* inst,
                      std::set<ir::Instruction*>* returned_set,
                      bool ignore_phi_users = false);
};

void LoopFissionUtils::TraverseUseDef(const ir::Loop& loop,
                                      ir::Instruction* inst,
                                      std::set<ir::Instruction*>* returned_set,
                                      bool ignore_phi_users) {
  assert(returned_set && "Set to be returned cannot be null.");

  opt::analysis::DefUseManager* def_use = context_->get_def_use_mgr();
  std::set<ir::Instruction*>& inst_set = *returned_set;

  // We create this functor to traverse the use def chain to build the
  // grouping of related instructions. The lambda captures the std::function
  // to allow it to recurse.
  std::function<void(ir::Instruction*)> traverser_functor;
  traverser_functor = [this, &loop, def_use, &inst_set, &traverser_functor,
                       ignore_phi_users](ir::Instruction* user) {
    if (!user || seen_instructions_.count(user) != 0 ||
        !context_->get_instr_block(user) ||
        !loop.IsInsideLoop(context_->get_instr_block(user))) {
      return;
    }

    if (user->opcode() == SpvOp::SpvOpLoopMerge ||
        user->opcode() == SpvOp::SpvOpLabel)
      return;

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

void LoopFissionUtils::BuildRelatedSets(const ir::Loop& loop) {
  std::vector<std::set<ir::Instruction*>> sets{};

  // We want to ignore all the instructions stemming from the loop condition
  // instruction.
  ir::BasicBlock* condition_block = loop.FindConditionBlock();
  ir::Instruction* condition = &*condition_block->tail();

  std::set<ir::Instruction*> tmp_set{};

  TraverseUseDef(loop, condition, &tmp_set, true);
  for (uint32_t block_id : loop.GetBlocks()) {
    ir::BasicBlock* block = context_->cfg()->block(block_id);

    for (ir::Instruction& inst : *block) {
      if (seen_instructions_.count(&inst) != 0) {
        continue;
      }

      std::set<ir::Instruction*> inst_set{};
      TraverseUseDef(loop, &inst, &inst_set);

      if (!inst_set.empty()) sets.push_back(std::move(inst_set));
    }
  }

  //
  for (size_t index = 0; index < sets.size() / 2; ++index) {
    group_1_.insert(sets[index].begin(), sets[index].end());
  }
  for (size_t index = sets.size() / 2; index < sets.size(); ++index) {
    group_2_.insert(sets[index].begin(), sets[index].end());
  }
}

bool LoopFissionUtils::CanPerformSplit(ir::Loop* loop) {
  std::vector<const ir::Loop*> loops;
  ir::Loop* parent_loop = loop;

  while (parent_loop) {
    loops.push_back(parent_loop);
    parent_loop = parent_loop->GetParent();
  }

  LoopDependenceAnalysis analysis{context_, loops};

  std::vector<ir::Instruction*> set_one_stores{};
  std::vector<ir::Instruction*> set_one_loads{};

  for (ir::Instruction* inst : group_1_) {
    if (inst->opcode() == SpvOp::SpvOpStore) {
      set_one_stores.push_back(inst);
    } else if (inst->opcode() == SpvOp::SpvOpLoad) {
      set_one_loads.push_back(inst);
    }
  }

  const size_t loop_depth = loop->GetDepth();

  for (ir::Instruction* inst : group_2_) {
    // Look at the dependency between the.
    if (inst->opcode() == SpvOp::SpvOpLoad) {
      for (ir::Instruction* store : set_one_stores) {
        DistanceVector vec{loop_depth};

        // If not independent check the distance vector.
        if (!analysis.GetDependence(store, inst, &vec)) {
          for (DistanceEntry& entry : vec.GetEntries()) {
            if (entry.distance <= 0) return false;
          }
        }
      }
    } else if (inst->opcode() == SpvOp::SpvOpStore) {
      for (ir::Instruction* load : set_one_loads) {
        DistanceVector vec{loop_depth};

        // If not independent check the distance vector.
        if (!analysis.GetDependence(inst, load, &vec)) {
          for (DistanceEntry& entry : vec.GetEntries()) {
            if (entry.distance > 0) return false;
          }
        }
      }
    }
  }
  return true;
}

void LoopFissionUtils::SplitLoop(ir::Loop* loop) {
  BuildRelatedSets(*loop);

  if (!CanPerformSplit(loop)) return;

  // Clone the loop.
  LoopUtils util{context_, loop};
  LoopUtils::LoopCloningResult clone_results;
  ir::Loop* second_loop = util.CloneAndAttachLoopToHeader(&clone_results);

  second_loop->UpdateLoopMergeInst();

  // Add the loop to the module.
  ir::Function::iterator it =
      util.GetFunction()->FindBlock(loop->GetOrCreatePreHeaderBlock()->id());
  util.GetFunction()->AddBasicBlocks(clone_results.cloned_bb_.begin(),
                                     clone_results.cloned_bb_.end(), ++it);

  std::vector<ir::Instruction*> instructions_to_kill{};
  for (uint32_t id : loop->GetBlocks()) {
    ir::BasicBlock* block = context_->cfg()->block(id);

    for (ir::Instruction& inst : *block) {
      // If the instruction belongs to the second instruction group, kill
      // it.
      if (group_1_.count(&inst) == 1 && group_2_.count(&inst) == 0) {
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
      if (group_1_.count(old_inst) == 0 && group_2_.count(old_inst) == 1) {
        instructions_to_kill.push_back(&inst);
      }
    }
  }

  for (ir::Instruction* i : instructions_to_kill) {
    context_->KillInst(i);
  }
}

Pass::Status LoopFissionPass::Process(ir::IRContext* c) {
  LoopFissionUtils utils{c};

  bool changed = false;

  for (ir::Function& f : *c->module()) {
    ir::LoopDescriptor& loop_descriptor = *c->GetLoopDescriptor(&f);
    ir::Loop* loop = nullptr;

    loop = &loop_descriptor.GetLoopByIndex(0);
    utils.SplitLoop(loop);
    changed = true;
  }

  return changed ? Pass::Status::SuccessWithChange
                 : Pass::Status::SuccessWithoutChange;
}

}  // namespace opt
}  // namespace spvtools
