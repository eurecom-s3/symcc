
add_library(SymRuntime SHARED
  ../Config.cpp
  ../RuntimeCommon.cpp
  ../LibcWrappers.cpp
  ../Shadow.cpp
  Runtime.cpp)

target_link_libraries(SymRuntime "z3")

target_include_directories(SymRuntime PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..)
