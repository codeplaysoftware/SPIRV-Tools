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

#ifndef LIBSPIRV_OPT_VIRTUAL_REGISTER_PRESSURE_H_
#define LIBSPIRV_OPT_VIRTUAL_REGISTER_PRESSURE_H_

#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "cfg.h"
#include "def_use_manager.h"
#include "dominator_tree.h"
#include "function.h"
#include "types.h"

namespace spvtools {
namespace ir {
class IRContext;
class LoopDescriptor;
}  // namespace ir

namespace opt {

// Handles the register pressure of a function for different regions (function,
// loop, basic block). It also contains some utilities to foresee the register
// pressure following code transformations.
class RegisterLiveness {
 public:
  // Classification of SSA registers.
  struct RegisterClass {
    analysis::Type* type_;
    bool is_uniform_;

    bool operator==(const RegisterClass& rhs) const {
      return std::tie(type_, is_uniform_) ==
             std::tie(rhs.type_, rhs.is_uniform_);
    }
  };

  struct RegionRegisterLiveness {
    using LiveSet = std::unordered_set<ir::Instruction*>;

    // SSA register live when entering the basic block.
    LiveSet live_in_;
    // SSA register live when exiting the basic block.
    LiveSet live_out_;

    // Maximum number of required registers.
    size_t used_registers_;
    // Break down of the number of required registers per class of register.
    std::vector<std::pair<RegisterClass, size_t>> registers_classes_;

    void AddRegisterClass(const RegisterClass& reg_class) {
      auto it = std::find_if(
          registers_classes_.begin(), registers_classes_.end(),
          [&reg_class](const std::pair<RegisterClass, size_t>& class_count) {
            return class_count.first == reg_class;
          });
      if (it != registers_classes_.end()) {
        it->second++;
      } else {
        registers_classes_.emplace_back(std::move(reg_class),
                                        static_cast<size_t>(1));
      }
    }
  };

  RegisterLiveness(ir::IRContext* context, ir::Function* f)
      : context_(context) {
    Analyze(f);
  }

  const RegionRegisterLiveness* Get(const ir::BasicBlock* bb) const {
    return Get(bb->id());
  }

  const RegionRegisterLiveness* Get(uint32_t bb_id) const {
    RegionRegisterLivenessMap::const_iterator it = block_pressure_.find(bb_id);
    if (it != block_pressure_.end()) {
      return &it->second;
    }
    return nullptr;
  }

  ir::IRContext* GetContext() const { return context_; }

  RegionRegisterLiveness* Get(const ir::BasicBlock* bb) { return Get(bb->id()); }

  RegionRegisterLiveness* Get(uint32_t bb_id) {
    RegionRegisterLivenessMap::iterator it = block_pressure_.find(bb_id);
    if (it != block_pressure_.end()) {
      return &it->second;
    }
    return nullptr;
  }

  RegionRegisterLiveness* GetOrInsert(uint32_t bb_id) {
    return &block_pressure_[bb_id];
  }

 private:
  using RegionRegisterLivenessMap =
      std::unordered_map<uint32_t, RegionRegisterLiveness>;

  ir::IRContext* context_;
  RegionRegisterLivenessMap block_pressure_;

  void Analyze(ir::Function* f);
};

// Handles the register pressure of a function for different regions (function,
// loop, basic block). It also contains some utilities to foresee the register
// pressure following code transformations.
class LivenessAnalysis {
  using LivenessAnalysisMap =
      std::unordered_map<const ir::Function*, RegisterLiveness>;

 public:
  LivenessAnalysis(ir::IRContext* context) : context_(context) {}

  const RegisterLiveness* Get(ir::Function* f) {
    LivenessAnalysisMap::iterator it = analysis_cache_.find(f);
    if (it != analysis_cache_.end()) {
      return &it->second;
    }
    return &analysis_cache_.emplace(f, RegisterLiveness{context_, f})
                .first->second;
  }

 private:
  ir::IRContext* context_;
  LivenessAnalysisMap analysis_cache_;
};

}  // namespace opt
}  // namespace spvtools

#endif  // ! LIBSPIRV_OPT_VIRTUAL_REGISTER_PRESSURE_H_
