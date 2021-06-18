// This file is part of SymCC.
//
// SymCC is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// SymCC is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// SymCC. If not, see <https://www.gnu.org/licenses/>.

#include <Runtime.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <set>
#include <vector>

#include "Config.h"
#include "GarbageCollection.h"
#include "LibcWrappers.h"
#include "Shadow.h"

namespace
{

  /// Indicate whether the runtime has been initialized.
  std::atomic_flag g_initialized = ATOMIC_FLAG_INIT;

  FILE *g_log = stderr;

} // namespace

void _sym_initialize(void)
{
  if (g_initialized.test_and_set())
    return;

#ifndef NDEBUG
  std::cerr << "Initializing symbolic runtime" << std::endl;
#endif

  loadConfig();
  initLibcWrappers();
  std::cerr << "This is SymCC running with the rust backend" << std::endl
            << std::endl;

  if (g_config.logFile.empty())
  {
    g_log = stderr;
  }
  else
  {
    g_log = fopen(g_config.logFile.c_str(), "w");
  }
}

/* No call-stack tracing */
void _sym_notify_call(uintptr_t) {}
void _sym_notify_ret(uintptr_t) {}
void _sym_notify_basic_block(uintptr_t) {}

/* No debugging */
const char *_sym_expr_to_string(SymExpr)
{
  return NULL;
}

bool _sym_feasible(SymExpr)
{
  return false;
}

/* No garbage collection */
void _sym_collect_garbage()
{
}
