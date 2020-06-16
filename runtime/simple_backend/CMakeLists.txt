# This file is part of SymCC.
#
# SymCC is free software: you can redistribute it and/or modify it under the
# terms of the GNU General Public License as published by the Free Software
# Foundation, either version 3 of the License, or (at your option) any later
# version.
#
# SymCC is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE. See the GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along with
# SymCC. If not, see <https://www.gnu.org/licenses/>.

find_package(Z3 4 REQUIRED config)

add_library(SymRuntime SHARED
  ${SHARED_RUNTIME_SOURCES}
  Runtime.cpp)

target_link_libraries(SymRuntime ${Z3_LIBRARIES})

target_include_directories(SymRuntime PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${Z3_C_INCLUDE_DIRS})

set_target_properties(SymRuntime PROPERTIES COMPILE_FLAGS "-Werror")
