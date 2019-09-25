#ifndef RUNTIME_H
#define RUNTIME_H

#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Module.h>

/// Runtime functions
struct Runtime {
  Runtime(llvm::Module &M);

  llvm::Value *buildInteger{};
  llvm::Value *buildInteger128{};
  llvm::Value *buildFloat{};
  llvm::Value *buildNullPointer{};
  llvm::Value *buildTrue{};
  llvm::Value *buildFalse{};
  llvm::Value *buildBool{};
  llvm::Value *buildNeg{};
  llvm::Value *buildSExt{};
  llvm::Value *buildZExt{};
  llvm::Value *buildTrunc{};
  llvm::Value *buildIntToFloat{};
  llvm::Value *buildFloatToFloat{};
  llvm::Value *buildBitsToFloat{};
  llvm::Value *buildFloatToBits{};
  llvm::Value *buildFloatToSignedInt{};
  llvm::Value *buildFloatToUnsignedInt{};
  llvm::Value *buildFloatAbs{};
  llvm::Value *buildBoolAnd{};
  llvm::Value *buildBoolOr{};
  llvm::Value *buildBoolXor{};
  llvm::Value *buildBoolToBits{};
  llvm::Value *pushPathConstraint{};
  llvm::Value *getParameterExpression{};
  llvm::Value *setParameterExpression{};
  llvm::Value *setReturnExpression{};
  llvm::Value *getReturnExpression{};
  llvm::Value *memcpy{};
  llvm::Value *memset{};
  llvm::Value *memmove{};
  llvm::Value *readMemory{};
  llvm::Value *writeMemory{};
  llvm::Value *buildExtract{};

  /// Mapping from icmp predicates to the functions that build the corresponding
  /// symbolic expressions.
  std::array<llvm::Value *, llvm::CmpInst::BAD_ICMP_PREDICATE>
      comparisonHandlers{};

  /// Mapping from binary operators to the functions that build the
  /// corresponding symbolic expressions.
  std::array<llvm::Value *, llvm::Instruction::BinaryOpsEnd>
      binaryOperatorHandlers{};
};

bool isInterceptedFunction(const llvm::Function &f);

#endif
