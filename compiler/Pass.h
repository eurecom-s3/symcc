#ifndef PASS_H
#define PASS_H

#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/ValueMap.h>
#include <llvm/Pass.h>

class SymbolizePass : public llvm::FunctionPass {
public:
  static char ID;

  SymbolizePass() : FunctionPass(ID) {}

  bool doInitialization(llvm::Module &M) override;
  bool runOnFunction(llvm::Function &F) override;

private:
  static constexpr char kSymCtorName[] = "__sym_ctor";

  /// Mapping from global variables to their corresponding symbolic expressions.
  llvm::ValueMap<llvm::GlobalVariable *, llvm::GlobalVariable *>
      globalExpressions;
};

#endif
