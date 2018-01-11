
#ifndef LIBSPIRV_OPT_LOOP_UNROLLER_H_
#define LIBSPIRV_OPT_LOOP_UNROLLER_H_
#include <list>
#include "opt/loop_descriptor.h"
#include "pass.h"

namespace spvtools {
namespace opt {

class LoopUtils {
 public:
  LoopUtils(ir::Function& function, ir::IRContext* context)
      : function_(function),
        ir_context_(context),
        loop_descriptor_(&function_) {}

  void InsertLoopClosedSSA();

  ir::BasicBlock* CopyLoop(ir::Loop& loop, ir::BasicBlock* preheader);

  ir::Instruction* CopyBody(ir::Loop& loop, int, ir::Instruction*);

  bool PartiallyUnroll(ir::Loop& loop, int factor);
  bool FullyUnroll(ir::Loop& loop);

  ir::LoopDescriptor& GetLoopDescriptor() { return loop_descriptor_; }

  void RemoveLoopFromFunction(ir::Loop& loop, ir::BasicBlock* preheader);

 private:
  ir::Function& function_;
  ir::IRContext* ir_context_;
  ir::LoopDescriptor loop_descriptor_;
  uint32_t previous_latch_block_id_;
  ir::BasicBlock* previous_continue_block_;

  std::vector<std::unique_ptr<ir::BasicBlock>> blocks_to_add_;

  ir::Instruction* RemapResultIDs(ir::Loop&, ir::BasicBlock* BB,
                                  std::map<uint32_t, uint32_t>& new_inst) const;

  void RemapOperands(ir::BasicBlock* BB, uint32_t old_header,
                     std::map<uint32_t, uint32_t>& new_inst) const;

  uint32_t GetPhiVariableID(const ir::Instruction* phi, uint32_t label) const;
};

class LoopUnroller : public Pass {
 public:
  LoopUnroller() : Pass() {}

  const char* name() const override { return "Loop unroller"; }

  Status Process(ir::IRContext* context) override;

 private:
  ir::IRContext* context_;
};

}  // namespace opt
}  // namespace spvtools
#endif
