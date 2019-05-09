#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/ValueMap.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include <llvm/Transforms/Utils/ValueMapper.h>

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

private:
  static constexpr char kSymCtorName[] = "__sym_ctor";

  //
  // Runtime functions
  //

  Value *buildInteger;
  Value *buildNeg;
  Value *buildSExt;
  Value *buildZExt;
  Value *pushPathConstraint;
  Value *getParameterExpression;
  Value *setParameterExpression;
  Value *setReturnExpression;
  Value *getReturnExpression;

  /// Mapping from icmp predicates to the functions that build the corresponding
  /// symbolic expressions.
  std::array<Value *, CmpInst::BAD_ICMP_PREDICATE> comparisonHandlers;

  /// Mapping from binary operators to the functions that build the
  /// corresponding symbolic expressions.
  std::array<Value *, Instruction::BinaryOpsEnd> binaryOperatorHandlers;

  /// Mapping from global variables to their corresponding symbolic expressions.
  ValueMap<GlobalVariable *, GlobalVariable *> globalExpressions;

  friend class Symbolizer;
};

class Symbolizer : public InstVisitor<Symbolizer> {
public:
  explicit Symbolizer(const SymbolizePass &symPass) : SP(symPass) {}

  /// Load or create the symbolic expression for a value.
  Value *getOrCreateSymbolicExpression(Value *V, IRBuilder<> &IRB) {
    if (auto exprIt = symbolicExpressions.find(V);
        exprIt != symbolicExpressions.end()) {
      return exprIt->second;
    }

    if (auto global = dyn_cast<GlobalVariable>(V)) {
      if (auto exprIt = SP.globalExpressions.find(global);
          exprIt != SP.globalExpressions.end()) {
        return exprIt->second;
      }
    }

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
    } else if (auto gep = dyn_cast<GEPOperator>(V)) {
      // TODO can we assume this is always a global value?
      auto baseExpr = cast<GlobalValue>(
          getOrCreateSymbolicExpression(gep->getPointerOperand(), IRB));
      SmallVector<Value *, kExpectedMaxGEPIndices> indices(gep->idx_begin(),
                                                           gep->idx_end());
      ret = ConstantExpr::getGetElementPtr(baseExpr->getValueType(), baseExpr,
                                           indices, gep->isInBounds(),
                                           gep->getInRangeIndex());
    }

    if (ret == nullptr) {
      DEBUG(errs() << "Unable to obtain a symbolic expression for " << *V
                   << '\n');
      assert(!"No symbolic expression for value");
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
    Value *handler = SP.binaryOperatorHandlers.at(I.getOpcode());
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
    IRB.CreateCall(SP.pushPathConstraint,
                   {getOrCreateSymbolicExpression(I.getCondition(), IRB),
                    I.getCondition()});
    symbolicExpressions[&I] = IRB.CreateSelect(
        I.getCondition(), getOrCreateSymbolicExpression(I.getTrueValue(), IRB),
        getOrCreateSymbolicExpression(I.getFalseValue(), IRB));
  }

  void visitICmpInst(ICmpInst &I) {
    // ICmp is integer comparison; we simply include it in the resulting
    // expression.

    IRBuilder<> IRB(&I);
    Value *handler = SP.comparisonHandlers.at(I.getPredicate());
    assert(handler && "Unable to handle icmp variant");
    symbolicExpressions[&I] = IRB.CreateCall(
        handler, {getOrCreateSymbolicExpression(I.getOperand(0), IRB),
                  getOrCreateSymbolicExpression(I.getOperand(1), IRB)});
  }

  void visitReturnInst(ReturnInst &I) {
    // Upon return, we just store the expression for the return value.

    if (I.getReturnValue() == nullptr)
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

    IRBuilder<> IRB(&I);
    IRB.CreateCall(SP.pushPathConstraint,
                   {getOrCreateSymbolicExpression(I.getCondition(), IRB),
                    I.getCondition()});
  }

  void visitCallInst(CallInst &I) {
    // TODO handle indirect calls
    // TODO handle calls to intrinsics
    // TODO prevent instrumentation of our own functions with attributes

    Function *callee = I.getCalledFunction();
    bool isIndirect = (callee == nullptr);
    if (isIndirect) {
      errs()
          << "Warning: losing track of symbolic expressions at indirect call "
          << I << '\n';
      return;
    }

    bool isBuildVariable = (callee->getName() == "_sym_build_variable");
    bool isSymRuntimeFunction = callee->getName().startswith("_sym_");
    if (!isBuildVariable && isSymRuntimeFunction)
      return;
    if (callee->isIntrinsic()) {
      errs() << "Warning: unhandled LLVM intrinsic " << callee->getName()
             << '\n';
      return;
    }

    IRBuilder<> IRB(&I);

    if (!isBuildVariable) {
      for (Use &arg : I.args())
        IRB.CreateCall(SP.setParameterExpression,
                       {ConstantInt::get(IRB.getInt8Ty(), arg.getOperandNo()),
                        getOrCreateSymbolicExpression(arg, IRB)});
    }

    IRB.SetInsertPoint(I.getNextNonDebugInstruction());
    // TODO get the expression only if the function set one
    symbolicExpressions[&I] = IRB.CreateCall(SP.getReturnExpression);
  }

  void visitAllocaInst(AllocaInst &I) {
    if (auto size = dyn_cast<ConstantInt>(I.getArraySize());
        (size == nullptr) || !size->isOne()) {
      errs() << "Warning: stack-allocated arrays are not supported yet\n";
      return;
    }

    IRBuilder<> IRB(&I);
    symbolicExpressions[&I] = IRB.CreateAlloca(IRB.getInt8PtrTy());
  }

  void visitLoadInst(LoadInst &I) {
    IRBuilder<> IRB(&I);
    symbolicExpressions[&I] = IRB.CreateLoad(
        getOrCreateSymbolicExpression(I.getPointerOperand(), IRB));
  }

  void visitStoreInst(StoreInst &I) {
    IRBuilder<> IRB(&I);
    IRB.CreateStore(getOrCreateSymbolicExpression(I.getValueOperand(), IRB),
                    getOrCreateSymbolicExpression(I.getPointerOperand(), IRB));
  }

  void visitBitCastInst(BitCastInst &I) {
    if (!I.getSrcTy()->isPointerTy() || !I.getDestTy()->isPointerTy()) {
      errs() << "Warning: unhandled non-pointer bit cast " << I << '\n';
      return;
    }

    IRBuilder<> IRB(&I);
    symbolicExpressions[&I] =
        getOrCreateSymbolicExpression(I.getOperand(0), IRB);
  }

  void visitGetElementPtrInst(GetElementPtrInst &I) {
    IRBuilder<> IRB(&I);
    SmallVector<Value *, kExpectedMaxGEPIndices> indices(I.idx_begin(),
                                                         I.idx_end());
    symbolicExpressions[&I] = IRB.CreateGEP(
        getOrCreateSymbolicExpression(I.getPointerOperand(), IRB), indices);
  }

  void visitCastInst(CastInst &I) {
    auto opcode = I.getOpcode();
    if (opcode != Instruction::SExt && opcode != Instruction::ZExt) {
      errs() << "Warning: unhandled cast instruction " << I << '\n';
      return;
    }

    IRBuilder<> IRB(&I);

    // LLVM bitcode represents Boolean values as i1. In Z3, those are a not a
    // bit-vector sort, so trying to cast one into a bit vector of any length
    // raises an error. For now, we follow the heuristic that i1 is always a
    // Boolean and thus does not need extension on the Z3 side.
    if (I.getSrcTy()->getIntegerBitWidth() == 1) {
      symbolicExpressions[&I] =
          getOrCreateSymbolicExpression(I.getOperand(0), IRB);
    } else {
      Value *target;

      // TODO use array indexed with cast opcodes
      switch (I.getOpcode()) {
      case Instruction::SExt:
        target = SP.buildSExt;
        break;
      case Instruction::ZExt:
        target = SP.buildZExt;
        break;
      default:
        llvm_unreachable("Unknown cast opcode");
      }

      symbolicExpressions[&I] = IRB.CreateCall(
          target, {getOrCreateSymbolicExpression(I.getOperand(0), IRB),
                   IRB.getInt8(I.getDestTy()->getIntegerBitWidth() -
                               I.getSrcTy()->getIntegerBitWidth())});
    }
  }

  void visitInstruction(Instruction &I) {
    errs() << "Warning: unknown instruction " << I << '\n';
  }

private:
  static constexpr int kExpectedMaxGEPIndices = 5;

  const SymbolizePass &SP;

  /// Mapping from SSA values to symbolic expressions.
  ///
  /// For pointer values, the stored value is not an expression but a pointer to
  /// the expression of the referenced value.
  ValueMap<Value *, Value *> symbolicExpressions;
};

void addSymbolizePass(const PassManagerBuilder & /* unused */,
                      legacy::PassManagerBase &PM) {
  PM.add(new SymbolizePass());
}

/// Return the appropriate type for storing symbolic expressions.
Type *expressionType(Type *type, LLVMContext &ctx) {
  // TODO handle struct types
  if (type->isIntegerTy()) {
    return Type::getInt8PtrTy(ctx);
  }

  if (type->isArrayTy()) {
    return ArrayType::get(expressionType(type->getArrayElementType(), ctx),
                          type->getArrayNumElements());
  }

  errs() << "Warning: cannot determine expression type for " << type << '\n';
  llvm_unreachable("Unable to determine expression type");
}

} // end of anonymous namespace

char SymbolizePass::ID = 0;

// Make the pass known to opt.
static RegisterPass<SymbolizePass> X("symbolize", "Symbolization Pass");
// Tell frontends to run the pass automatically.
static struct RegisterStandardPasses Y(PassManagerBuilder::EP_EarlyAsPossible,
                                       addSymbolizePass);

bool SymbolizePass::doInitialization(Module &M) {
  DEBUG(errs() << "Symbolizer module init\n");

  IRBuilder<> IRB(M.getContext());
  buildInteger = M.getOrInsertFunction("_sym_build_integer", IRB.getInt8PtrTy(),
                                       IRB.getInt64Ty(), IRB.getInt8Ty());
  buildNeg = M.getOrInsertFunction("_sym_build_neg", IRB.getInt8PtrTy(),
                                   IRB.getInt8PtrTy());
  buildSExt = M.getOrInsertFunction("_sym_build_sext", IRB.getInt8PtrTy(),
                                    IRB.getInt8PtrTy(), IRB.getInt8Ty());
  buildZExt = M.getOrInsertFunction("_sym_build_zext", IRB.getInt8PtrTy(),
                                    IRB.getInt8PtrTy(), IRB.getInt8Ty());
  pushPathConstraint =
      M.getOrInsertFunction("_sym_push_path_constraint", IRB.getInt8PtrTy(),
                            IRB.getInt8PtrTy(), IRB.getInt1Ty());

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

  // TODO make sure that we use the correct variant (signed or unsigned)
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
  LOAD_COMPARISON_HANDLER(ICMP_NE, not_equal)
  LOAD_COMPARISON_HANDLER(ICMP_UGT, unsigned_greater_than)
  LOAD_COMPARISON_HANDLER(ICMP_UGE, unsigned_greater_equal)
  LOAD_COMPARISON_HANDLER(ICMP_ULT, unsigned_less_than)
  LOAD_COMPARISON_HANDLER(ICMP_ULE, unsigned_less_equal)
  LOAD_COMPARISON_HANDLER(ICMP_SGT, signed_greater_than)
  LOAD_COMPARISON_HANDLER(ICMP_SGE, signed_greater_equal)
  LOAD_COMPARISON_HANDLER(ICMP_SLT, signed_less_than)
  LOAD_COMPARISON_HANDLER(ICMP_SLE, signed_less_equal)

#undef LOAD_COMPARISON_HANDLER

  // For each global variable, we need another global variable that holds the
  // corresponding symbolic expression.
  for (auto &global : M.globals()) {
    auto exprType = expressionType(global.getValueType(), M.getContext());
    // The expression has to be initialized at run time and can therefore never
    // be constant, even if the value that it represents is.
    globalExpressions[&global] =
        new GlobalVariable(M, exprType, false, global.getLinkage(),
                           Constant::getNullValue(exprType),
                           global.getName() + ".sym_expr", &global);
  }

  // Insert a constructor that initializes the runtime and any globals.
  Function *ctor;
  std::tie(ctor, std::ignore) = createSanitizerCtorAndInitFunctions(
      M, kSymCtorName, "_sym_initialize", {}, {});
  IRB.SetInsertPoint(ctor->getEntryBlock().getTerminator());
  auto initializeGlobal = M.getOrInsertFunction(
      "_sym_initialize_array", IRB.getVoidTy(), IRB.getInt8PtrTy(),
      IRB.getInt8PtrTy(), IRB.getInt64Ty());
  for (auto &&[value, expression] : globalExpressions) {
    // TODO handle non-array globals and non-char arrays
    auto valueType = value->getValueType();
    // errs() << *value << " - " << *valueType << '\n';
    if (valueType->isIntegerTy()) {
      auto intValue = IRB.CreateLoad(value);
      auto intExpr = IRB.CreateCall(
          buildInteger,
          {intValue, IRB.getInt8(valueType->getIntegerBitWidth())});
      IRB.CreateStore(intExpr, expression);
    } else {
      IRB.CreateCall(
          initializeGlobal,
          {expression, value, IRB.getInt64(valueType->getArrayNumElements())});
    }
  }
  appendToGlobalCtors(M, ctor, 0);

  return true;
}

bool SymbolizePass::runOnFunction(Function &F) {
  if (F.getName() == kSymCtorName)
    return false;

  DEBUG(errs() << "Symbolizing function ");
  DEBUG(errs().write_escaped(F.getName()) << '\n');

  Symbolizer symbolizer(*this);
  symbolizer.visit(F);
  // DEBUG(errs() << F << '\n');

  return true; // TODO be more specific
}
