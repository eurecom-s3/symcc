#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/ValueMap.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>

#define DEBUG(X)                                                               \
  do {                                                                         \
    X;                                                                         \
  } while (false)

using namespace llvm;

namespace {

class SymbolizePass : public FunctionPass {
public:
  static char ID;

  SymbolizePass() : FunctionPass(ID) {}
  bool doInitialization(Module &M) override;
  bool runOnFunction(Function &F) override;

  // Runtime functions
  Value *buildInteger;
  Value *buildAdd;
};

class Symbolizer : public InstVisitor<Symbolizer> {
public:
  Symbolizer(SymbolizePass &symPass) : SP(symPass) {}

  /// Load or create the symbolic expression for a value.
  Value *getOrCreateSymbolicExpression(Value *V, IRBuilder<> &IRB) {
    if (auto exprIt = symbolicExpressions.find(V);
        exprIt != symbolicExpressions.end())
      return exprIt->second;

    if (auto C = dyn_cast<ConstantInt>(V)) {
      // TODO is sign extension always correct?
      auto expr =
          IRB.CreateCall(SP.buildInteger,
                         {IRB.CreateSExt(C, IRB.getInt64Ty()),
                          ConstantInt::get(IRB.getInt8Ty(), C->getBitWidth())});
      symbolicExpressions[V] = expr;
      return expr;
    }

    DEBUG(errs() << "Unable to obtain a symbolic expression for " << *V
                 << '\n');
    assert(!"No symbolic expression for value");
  }

  /// Create symbolic expressions for the function's arguments.
  void symbolizeArguments(Function &F) {
    // TODO get expressions from function parameters

    IRBuilder<> IRB(F.getEntryBlock().getFirstNonPHI());
    for (auto arg = F.arg_begin(), arg_end = F.arg_end(); arg != arg_end;
         arg++) {
      symbolicExpressions[arg] =
          // IRB.CreateAlloca(IRB.getInt8PtrTy(), nullptr,
          //                  "expression_arg_" + Twine(arg->getArgNo()));
          ConstantPointerNull::get(IRB.getInt8PtrTy());
    }
  }

  //
  // Implementation of InstVisitor
  //

  void visitBinaryOperator(BinaryOperator &I) {
    if (I.getOpcode() == Instruction::Add) {
      errs() << "Found the addition!\n"
             << *I.getOperand(0) << '\n'
             << *I.getOperand(1) << '\n';

      IRBuilder<> IRB(&I);
      symbolicExpressions[&I] = IRB.CreateCall(
          SP.buildAdd, {getOrCreateSymbolicExpression(I.getOperand(0), IRB),
                        getOrCreateSymbolicExpression(I.getOperand(1), IRB)});
    }
  }

private:
  SymbolizePass &SP;
  ValueMap<Value *, Value *> symbolicExpressions;
};

} // end of anonymous namespace

char SymbolizePass::ID = 0;

static RegisterPass<SymbolizePass> X("symbolize", "Symbolization Pass");

bool SymbolizePass::doInitialization(Module &M) {
  DEBUG(errs() << "Symbolizer module init\n");

  IRBuilder<> IRB(M.getContext());
  buildInteger =
      M.getOrInsertFunction("__sym_build_integer", IRB.getInt8PtrTy(),
                            IRB.getInt64Ty(), IRB.getInt8Ty());
  buildAdd = M.getOrInsertFunction("__sym_build_add", IRB.getInt8PtrTy(),
                                   IRB.getInt8PtrTy(), IRB.getInt8PtrTy());
  return true;
}

bool SymbolizePass::runOnFunction(Function &F) {
  // TODO handle main later
  if (F.getName() == "main")
    return false;

  DEBUG(errs() << "Symbolizing function ");
  DEBUG(errs().write_escaped(F.getName()) << '\n');

  Symbolizer symbolizer(*this);
  symbolizer.symbolizeArguments(F);
  symbolizer.visit(F);
  DEBUG(errs() << F << '\n');

  return true; // TODO be more specific
}
