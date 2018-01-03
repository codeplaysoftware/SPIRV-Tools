
#ifndef LIBSPIRV_OPT_LOOP_UNROLLER_H_
#define LIBSPIRV_OPT_LOOP_UNROLLER_H_
#include "opt/loop_descriptor.h"
#include "pass.h"

namespace spvtools {
namespace opt {

class LoopUnroller : public Pass {
 public:
  LoopUnroller() : Pass() {}

  const char* name() const override { return "Loop unroller"; }

  Status Process(ir::IRContext* context) override;

 private:
  ir::IRContext* context_;

  bool RunOnFunction(ir::Function& f);
  bool RunOnLoop(ir::Loop& loop);
};

}  // namespace opt
}  // namespace spvtools
#endif
