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

  //
  // Runtime functions
  //

  Value *buildInteger;
  Value *buildAdd;
  Value *buildNeg;
  Value *pushPathConstraint;

  /// Mapping from icmp predicates to the functions that build the corresponding
  /// symbolic expressions.
  Value *comparisonHandlers[CmpInst::BAD_ICMP_PREDICATE];
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
    // return ConstantPointerNull::get(IRB.getInt8PtrTy());
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
    // Binary operators propagate into the symbolic expression.
    if (I.getOpcode() == Instruction::Add) {
      IRBuilder<> IRB(&I);
      symbolicExpressions[&I] = IRB.CreateCall(
          SP.buildAdd, {getOrCreateSymbolicExpression(I.getOperand(0), IRB),
                        getOrCreateSymbolicExpression(I.getOperand(1), IRB)});
    }
  }

  void visitSelectInst(SelectInst &I) {
    // Select is like the ternary operator ("?:") in C. We push the (potentially
    // negated) condition to the path constraints and copy the symbolic
    // expression over from the chosen argument.

    IRBuilder<> IRB(&I);
    auto condition = getOrCreateSymbolicExpression(I.getCondition(), IRB);
    auto negatedCondition =
        IRB.CreateCall(SP.buildNeg, condition, "negated_condition");
    auto newConstraint = IRB.CreateSelect(
        I.getCondition(), condition, negatedCondition, "new_path_constraint");
    IRB.CreateCall(SP.pushPathConstraint, newConstraint);
    symbolicExpressions[&I] = IRB.CreateSelect(
        I.getCondition(), getOrCreateSymbolicExpression(I.getTrueValue(), IRB),
        getOrCreateSymbolicExpression(I.getFalseValue(), IRB));
  }

  void visitICmpInst(ICmpInst &I) {
    // ICmp is integer comparison; we simply include it in the resulting
    // expression.

    IRBuilder<> IRB(&I);
    Value *handler = SP.comparisonHandlers[I.getPredicate()];
    assert(handler && "Unable to handle icmp variant");
    symbolicExpressions[&I] = IRB.CreateCall(
        handler, {getOrCreateSymbolicExpression(I.getOperand(0), IRB),
                  getOrCreateSymbolicExpression(I.getOperand(1), IRB)});
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
  buildNeg = M.getOrInsertFunction("__sym_build_neg", IRB.getInt8PtrTy(),
                                   IRB.getInt8PtrTy());
  pushPathConstraint = M.getOrInsertFunction(
      "__sym_push_path_constraint", IRB.getVoidTy(), IRB.getInt8PtrTy());

#define LOAD_COMPARISON_HANDLER(constant, name)                                \
  comparisonHandlers[CmpInst::constant] =                                      \
      M.getOrInsertFunction("__sym_build_" #name, IRB.getInt8PtrTy(),          \
                            IRB.getInt8PtrTy(), IRB.getInt8PtrTy());

  LOAD_COMPARISON_HANDLER(ICMP_EQ, equal)
  LOAD_COMPARISON_HANDLER(ICMP_UGT, unsigned_greater_than)
  LOAD_COMPARISON_HANDLER(ICMP_UGE, unsigned_greater_equal)
  LOAD_COMPARISON_HANDLER(ICMP_ULT, unsigned_less_than)
  LOAD_COMPARISON_HANDLER(ICMP_ULE, unsigned_less_equal)
  LOAD_COMPARISON_HANDLER(ICMP_SGT, signed_greater_than)
  LOAD_COMPARISON_HANDLER(ICMP_SGE, signed_greater_equal)
  LOAD_COMPARISON_HANDLER(ICMP_SLT, signed_less_than)
  LOAD_COMPARISON_HANDLER(ICMP_SLE, signed_less_equal)

#undef LOAD_COMPARISON_HANDLER

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
