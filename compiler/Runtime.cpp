#include "Runtime.h"

#include <llvm/ADT/StringSet.h>
#include <llvm/IR/IRBuilder.h>

using namespace llvm;

Runtime::Runtime(Module &M) {
  IRBuilder<> IRB(M.getContext());
  auto intPtrType = M.getDataLayout().getIntPtrType(M.getContext());
  auto ptrT = IRB.getInt8PtrTy();
  auto int8T = IRB.getInt8Ty();
  auto voidT = IRB.getVoidTy();

  buildInteger = M.getOrInsertFunction("_sym_build_integer", ptrT,
                                       IRB.getInt64Ty(), int8T);
  buildInteger128 =
      M.getOrInsertFunction("_sym_build_integer128", ptrT, IRB.getInt128Ty());
  buildFloat = M.getOrInsertFunction("_sym_build_float", ptrT,
                                     IRB.getDoubleTy(), IRB.getInt1Ty());
  buildNullPointer = M.getOrInsertFunction("_sym_build_null_pointer", ptrT);
  buildTrue = M.getOrInsertFunction("_sym_build_true", ptrT);
  buildFalse = M.getOrInsertFunction("_sym_build_false", ptrT);
  buildBool = M.getOrInsertFunction("_sym_build_bool", ptrT, IRB.getInt1Ty());
  buildNeg = M.getOrInsertFunction("_sym_build_neg", ptrT, ptrT);
  buildSExt = M.getOrInsertFunction("_sym_build_sext", ptrT, ptrT, int8T);
  buildZExt = M.getOrInsertFunction("_sym_build_zext", ptrT, ptrT, int8T);
  buildTrunc = M.getOrInsertFunction("_sym_build_trunc", ptrT, ptrT, int8T);
  buildIntToFloat = M.getOrInsertFunction("_sym_build_int_to_float", ptrT, ptrT,
                                          IRB.getInt1Ty(), IRB.getInt1Ty());
  buildFloatToFloat = M.getOrInsertFunction("_sym_build_float_to_float", ptrT,
                                            ptrT, IRB.getInt1Ty());
  buildBitsToFloat = M.getOrInsertFunction("_sym_build_bits_to_float", ptrT,
                                           ptrT, IRB.getInt1Ty());
  buildFloatToBits =
      M.getOrInsertFunction("_sym_build_float_to_bits", ptrT, ptrT);
  buildFloatToSignedInt = M.getOrInsertFunction(
      "_sym_build_float_to_signed_integer", ptrT, ptrT, int8T);
  buildFloatToUnsignedInt = M.getOrInsertFunction(
      "_sym_build_float_to_unsigned_integer", ptrT, ptrT, int8T);
  buildFloatAbs = M.getOrInsertFunction("_sym_build_fp_abs", ptrT, ptrT);
  buildBoolAnd = M.getOrInsertFunction("_sym_build_bool_and", ptrT, ptrT, ptrT);
  buildBoolOr = M.getOrInsertFunction("_sym_build_bool_or", ptrT, ptrT, ptrT);
  buildBoolXor = M.getOrInsertFunction("_sym_build_bool_xor", ptrT, ptrT, ptrT);
  pushPathConstraint = M.getOrInsertFunction("_sym_push_path_constraint", voidT,
                                             ptrT, IRB.getInt1Ty(), intPtrType);

  setParameterExpression = M.getOrInsertFunction(
      "_sym_set_parameter_expression", voidT, int8T, ptrT);
  getParameterExpression =
      M.getOrInsertFunction("_sym_get_parameter_expression", ptrT, int8T);
  setReturnExpression =
      M.getOrInsertFunction("_sym_set_return_expression", voidT, ptrT);
  getReturnExpression =
      M.getOrInsertFunction("_sym_get_return_expression", ptrT);

#define LOAD_BINARY_OPERATOR_HANDLER(constant, name)                           \
  binaryOperatorHandlers[Instruction::constant] =                              \
      M.getOrInsertFunction("_sym_build_" #name, ptrT, ptrT, ptrT);

  LOAD_BINARY_OPERATOR_HANDLER(Add, add)
  LOAD_BINARY_OPERATOR_HANDLER(Sub, sub)
  LOAD_BINARY_OPERATOR_HANDLER(Mul, mul)
  LOAD_BINARY_OPERATOR_HANDLER(UDiv, unsigned_div)
  LOAD_BINARY_OPERATOR_HANDLER(SDiv, signed_div)
  LOAD_BINARY_OPERATOR_HANDLER(URem, unsigned_rem)
  LOAD_BINARY_OPERATOR_HANDLER(SRem, signed_rem)
  LOAD_BINARY_OPERATOR_HANDLER(Shl, shift_left)
  LOAD_BINARY_OPERATOR_HANDLER(LShr, logical_shift_right)
  LOAD_BINARY_OPERATOR_HANDLER(AShr, arithmetic_shift_right)
  LOAD_BINARY_OPERATOR_HANDLER(And, and)
  LOAD_BINARY_OPERATOR_HANDLER(Or, or)
  LOAD_BINARY_OPERATOR_HANDLER(Xor, xor)

  // Floating-point arithmetic
  LOAD_BINARY_OPERATOR_HANDLER(FAdd, fp_add)
  LOAD_BINARY_OPERATOR_HANDLER(FSub, fp_sub)
  LOAD_BINARY_OPERATOR_HANDLER(FMul, fp_mul)
  LOAD_BINARY_OPERATOR_HANDLER(FDiv, fp_div)
  LOAD_BINARY_OPERATOR_HANDLER(FRem, fp_rem)

#undef LOAD_BINARY_OPERATOR_HANDLER

#define LOAD_COMPARISON_HANDLER(constant, name)                                \
  comparisonHandlers[CmpInst::constant] =                                      \
      M.getOrInsertFunction("_sym_build_" #name, ptrT, ptrT, ptrT);

  LOAD_COMPARISON_HANDLER(ICMP_EQ, equal)
  LOAD_COMPARISON_HANDLER(ICMP_NE, not_equal)
  LOAD_COMPARISON_HANDLER(ICMP_UGT, unsigned_greater_than)
  LOAD_COMPARISON_HANDLER(ICMP_UGE, unsigned_greater_equal)
  LOAD_COMPARISON_HANDLER(ICMP_ULT, unsigned_less_than)
  LOAD_COMPARISON_HANDLER(ICMP_ULE, unsigned_less_equal)
  LOAD_COMPARISON_HANDLER(ICMP_SGT, signed_greater_than)
  LOAD_COMPARISON_HANDLER(ICMP_SGE, signed_greater_equal)
  LOAD_COMPARISON_HANDLER(ICMP_SLT, signed_less_than)
  LOAD_COMPARISON_HANDLER(ICMP_SLE, signed_less_equal)

  // Floating-point comparisons
  LOAD_COMPARISON_HANDLER(FCMP_OGT, float_ordered_greater_than)
  LOAD_COMPARISON_HANDLER(FCMP_OGE, float_ordered_greater_equal)
  LOAD_COMPARISON_HANDLER(FCMP_OLT, float_ordered_less_than)
  LOAD_COMPARISON_HANDLER(FCMP_OLE, float_ordered_less_equal)
  LOAD_COMPARISON_HANDLER(FCMP_OEQ, float_ordered_equal)
  LOAD_COMPARISON_HANDLER(FCMP_ONE, float_ordered_not_equal)
  LOAD_COMPARISON_HANDLER(FCMP_ORD, float_ordered)
  LOAD_COMPARISON_HANDLER(FCMP_UNO, float_unordered)
  LOAD_COMPARISON_HANDLER(FCMP_UGT, float_unordered_greater_than)
  LOAD_COMPARISON_HANDLER(FCMP_UGE, float_unordered_greater_equal)
  LOAD_COMPARISON_HANDLER(FCMP_ULT, float_unordered_less_than)
  LOAD_COMPARISON_HANDLER(FCMP_ULE, float_unordered_less_equal)
  LOAD_COMPARISON_HANDLER(FCMP_UEQ, float_unordered_equal)
  LOAD_COMPARISON_HANDLER(FCMP_UNE, float_unordered_not_equal)

#undef LOAD_COMPARISON_HANDLER

  memcpy = M.getOrInsertFunction("_sym_memcpy", voidT, ptrT, ptrT, intPtrType);
  memset =
      M.getOrInsertFunction("_sym_memset", voidT, ptrT, ptrT, IRB.getInt64Ty());
  memmove =
      M.getOrInsertFunction("_sym_memmove", voidT, ptrT, ptrT, intPtrType);
  readMemory = M.getOrInsertFunction("_sym_read_memory", ptrT, intPtrType,
                                     intPtrType, int8T);
  writeMemory = M.getOrInsertFunction("_sym_write_memory", voidT, intPtrType,
                                      intPtrType, ptrT, int8T);
  buildExtract =
      M.getOrInsertFunction("_sym_build_extract", ptrT, ptrT, IRB.getInt64Ty(),
                            IRB.getInt64Ty(), int8T);
}

/// Decide whether a function is called symbolically.
bool isInterceptedFunction(const Function &f) {
  static const StringSet<> kInterceptedFunctions = {
      "malloc", "calloc",  "mmap",   "read",   "getc",   "memcpy",
      "memset", "strncpy", "strchr", "memcmp", "memmove"};

  return (kInterceptedFunctions.count(f.getName()) > 0);
}
