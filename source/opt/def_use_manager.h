// Copyright (c) 2016 Google Inc.
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

#ifndef LIBSPIRV_OPT_DEF_USE_MANAGER_H_
#define LIBSPIRV_OPT_DEF_USE_MANAGER_H_

#include <list>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "instruction.h"
#include "module.h"
#include "spirv-tools/libspirv.hpp"

namespace spvtools {
namespace opt {
namespace analysis {

// Class for representing a use of id. Note that:
// * Result type id is a use.
// * Ids referenced in OpSectionMerge & OpLoopMerge are considered as use.
// * Ids referenced in OpPhi's in operands are considered as use.
struct Use {
  ir::Instruction* inst;   // Instruction using the id.
  uint32_t operand_index;  // logical operand index of the id use. This can be
                           // the index of result type id.
};

inline bool operator==(const Use& lhs, const Use& rhs) {
  return lhs.inst == rhs.inst && lhs.operand_index == rhs.operand_index;
}

inline bool operator!=(const Use& lhs, const Use& rhs) { return !(lhs == rhs); }

inline bool operator<(const Use& lhs, const Use& rhs) {
  if (lhs.inst < rhs.inst) return true;
  if (lhs.inst > rhs.inst) return false;
  return lhs.operand_index < rhs.operand_index;
}

// Definition and user pair.
//
// The first element of the pair is the definition.
// The second element of the pair is the user.
//
// Definition should never be null. User can be null, however, such an entry
// should be used only for searching (e.g. all users of a particular definition)
// and never stored in a container.
using UserEntry = std::pair<ir::Instruction*, ir::Instruction*>;

// Orders UserEntry for use in associative containers (i.e. less than ordering).
//
// The definition of an UserEntry is treated as the major key and the users as
// the minor key so that all the users of a particular definition are
// consecutive in a container.
//
// A null user always compares less than a real user. This is done to provide
// easy values to search for the beginning of the users of a particular
// definition (i.e. using {def, nullptr}).
struct UserEntryLess {
  bool operator()(const UserEntry& lhs, const UserEntry& rhs) const {
    // If lhs.first and rhs.first are both null, fall through to checking the
    // second entries.
    if (!lhs.first && rhs.first) return true;
    if (lhs.first && !rhs.first) return false;

    // If neither definition is null, then compare unique ids.
    if (lhs.first && rhs.first) {
      if (lhs.first->unique_id() < rhs.first->unique_id()) return true;
      if (rhs.first->unique_id() < lhs.first->unique_id()) return false;
    }

    // Return false on equality.
    if (!lhs.second && !rhs.second) return false;
    if (!lhs.second) return true;
    if (!rhs.second) return false;

    // If neither user is null then compare unique ids.
    return lhs.second->unique_id() < rhs.second->unique_id();
  }
};

// A class for analyzing and managing defs and uses in an ir::Module.
class DefUseManager {
 public:
  using IdToDefMap = std::unordered_map<uint32_t, ir::Instruction*>;
  using IdToUsersMap = std::set<UserEntry, UserEntryLess>;

  // Constructs a def-use manager from the given |module|. All internal messages
  // will be communicated to the outside via the given message |consumer|. This
  // instance only keeps a reference to the |consumer|, so the |consumer| should
  // outlive this instance.
  DefUseManager(ir::Module* module) { AnalyzeDefUse(module); }

  DefUseManager(const DefUseManager&) = delete;
  DefUseManager(DefUseManager&&) = delete;
  DefUseManager& operator=(const DefUseManager&) = delete;
  DefUseManager& operator=(DefUseManager&&) = delete;

  // Analyzes the defs in the given |inst|.
  void AnalyzeInstDef(ir::Instruction* inst);

  // Analyzes the uses in the given |inst|.
  //
  // All operands of |inst| must be analyzed as defs.
  void AnalyzeInstUse(ir::Instruction* inst);

  // Analyzes the defs and uses in the given |inst|.
  void AnalyzeInstDefUse(ir::Instruction* inst);

  // Returns the def instruction for the given |id|. If there is no instruction
  // defining |id|, returns nullptr.
  ir::Instruction* GetDef(uint32_t id);
  const ir::Instruction* GetDef(uint32_t id) const;

  // Replace all uses of the id |def_id| by the id |new_use|.
  void ReplaceAllUseOf(uint32_t def_id, uint32_t new_use) {
    std::unordered_set<ir::Instruction*> modifed_instructions;
    ReplaceAllUseOf(def_id, new_use, &modifed_instructions);
  }

  // Replace all uses of the id |def_id| by the id |new_use|. All modified
  // instructions are recorded in the |modifed_instructions| set. The set
  // |modifed_instructions| must be empty on invocation.
  void ReplaceAllUseOf(
      uint32_t def_id, uint32_t new_use,
      std::unordered_set<ir::Instruction*>* modifed_instructions);

  // Runs the given function |f| on each unique user instruction of |def| (or
  // |id|).
  //
  // If one instruction uses |def| in multiple operands, that instruction will
  // only be visited once.
  //
  // |def| (or |id|) must be registered as a definition.
  void ForEachUser(const ir::Instruction* def,
                   const std::function<void(ir::Instruction*)>& f) const;
  void ForEachUser(uint32_t id,
                   const std::function<void(ir::Instruction*)>& f) const;

  // Runs the given function |f| on each unique user instruction of |def| (or
  // |id|). If |f| returns false, iteration is terminated and this function
  // returns false.
  //
  // If one instruction uses |def| in multiple operands, that instruction will
  // be only be visited once.
  //
  // |def| (or |id|) must be registered as a definition.
  bool WhileEachUser(const ir::Instruction* def,
                     const std::function<bool(ir::Instruction*)>& f) const;
  bool WhileEachUser(uint32_t id,
                     const std::function<bool(ir::Instruction*)>& f) const;

  // Runs the given function |f| on each unique use of |def| (or
  // |id|).
  //
  // If one instruction uses |def| in multiple operands, each operand will be
  // visited separately.
  //
  // |def| (or |id|) must be registered as a definition.
  void ForEachUse(const ir::Instruction* def,
                  const std::function<void(ir::Instruction*,
                                           uint32_t operand_index)>& f) const;
  void ForEachUse(uint32_t id,
                  const std::function<void(ir::Instruction*,
                                           uint32_t operand_index)>& f) const;

  // Runs the given function |f| on each unique use of |def| (or
  // |id|). If |f| returns false, iteration is terminated and this function
  // returns false.
  //
  // If one instruction uses |def| in multiple operands, each operand will be
  // visited separately.
  //
  // |def| (or |id|) must be registered as a definition.
  bool WhileEachUse(const ir::Instruction* def,
                    const std::function<bool(ir::Instruction*,
                                             uint32_t operand_index)>& f) const;
  bool WhileEachUse(uint32_t id,
                    const std::function<bool(ir::Instruction*,
                                             uint32_t operand_index)>& f) const;

  // Returns the number of users of |def| (or |id|).
  uint32_t NumUsers(const ir::Instruction* def) const;
  uint32_t NumUsers(uint32_t id) const;

  // Returns the number of uses of |def| (or |id|).
  uint32_t NumUses(const ir::Instruction* def) const;
  uint32_t NumUses(uint32_t id) const;

  // Returns the annotation instrunctions which are a direct use of the given
  // |id|. This means when the decorations are applied through decoration
  // group(s), this function will just return the OpGroupDecorate
  // instrcution(s) which refer to the given id as an operand. The OpDecorate
  // instructions which decorate the decoration group will not be returned.
  std::vector<ir::Instruction*> GetAnnotations(uint32_t id) const;

  // Returns the map from ids to their def instructions.
  const IdToDefMap& id_to_defs() const { return id_to_def_; }
  // Returns the map from instructions to their users.
  const IdToUsersMap& id_to_users() const { return id_to_users_; }

  // Clear the internal def-use record of the given instruction |inst|. This
  // method will update the use information of the operand ids of |inst|. The
  // record: |inst| uses an |id|, will be removed from the use records of |id|.
  // If |inst| defines an result id, the use record of this result id will also
  // be removed. Does nothing if |inst| was not analyzed before.
  void ClearInst(ir::Instruction* inst);

  // Erases the records that a given instruction uses its operand ids.
  void EraseUseRecordsOfOperandIds(const ir::Instruction* inst);

  friend bool operator==(const DefUseManager&, const DefUseManager&);
  friend bool operator!=(const DefUseManager& lhs, const DefUseManager& rhs) {
    return !(lhs == rhs);
  }

 private:
  using InstToUsedIdsMap =
      std::unordered_map<const ir::Instruction*, std::vector<uint32_t>>;

  // Returns the first location that {|def|, nullptr} could be inserted into the
  // users map without violating ordering.
  IdToUsersMap::const_iterator UsersBegin(const ir::Instruction* def) const;

  // Returns true if |iter| has not reached the end of |def|'s users.
  //
  // In the first version |iter| is compared against the end of the map for
  // validity before other checks. In the second version, |iter| is compared
  // against |cached_end| for validity before other checks. This allows caching
  // the map's end which is a performance improvement on some platforms.
  bool UsersNotEnd(const IdToUsersMap::const_iterator& iter,
                   const ir::Instruction* def) const;
  bool UsersNotEnd(const IdToUsersMap::const_iterator& iter,
                   const IdToUsersMap::const_iterator& cached_end,
                   const ir::Instruction* def) const;

  // Analyzes the defs and uses in the given |module| and populates data
  // structures in this class. Does nothing if |module| is nullptr.
  void AnalyzeDefUse(ir::Module* module);

  IdToDefMap id_to_def_;      // Mapping from ids to their definitions
  IdToUsersMap id_to_users_;  // Mapping from ids to their users
  // Mapping from instructions to the ids used in the instruction.
  InstToUsedIdsMap inst_to_used_ids_;
};

}  // namespace analysis
}  // namespace opt
}  // namespace spvtools

#endif  // LIBSPIRV_OPT_DEF_USE_MANAGER_H_
