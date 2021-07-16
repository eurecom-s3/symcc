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

cmake_minimum_required(VERSION 3.19)

add_library(SymRuntime SHARED
  ${SHARED_RUNTIME_SOURCES}
  Runtime.cpp)

target_include_directories(SymRuntime PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..)

set_target_properties(SymRuntime PROPERTIES COMPILE_FLAGS "-Werror")

find_package(Corrosion REQUIRED)

corrosion_import_crate(MANIFEST_PATH ../../../runtime/Cargo.toml)

# enfore panic=abort on the rust runtime, as unwinding will never be what we want (and may cause weird behavior)
set_property(
        TARGET runtime
        APPEND
        PROPERTY CORROSION_ENVIRONMENT_VARIABLES
        "CARGO_PROFILE_DEV_PANIC=abort"
)
set_property(
        TARGET runtime
        APPEND
        PROPERTY CORROSION_ENVIRONMENT_VARIABLES
        "CARGO_PROFILE_RELEASE_PANIC=abort"
)

target_link_libraries(SymRuntime PUBLIC runtime)
