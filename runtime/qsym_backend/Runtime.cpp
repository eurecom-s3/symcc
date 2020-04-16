//
// Definitions that we need for the Qsym backend
//

#include "Runtime.h"

// C++
#include <atomic>
#include <fstream>
#include <iterator>
#include <iostream>

// C
#include <cstdio>

// Qsym
#include <afl_trace_map.h>
#include <call_stack_manager.h>
#include <expr_builder.h>
#include <solver.h>

// LLVM
#include <llvm/ADT/APInt.h>
#include <llvm/ADT/ArrayRef.h>

// Runtime
#include <Config.h>
#include <LibcWrappers.h>
#include <Shadow.h>

// A macro to create SymExpr from ExprRef. Basically, we move the shared pointer
// to the heap.
#define H(x) (new ExprRef(x))

namespace qsym {

ExprBuilder *g_expr_builder;
Solver *g_solver;
CallStackManager g_call_stack_manager;
z3::context g_z3_context;

} // namespace qsym

namespace {

/// Indicate whether the runtime has been initialized.
std::atomic_flag g_initialized = ATOMIC_FLAG_INIT;

/// The file that contains out input.
std::string inputFileName;

void deleteInputFile() { std::remove(inputFileName.c_str()); }

} // namespace

using namespace qsym;

void _sym_initialize(void) {
  if (g_initialized.test_and_set())
    return;

  loadConfig();
  initLibcWrappers();
  if (g_config.fullyConcrete)
    return;

  // Qsym requires the full input in a file
  if (g_config.inputFile.empty()) {
    std::istreambuf_iterator<char> in_begin(std::cin), in_end;
    std::vector<char> inputData(in_begin, in_end);
    inputFileName = std::tmpnam(nullptr);
    std::ofstream inputFile(inputFileName, std::ios::trunc);
    std::copy(inputData.begin(), inputData.end(),
              std::ostreambuf_iterator<char>(inputFile));
    inputFile.close();

#ifdef DEBUG_RUNTIME
    std::cout << "Loaded input:" << std::endl;
    std::copy(inputData.begin(), inputData.end(),
              std::ostreambuf_iterator<char>(std::cout));
    std::cout << std::endl;
#endif

    atexit(deleteInputFile);

    // Restore some semblance of standard input
    auto newStdin = freopen(inputFileName.c_str(), "r", stdin);
    if (newStdin == nullptr) {
      perror("Failed to reopen stdin");
      exit(-1);
    }
  } else {
    inputFileName = g_config.inputFile;
  }

  g_solver =
      new Solver(inputFileName, g_config.outputDir, g_config.aflCoverageMap);
  g_expr_builder = g_config.pruning ? PruneExprBuilder::create()
                                    : SymbolicExprBuilder::create();
}

SymExpr _sym_build_integer(uint64_t value, uint8_t bits) {
  return H(g_expr_builder->createConstant(value, bits));
}

#ifdef __SIZEOF_INT128__
SymExpr _sym_build_integer128(unsigned __int128 value) {
  std::array<uint64_t, 2> words = {static_cast<uint64_t>(value),
                                   static_cast<uint64_t>(value >> 64)};
  return H(g_expr_builder->createConstant({128, words}, 128));
}
#endif

SymExpr _sym_build_null_pointer() {
  return H(g_expr_builder->createConstant(0, sizeof(uintptr_t) * 8));
}

SymExpr _sym_build_true() { return H(g_expr_builder->createTrue()); }

SymExpr _sym_build_false() { return H(g_expr_builder->createFalse()); }

SymExpr _sym_build_bool(bool value) {
  return H(g_expr_builder->createBool(value));
}

SymExpr _sym_build_add(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createAdd(*a, *b));
}

SymExpr _sym_build_sub(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createSub(*a, *b));
}

SymExpr _sym_build_mul(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createMul(*a, *b));
}

SymExpr _sym_build_unsigned_div(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createUDiv(*a, *b));
}

SymExpr _sym_build_signed_div(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createSDiv(*a, *b));
}

SymExpr _sym_build_unsigned_rem(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createURem(*a, *b));
}

SymExpr _sym_build_signed_rem(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createSRem(*a, *b));
}

SymExpr _sym_build_shift_left(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createShl(*a, *b));
}

SymExpr _sym_build_logical_shift_right(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createLShr(*a, *b));
}

SymExpr _sym_build_arithmetic_shift_right(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createAShr(*a, *b));
}

SymExpr _sym_build_neg(SymExpr expr) {
  return H(g_expr_builder->createNeg(*expr));
}

SymExpr _sym_build_signed_less_than(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createSlt(*a, *b));
}

SymExpr _sym_build_signed_less_equal(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createSle(*a, *b));
}

SymExpr _sym_build_signed_greater_than(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createSgt(*a, *b));
}

SymExpr _sym_build_signed_greater_equal(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createSge(*a, *b));
}

SymExpr _sym_build_unsigned_less_than(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createUlt(*a, *b));
}

SymExpr _sym_build_unsigned_less_equal(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createUle(*a, *b));
}

SymExpr _sym_build_unsigned_greater_than(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createUgt(*a, *b));
}

SymExpr _sym_build_unsigned_greater_equal(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createUge(*a, *b));
}

SymExpr _sym_build_equal(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createEqual(*a, *b));
}

SymExpr _sym_build_not_equal(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createDistinct(*a, *b));
}

SymExpr _sym_build_bool_and(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createLAnd(*a, *b));
}

SymExpr _sym_build_and(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createAnd(*a, *b));
}

SymExpr _sym_build_bool_or(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createLOr(*a, *b));
}

SymExpr _sym_build_or(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createOr(*a, *b));
}

SymExpr _sym_build_bool_xor(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createLOr(*a, *b));
}

SymExpr _sym_build_xor(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createXor(*a, *b));
}

SymExpr _sym_build_sext(SymExpr expr, uint8_t bits) {
  return H(g_expr_builder->createSExt(*expr, bits + (*expr)->bits()));
}

SymExpr _sym_build_zext(SymExpr expr, uint8_t bits) {
  return H(g_expr_builder->createZExt(*expr, bits + (*expr)->bits()));
}

SymExpr _sym_build_trunc(SymExpr expr, uint8_t bits) {
  return H(g_expr_builder->createTrunc(*expr, bits));
}

void _sym_push_path_constraint(SymExpr constraint, int taken,
                               uintptr_t site_id) {
  if (!constraint)
    return;

  g_solver->addJcc(*constraint, taken, site_id);
}

SymExpr _sym_get_input_byte(size_t offset) {
  return H(g_expr_builder->createRead(offset));
}

SymExpr _sym_concat_helper(SymExpr a, SymExpr b) {
  return H(g_expr_builder->createConcat(*a, *b));
}

SymExpr _sym_extract_helper(SymExpr expr, size_t first_bit, size_t last_bit) {
  return H(
      g_expr_builder->createExtract(*expr, last_bit, first_bit - last_bit + 1));
}

size_t _sym_bits_helper(SymExpr expr) { return (*expr)->bits(); }

SymExpr _sym_build_bool_to_bits(SymExpr expr, uint8_t bits) {
  return H(g_expr_builder->boolToBit(*expr, bits));
}

//
// Floating-point operations (unsupported in Qsym)
//

#define UNSUPPORTED(prototype)                                                 \
  prototype { return nullptr; }

UNSUPPORTED(SymExpr _sym_build_float(double value, int is_double))
UNSUPPORTED(SymExpr _sym_build_fp_add(SymExpr a, SymExpr b))
UNSUPPORTED(SymExpr _sym_build_fp_sub(SymExpr a, SymExpr b))
UNSUPPORTED(SymExpr _sym_build_fp_mul(SymExpr a, SymExpr b))
UNSUPPORTED(SymExpr _sym_build_fp_div(SymExpr a, SymExpr b))
UNSUPPORTED(SymExpr _sym_build_fp_rem(SymExpr a, SymExpr b))
UNSUPPORTED(SymExpr _sym_build_fp_abs(SymExpr a))
UNSUPPORTED(SymExpr _sym_build_float_ordered_greater_than(SymExpr a, SymExpr b))
UNSUPPORTED(SymExpr _sym_build_float_ordered_greater_equal(SymExpr a,
                                                           SymExpr b))
UNSUPPORTED(SymExpr _sym_build_float_ordered_less_than(SymExpr a, SymExpr b))
UNSUPPORTED(SymExpr _sym_build_float_ordered_less_equal(SymExpr a, SymExpr b))
UNSUPPORTED(SymExpr _sym_build_float_ordered_equal(SymExpr a, SymExpr b))
UNSUPPORTED(SymExpr _sym_build_float_ordered_not_equal(SymExpr a, SymExpr b))
UNSUPPORTED(SymExpr _sym_build_float_ordered(SymExpr a, SymExpr b))
UNSUPPORTED(SymExpr _sym_build_float_unordered(SymExpr a, SymExpr b))
UNSUPPORTED(SymExpr _sym_build_float_unordered_greater_than(SymExpr a,
                                                            SymExpr b))
UNSUPPORTED(SymExpr _sym_build_float_unordered_greater_equal(SymExpr a,
                                                             SymExpr b))
UNSUPPORTED(SymExpr _sym_build_float_unordered_less_than(SymExpr a, SymExpr b))
UNSUPPORTED(SymExpr _sym_build_float_unordered_less_equal(SymExpr a, SymExpr b))
UNSUPPORTED(SymExpr _sym_build_float_unordered_equal(SymExpr a, SymExpr b))
UNSUPPORTED(SymExpr _sym_build_float_unordered_not_equal(SymExpr a, SymExpr b))
UNSUPPORTED(SymExpr _sym_build_int_to_float(SymExpr value, int is_double,
                                            int is_signed))
UNSUPPORTED(SymExpr _sym_build_float_to_float(SymExpr expr, int to_double))
UNSUPPORTED(SymExpr _sym_build_bits_to_float(SymExpr expr, int to_double))
UNSUPPORTED(SymExpr _sym_build_float_to_bits(SymExpr expr))
UNSUPPORTED(SymExpr _sym_build_float_to_signed_integer(SymExpr expr,
                                                       uint8_t bits))
UNSUPPORTED(SymExpr _sym_build_float_to_unsigned_integer(SymExpr expr,
                                                         uint8_t bits))

#undef UNSUPPORTED
#undef H

//
// Call-stack tracing
//

void _sym_notify_call(uintptr_t site_id) {
  g_call_stack_manager.visitCall(site_id);
}

void _sym_notify_ret(uintptr_t site_id) {
  g_call_stack_manager.visitRet(site_id);
}

void _sym_notify_basic_block(uintptr_t site_id) {
  g_call_stack_manager.visitBasicBlock(site_id);
}

//
// Debugging
//

const char *_sym_expr_to_string(SymExpr expr) {
  static char buffer[4096];

  auto expr_string = (*expr)->toString();
  auto copied = expr_string.copy(
      buffer, std::min(expr_string.length(), sizeof(buffer) - 1));
  buffer[copied] = '\0';

  return buffer;
}

bool _sym_feasible(SymExpr expr) {
  (*expr)->simplify();

  g_solver->push();
  g_solver->add((*expr)->toZ3Expr());
  bool feasible = (g_solver->check() == z3::sat);
  g_solver->pop();

  return feasible;
}
