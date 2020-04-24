#ifndef GARBAGECOLLECTION_H
#define GARBAGECOLLECTION_H

#include <utility>
#include <set>

#include <Runtime.h>

/// An imitation of std::span (which is not available before C++20) for symbolic
/// expressions.
using ExpressionRegion = std::pair<SymExpr *, size_t>;

/// Add the specified region to the list of places to search for symbolic
/// expressions.
void registerExpressionRegion(ExpressionRegion r);

/// Return the set of currently reachable symbolic expressions.
std::set<SymExpr> collectReachableExpressions();

#endif
