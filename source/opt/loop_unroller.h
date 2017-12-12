
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
  bool RunOnFunction(ir::Function& f);
  bool RunOnLoop(Loop& loop);
};

}  // namespace opt
}  // namespace spvtools
#endif
