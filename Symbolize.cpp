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

  Value *initializeRuntime;
  Value *buildInteger;
  Value *buildNeg;
  Value *pushPathConstraint;
  Value *getParameterExpression;
  Value *setParameterExpression;
  Value *setReturnExpression;
  Value *getReturnExpression;

  /// Mapping from icmp predicates to the functions that build the corresponding
  /// symbolic expressions.
  Value *comparisonHandlers[CmpInst::BAD_ICMP_PREDICATE];

  /// Mapping from binary operators to the functions that build the
  /// corresponding symbolic expressions.
  Value *binaryOperatorHandlers[Instruction::BinaryOpsEnd];
};

class Symbolizer : public InstVisitor<Symbolizer> {
public:
  Symbolizer(SymbolizePass &symPass) : SP(symPass) {}

  /// Load or create the symbolic expression for a value.
  Value *getOrCreateSymbolicExpression(Value *V, IRBuilder<> &IRB) {
    if (auto exprIt = symbolicExpressions.find(V);
        exprIt != symbolicExpressions.end())
      return exprIt->second;

    Value *ret = nullptr;

    if (auto C = dyn_cast<ConstantInt>(V)) {
      // TODO is sign extension always correct?
      ret =
          IRB.CreateCall(SP.buildInteger,
                         {IRB.CreateSExt(C, IRB.getInt64Ty()),
                          ConstantInt::get(IRB.getInt8Ty(), C->getBitWidth())});
    } else if (auto A = dyn_cast<Argument>(V)) {
      ret = IRB.CreateCall(SP.getParameterExpression,
                           ConstantInt::get(IRB.getInt8Ty(), A->getArgNo()));
    }

    if (!ret) {
      DEBUG(errs() << "Unable to obtain a symbolic expression for " << *V
                   << '\n');
      assert(!"No symbolic expression for value");
      // return ConstantPointerNull::get(IRB.getInt8PtrTy());
    }

    symbolicExpressions[V] = ret;
    return ret;
  }

  //
  // Implementation of InstVisitor
  //

  void visitBinaryOperator(BinaryOperator &I) {
    // Binary operators propagate into the symbolic expression.

    IRBuilder<> IRB(&I);
    Value *handler = SP.binaryOperatorHandlers[I.getOpcode()];
    assert(handler && "Unable to handle binary operator");
    symbolicExpressions[&I] = IRB.CreateCall(
        handler, {getOrCreateSymbolicExpression(I.getOperand(0), IRB),
                  getOrCreateSymbolicExpression(I.getOperand(1), IRB)});
  }

  void visitSelectInst(SelectInst &I) {
    // Select is like the ternary operator ("?:") in C. We push the (potentially
    // negated) condition to the path constraints and copy the symbolic
    // expression over from the chosen argument.

    // TODO The code we generate here causes the negation to be built in any
    // case, even when it's not used. Is this cheaper than a conditional branch?

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

  void visitReturnInst(ReturnInst &I) {
    // Upon return, we just store the expression for the return value.

    if (!I.getReturnValue())
      return;

    IRBuilder<> IRB(&I);
    IRB.CreateCall(SP.setReturnExpression,
                   getOrCreateSymbolicExpression(I.getReturnValue(), IRB));
  }

  void visitBranchInst(BranchInst &I) {
    // Br can jump conditionally or unconditionally. We are only interested in
    // the former case, in which we push the branch condition or its negation to
    // the path constraints.

    if (I.isUnconditional())
      return;

    // It seems that Clang can't optimize the sequence "call buildNeg; select;
    // call pushPathConstraint; br", failing to move the first call to the
    // negative branch. Therefore, we insert calls directly into the two target
    // blocks.

    IRBuilder<> IRB(I.getSuccessor(0)->getFirstNonPHI());
    IRB.CreateCall(SP.pushPathConstraint,
                   getOrCreateSymbolicExpression(I.getCondition(), IRB));
    IRB.SetInsertPoint(I.getSuccessor(1)->getFirstNonPHI());
    auto negatedCondition = IRB.CreateCall(
        SP.buildNeg, getOrCreateSymbolicExpression(I.getCondition(), IRB));
    IRB.CreateCall(SP.pushPathConstraint, negatedCondition);
  }

  void visitCallInst(CallInst &I) {
    // TODO handle indirect calls
    // TODO prevent instrumentation of our own functions with attributes

    Function *callee = I.getCalledFunction();
    bool isIndirect = !callee;
    // TODO find a better way to detect external functions
    bool isExternal = !callee->getInstructionCount();
    if (isIndirect || isExternal)
      return;

    errs() << "Found call: " << I << '\n';

    IRBuilder<> IRB(&I);
    for (Use &arg : I.args())
      IRB.CreateCall(SP.setParameterExpression,
                     {ConstantInt::get(IRB.getInt8Ty(), arg.getOperandNo()),
                      getOrCreateSymbolicExpression(arg, IRB)});

    IRB.SetInsertPoint(I.getNextNonDebugInstruction());
    symbolicExpressions[&I] = IRB.CreateCall(SP.getReturnExpression);
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
  initializeRuntime = M.getOrInsertFunction("_sym_initialize", IRB.getVoidTy());
  buildInteger = M.getOrInsertFunction("_sym_build_integer", IRB.getInt8PtrTy(),
                                       IRB.getInt64Ty(), IRB.getInt8Ty());
  buildNeg = M.getOrInsertFunction("_sym_build_neg", IRB.getInt8PtrTy(),
                                   IRB.getInt8PtrTy());
  pushPathConstraint = M.getOrInsertFunction(
      "_sym_push_path_constraint", IRB.getVoidTy(), IRB.getInt8PtrTy());

  setParameterExpression =
      M.getOrInsertFunction("_sym_set_parameter_expression", IRB.getVoidTy(),
                            IRB.getInt8Ty(), IRB.getInt8PtrTy());
  getParameterExpression = M.getOrInsertFunction(
      "_sym_get_parameter_expression", IRB.getInt8PtrTy(), IRB.getInt8Ty());
  setReturnExpression = M.getOrInsertFunction(
      "_sym_set_return_expression", IRB.getVoidTy(), IRB.getInt8PtrTy());
  getReturnExpression =
      M.getOrInsertFunction("_sym_get_return_expression", IRB.getInt8PtrTy());

#define LOAD_BINARY_OPERATOR_HANDLER(constant, name)                           \
  binaryOperatorHandlers[Instruction::constant] =                              \
      M.getOrInsertFunction("_sym_build_" #name, IRB.getInt8PtrTy(),           \
                            IRB.getInt8PtrTy(), IRB.getInt8PtrTy());

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

#undef LOAD_BINARY_OPERATOR_HANDLER

#define LOAD_COMPARISON_HANDLER(constant, name)                                \
  comparisonHandlers[CmpInst::constant] =                                      \
      M.getOrInsertFunction("_sym_build_" #name, IRB.getInt8PtrTy(),           \
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
  DEBUG(errs() << "Symbolizing function ");
  DEBUG(errs().write_escaped(F.getName()) << '\n');

  if (F.getName() == "main") {
    IRBuilder<> IRB(F.getEntryBlock().getFirstNonPHI());
    IRB.CreateCall(initializeRuntime);
  }

  Symbolizer symbolizer(*this);
  symbolizer.visit(F);
  DEBUG(errs() << F << '\n');

  return true; // TODO be more specific
}
