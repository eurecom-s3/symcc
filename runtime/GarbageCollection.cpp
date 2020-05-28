#include "GarbageCollection.h"

#include <vector>

#include <Runtime.h>
#include <Shadow.h>

/// A list of memory regions that are known to contain symbolic expressions.
std::vector<ExpressionRegion> expressionRegions;

void registerExpressionRegion(ExpressionRegion r) {
  expressionRegions.push_back(std::move(r));
}

std::set<SymExpr> collectReachableExpressions() {
  std::set<SymExpr> reachableExpressions;
  auto collectReachableExpressions = [&](ExpressionRegion r) {
    auto *end = r.first + r.second;
    for (SymExpr *expr_ptr = r.first; expr_ptr < end; expr_ptr++) {
      if (*expr_ptr != nullptr) {
        reachableExpressions.insert(*expr_ptr);
      }
    }
  };

  for (auto &r : expressionRegions) {
    collectReachableExpressions(r);
  }

  for (const auto &mapping : g_shadow_pages) {
    collectReachableExpressions({mapping.second, kPageSize});
  }

  return reachableExpressions;
}
