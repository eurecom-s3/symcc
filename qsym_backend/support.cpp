//
// Definitions that we need for the Qsym backend
//

#include <afl_trace_map.h>
#include <call_stack_manager.h>
#include <expr_builder.h>
#include <solver.h>

namespace qsym {

ExprBuilder *g_expr_builder;
Solver *g_solver;
CallStackManager g_call_stack_manager;
z3::context g_z3_context;

AflTraceMap::AflTraceMap(const std::string path) {}

bool AflTraceMap::isInterestingBranch(ADDRINT pc, bool taken) {
  // TODO
  return true;
}

} // namespace qsym

// TODO qsym::g_z3_context, g_solver

void initQsymBackend() {
  qsym::g_expr_builder = qsym::SymbolicExprBuilder::create();
}
