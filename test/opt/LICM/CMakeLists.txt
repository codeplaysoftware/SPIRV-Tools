# Copyright (c) 2017 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

add_spvtools_unittest(TARGET LICM_empty_loop
    SRCS ../function_utils.h
        empty_loop.cpp
    LIBS SPIRV-Tools-opt
)

add_spvtools_unittest(TARGET LICM_simple_hoist
    SRCS ../function_utils.h
        simple_hoist.cpp
    LIBS SPIRV-Tools-opt
)

add_spvtools_unittest(TARGET LICM_simple_hoist_3_loops
    SRCS ../function_utils.h
        simple_hoist_3_loops.cpp
    LIBS SPIRV-Tools-opt
)

add_spvtools_unittest(TARGET LICM_hoist_no_hoist
    SRCS ../function_utils.h
         hoist_no_hoist.cpp
    LIBS SPIRV-Tools-opt
)

add_spvtools_unittest(TARGET LICM_inside_outside_use
    SRCS ../function_utils.h
        inside_outside_use.cpp
    LIBS SPIRV-Tools-opt
)

add_spvtools_unittest(TARGET LICM_double_definition
    SRCS ../function_utils.h
        double_definition.cpp
    LIBS SPIRV-Tools-opt
)

add_spvtools_unittest(TARGET LICM_nested_single_hoist
    SRCS ../function_utils.h
        nested_single_hoist.cpp
    LIBS SPIRV-Tools-opt
)

add_spvtools_unittest(TARGET LICM_nested_double_hoist
    SRCS ../function_utils.h
        nested_double_hoist.cpp
    LIBS SPIRV-Tools-opt
)

add_spvtools_unittest(TARGET LICM_if_hoist
    SRCS ../function_utils.h
        if_hoist.cpp
    LIBS SPIRV-Tools-opt
)

add_spvtools_unittest(TARGET LICM_if_single_hoist
    SRCS ../function_utils.h
        if_single_hoist.cpp
    LIBS SPIRV-Tools-opt
)

add_spvtools_unittest(TARGET LICM_if_double_hoist
    SRCS ../function_utils.h
        if_double_hoist.cpp
    LIBS SPIRV-Tools-opt
)

add_spvtools_unittest(TARGET LICM_if_else_hoist
    SRCS ../function_utils.h
        if_else_hoist.cpp
    LIBS SPIRV-Tools-opt
)

add_spvtools_unittest(TARGET LICM_if_else_no_hoist
    SRCS ../function_utils.h
        if_else_no_hoist.cpp
    LIBS SPIRV-Tools-opt
)

add_spvtools_unittest(TARGET LICM_hoist_nested_loop_with_if
    SRCS ../function_utils.h
        hoist_nested_loop_with_if.cpp
    LIBS SPIRV-Tools-opt
)

add_spvtools_unittest(TARGET LICM_all_loop_types
    SRCS ../function_utils.h
        all_loop_types.cpp
    LIBS SPIRV-Tools-opt
)

add_spvtools_unittest(TARGET LICM_hoist_switch
    SRCS ../function_utils.h
        hoist_switch.cpp
    LIBS SPIRV-Tools-opt
)
