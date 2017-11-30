#include "loop_descriptor.h"
#include <iostream>

LoopDescriptor::LoopDescriptor(const ir::Function* f) {
  IRContext * context  = f->GetParent()->context();
}
