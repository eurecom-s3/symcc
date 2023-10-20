// This file is part of the SymCC runtime.
//
// The SymCC runtime is free software: you can redistribute it and/or modify it
// under the terms of the GNU Lesser General Public License as published by the
// Free Software Foundation, either version 3 of the License, or (at your
// option) any later version.
//
// The SymCC runtime is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
// for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with the SymCC runtime. If not, see <https://www.gnu.org/licenses/>.

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
