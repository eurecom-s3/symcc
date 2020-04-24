find_package(Z3 4 REQUIRED config)

add_library(SymRuntime SHARED
  ${SHARED_RUNTIME_SOURCES}
  Runtime.cpp)

target_link_libraries(SymRuntime ${Z3_LIBRARIES})

target_include_directories(SymRuntime PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${Z3_C_INCLUDE_DIRS})
