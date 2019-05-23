#include <llvm/IR/Function.h>
#include <llvm/IR/GetElementPtrTypeIterator.h>
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

  /// Generate code to initialize the expression corresponding to a global
  /// variable.
  void buildGlobalInitialization(Value *expression, Value *value,
                                 IRBuilder<> &IRB);

  //
  // Runtime functions
  //

  Value *buildInteger{};
  Value *buildFloat{};
  Value *buildNullPointer{};
  Value *buildTrue{};
  Value *buildFalse{};
  Value *buildNeg{};
  Value *buildSExt{};
  Value *buildZExt{};
  Value *buildTrunc{};
  Value *buildIntToFloat{};
  Value *buildFloatToFloat{};
  Value *buildBitsToFloat{};
  Value *buildFloatToBits{};
  Value *buildFloatToSignedInt{};
  Value *buildFloatToUnsignedInt{};
  Value *pushPathConstraint{};
  Value *getParameterExpression{};
  Value *setParameterExpression{};
  Value *setReturnExpression{};
  Value *getReturnExpression{};
  Value *initializeArray8{};
  Value *initializeArray16{};
  Value *initializeArray32{};
  Value *initializeArray64{};
  Value *memcpy{};
  Value *registerMemory{};
  Value *readMemory{};
  Value *writeMemory{};
  Value *initializeMemory{};
  Value *buildExtract{};

  /// Mapping from icmp predicates to the functions that build the corresponding
  /// symbolic expressions.
  std::array<Value *, CmpInst::BAD_ICMP_PREDICATE> comparisonHandlers{};

  /// Mapping from binary operators to the functions that build the
  /// corresponding symbolic expressions.
  std::array<Value *, Instruction::BinaryOpsEnd> binaryOperatorHandlers{};

  /// Mapping from global variables to their corresponding symbolic expressions.
  ValueMap<GlobalVariable *, GlobalVariable *> globalExpressions;

  /// The data layout of the currently processed module.
  const DataLayout *dataLayout;

  /// An integer type at least as wide as a pointer.
  IntegerType *intPtrType{};

  /// The width in bits of pointers in the module.
  unsigned ptrBits{};

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

    Value *ret = nullptr;

    if (isa<ConstantData>(V)) {
      // Constants may be used in multiple places throughout a function.
      // Ideally, we'd make sure that in such cases the symbolic expression is
      // generated as early as necessary but no earlier. For now, we just create
      // it at the very beginning of the function.

      auto oldInsertionPoint = IRB.saveIP();
      IRB.SetInsertPoint(oldInsertionPoint.getBlock()
                             ->getParent()
                             ->getEntryBlock()
                             .getFirstNonPHI());

      if (auto C = dyn_cast<ConstantInt>(V)) {
        // Special case: LLVM uses the type i1 to represent Boolean values, but
        // for Z3 we have to create expressions of a separate sort.
        if (C->getBitWidth() == 1) {
          ret = IRB.CreateCall(C->isOne() ? SP.buildTrue : SP.buildFalse, {});
        } else {
          ret = IRB.CreateCall(SP.buildInteger,
                               {IRB.CreateZExt(C, IRB.getInt64Ty()),
                                IRB.getInt8(C->getBitWidth())});
        }
      } else if (auto F = dyn_cast<ConstantFP>(V)) {
        ret = IRB.CreateCall(SP.buildFloat,
                             {IRB.CreateFPExt(F, IRB.getDoubleTy()),
                              IRB.getInt1(F->getType()->isDoubleTy())});
      }

      IRB.restoreIP(oldInsertionPoint);
    } else if (auto A = dyn_cast<Argument>(V)) {
      if (A->getParent()->getName() == "main") {
        // We don't have symbolic parameters in main.
        // TODO fix when we have a symbolic libc
        if (A->getType()->isIntegerTy()) {
          ret = IRB.CreateCall(
              SP.buildInteger,
              {IRB.CreateZExt(A, IRB.getInt64Ty()),
               ConstantInt::get(IRB.getInt8Ty(),
                                A->getType()->getIntegerBitWidth())});
        } else if (A->getType()->isPointerTy()) {
          ret = IRB.CreateCall(SP.buildInteger,
                               {IRB.CreatePtrToInt(A, SP.intPtrType),
                                ConstantInt::get(IRB.getInt8Ty(), SP.ptrBits)});
        } else {
          llvm_unreachable("Unknown argument type for main");
        }
      } else {
        ret = IRB.CreateCall(SP.getParameterExpression,
                             ConstantInt::get(IRB.getInt8Ty(), A->getArgNo()));
      }
    } else if (auto gep = dyn_cast<GEPOperator>(V)) {
      ret = handleGEPOperator(*gep, IRB);
    } else if (auto bc = dyn_cast<BitCastOperator>(V)) {
      ret = handleBitCastOperator(*bc, IRB);
    } else if (auto gv = dyn_cast<GlobalValue>(V)) {
      ret = IRB.CreateCall(SP.buildInteger,
                           {IRB.CreatePtrToInt(gv, SP.intPtrType),
                            ConstantInt::get(IRB.getInt8Ty(), SP.ptrBits)});
    } else if (isa<ConstantPointerNull>(V)) {
      // Return immediately to avoid caching. The null pointer may be used in
      // multiple unrelated places, so we either have to load it early enough in
      // the function or reload it every time.
      return IRB.CreateCall(SP.buildNullPointer, {});
    }

    if (ret == nullptr) {
      DEBUG(errs() << "Unable to obtain a symbolic expression for " << *V
                   << '\n');
      assert(!"No symbolic expression for value");
    }

    symbolicExpressions[V] = ret;
    return ret;
  }

  bool isLittleEndian(Type *type) {
    return (!type->isAggregateType() && SP.dataLayout->isLittleEndian());
  }

  //
  // Handling of operators that exist both as instructions and as constant
  // expressions
  //

  Value *handleBitCastOperator(BitCastOperator &I, IRBuilder<> &IRB) {
    assert(I.getSrcTy()->isPointerTy() && I.getDestTy()->isPointerTy() &&
           "Unhandled non-pointer bit cast");
    return getOrCreateSymbolicExpression(I.getOperand(0), IRB);
  }

  Value *handleGEPOperator(GEPOperator &I, IRBuilder<> &IRB) {
    // GEP performs address calculations but never actually accesses memory. In
    // order to represent the result of a GEP symbolically, we start from the
    // symbolic expression of the original pointer and duplicate its
    // computations at the symbolic level.

    auto expr = getOrCreateSymbolicExpression(I.getPointerOperand(), IRB);
    auto pointerSizeValue = ConstantInt::get(IRB.getInt8Ty(), SP.ptrBits);

    for (auto type_it = gep_type_begin(I), type_end = gep_type_end(I);
         type_it != type_end; ++type_it) {
      auto index = type_it.getOperand();

      // There are two cases for the calculation:
      // 1. If the indexed type is a struct, we need to add the offset of the
      //    desired member.
      // 2. If it is an array or a pointer, compute the offset of the desired
      //    element.
      Value *offset;
      if (auto structType = type_it.getStructTypeOrNull()) {
        // Structs can only be indexed with constants
        // (https://llvm.org/docs/LangRef.html#getelementptr-instruction).

        unsigned memberIndex = cast<ConstantInt>(index)->getZExtValue();
        unsigned memberOffset = SP.dataLayout->getStructLayout(structType)
                                    ->getElementOffset(memberIndex);
        offset = IRB.CreateCall(
            SP.buildInteger, {ConstantInt::get(IRB.getInt64Ty(), memberOffset),
                              pointerSizeValue});
      } else {
        if (auto ci = dyn_cast<ConstantInt>(index); ci && ci->isZero()) {
          // Fast path: an index of zero means that no calculations are
          // performed.
          continue;
        }

        // TODO optimize? If the index is constant, we can perform the
        // multiplication ourselves instead of having the solver do it. Also, if
        // the element size is 1, we can omit the multiplication.

        unsigned elementSize =
            SP.dataLayout->getTypeAllocSize(type_it.getIndexedType());
        auto elementSizeExpr = IRB.CreateCall(
            SP.buildInteger, {ConstantInt::get(IRB.getInt64Ty(), elementSize),
                              pointerSizeValue});
        auto indexExpr = getOrCreateSymbolicExpression(index, IRB);
        if (auto indexWidth = index->getType()->getIntegerBitWidth();
            indexWidth != 64) {
          indexExpr = IRB.CreateCall(
              SP.buildZExt,
              {indexExpr, ConstantInt::get(IRB.getInt8Ty(), 64 - indexWidth)});
        }
        offset = IRB.CreateCall(SP.binaryOperatorHandlers[Instruction::Mul],
                                {indexExpr, elementSizeExpr});
      }

      expr = IRB.CreateCall(SP.binaryOperatorHandlers[Instruction::Add],
                            {expr, offset});
    }

    return expr;
  }

  void handleIntrinsicCall(CallInst &I) {
    auto callee = I.getCalledFunction();

    switch (callee->getIntrinsicID()) {
    case Intrinsic::lifetime_start:
    case Intrinsic::lifetime_end:
      // These are safe to ignore.
      break;
    case Intrinsic::memcpy: {
      // auto destExpr = getOrCreateSymbolicExpression(I.getOperand(0), IRB);
      // auto srcExpr = getOrCreateSymbolicExpression(I.getOperand(1), IRB);
      // TODO generate diverging inputs for the source and destination of the
      // copy operation

      IRBuilder<> IRB(&I);
      IRB.CreateCall(SP.memcpy,
                     {I.getOperand(0), I.getOperand(1), I.getOperand(2)});
      break;
    }
    default:
      errs() << "Warning: unhandled LLVM intrinsic " << callee->getName()
             << '\n';
      break;
    }
  }

  void handleRegularCall(CallInst &I) {
    IRBuilder<> IRB(&I);
    auto targetName = I.getCalledFunction()->getName();
    bool isBuildVariable = (targetName == "_sym_build_variable");

    if (targetName.startswith("_sym_") && !isBuildVariable)
      return;

    if (!isBuildVariable) {
      for (Use &arg : I.args())
        IRB.CreateCall(
            SP.setParameterExpression,
            {ConstantInt::get(IRB.getInt8Ty(), arg.getOperandNo()),
             IRB.CreateBitCast(getOrCreateSymbolicExpression(arg, IRB),
                               IRB.getInt8PtrTy())});
    }

    IRB.SetInsertPoint(I.getNextNonDebugInstruction());
    // TODO get the expression only if the function set one
    symbolicExpressions[&I] = IRB.CreateCall(SP.getReturnExpression);
  }

  void handleInlineAssembly(CallInst &I) {
    if (I.getType()->isVoidTy()) {
      errs() << "Warning: skipping over inline assembly " << I << '\n';
      return;
    }

    errs() << "Warning: losing track of symbolic expressions at indirect "
              "call or inline assembly "
           << I << '\n';

    IRBuilder<> IRB(I.getNextNode());
    symbolicExpressions[&I] = IRB.CreateCall(
        SP.buildInteger, {IRB.CreateZExtOrBitCast(&I, IRB.getInt64Ty()),
                          IRB.getInt8(I.getType()->getPrimitiveSizeInBits())});
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

    IRBuilder<> IRB(&I);
    IRB.CreateCall(SP.pushPathConstraint,
                   {getOrCreateSymbolicExpression(I.getCondition(), IRB),
                    I.getCondition()});
    symbolicExpressions[&I] = IRB.CreateSelect(
        I.getCondition(), getOrCreateSymbolicExpression(I.getTrueValue(), IRB),
        getOrCreateSymbolicExpression(I.getFalseValue(), IRB));
  }

  void visitCmpInst(CmpInst &I) {
    // ICmp is integer comparison, FCmp compares floating-point values; we
    // simply include either in the resulting expression.

    IRBuilder<> IRB(&I);
    Value *handler = SP.comparisonHandlers.at(I.getPredicate());
    assert(handler && "Unable to handle icmp/fcmp variant");
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
    // TODO prevent instrumentation of our own functions with attributes

    if (I.isInlineAsm()) {
      handleInlineAssembly(I);
    } else if (I.isIndirectCall()) {
      errs() << "Warning: indirect calls aren't supported yet\n";
    } else if (I.getCalledFunction()->isIntrinsic()) {
      handleIntrinsicCall(I);
    } else {
      handleRegularCall(I);
    }
  }

  void visitAllocaInst(AllocaInst &I) {
    IRBuilder<> IRB(I.getNextNode());
    auto allocationLength =
        SP.dataLayout->getTypeAllocSize(I.getAllocatedType());
    auto shadow =
        IRB.CreateAlloca(ArrayType::get(IRB.getInt8PtrTy(), allocationLength));
    IRB.CreateCall(SP.registerMemory,
                   {IRB.CreatePtrToInt(&I, SP.intPtrType),
                    IRB.CreateBitCast(shadow, IRB.getInt8PtrTy()),
                    ConstantInt::get(SP.intPtrType, allocationLength)});
    symbolicExpressions[&I] = IRB.CreateCall(
        SP.buildInteger, {IRB.CreatePtrToInt(&I, IRB.getInt64Ty()),
                          ConstantInt::get(IRB.getInt8Ty(), SP.ptrBits)});
  }

  void visitLoadInst(LoadInst &I) {
    IRBuilder<> IRB(&I);

    auto addr = I.getPointerOperand();
    // auto addrExpr = getOrCreateSymbolicExpression(addr, IRB);
    // TODO generate diverging inputs for addrExpr

    auto dataType = I.getType();
    auto data = IRB.CreateCall(
        SP.readMemory,
        {IRB.CreatePtrToInt(addr, SP.intPtrType),
         ConstantInt::get(SP.intPtrType,
                          SP.dataLayout->getTypeStoreSize(dataType)),
         ConstantInt::get(IRB.getInt8Ty(), isLittleEndian(dataType) ? 1 : 0)});

    if (dataType->isFloatingPointTy()) {
      data = IRB.CreateCall(SP.buildBitsToFloat,
                            {data, IRB.getInt1(dataType->isDoubleTy())});
    }

    symbolicExpressions[&I] = data;
  }

  void visitStoreInst(StoreInst &I) {
    IRBuilder<> IRB(&I);

    // auto addrExpr = getOrCreateSymbolicExpression(I.getPointerOperand(),
    // IRB);
    // TODO generate diverging input for the target address

    auto data = getOrCreateSymbolicExpression(I.getValueOperand(), IRB);
    auto dataType = I.getValueOperand()->getType();
    if (dataType->isFloatingPointTy()) {
      data = IRB.CreateCall(SP.buildFloatToBits, data);
    }

    IRB.CreateCall(SP.writeMemory,
                   {IRB.CreatePtrToInt(I.getPointerOperand(), SP.intPtrType),
                    ConstantInt::get(SP.intPtrType,
                                     SP.dataLayout->getTypeStoreSize(dataType)),
                    data,
                    ConstantInt::get(IRB.getInt8Ty(),
                                     SP.dataLayout->isLittleEndian() ? 1 : 0)});
  }

  void visitGetElementPtrInst(GetElementPtrInst &I) {
    IRBuilder<> IRB(&I);
    symbolicExpressions[&I] = handleGEPOperator(cast<GEPOperator>(I), IRB);
  }

  void visitBitCastInst(BitCastInst &I) {
    IRBuilder<> IRB(&I);
    symbolicExpressions[&I] =
        handleBitCastOperator(cast<BitCastOperator>(I), IRB);
  }

  void visitTruncInst(TruncInst &I) {
    IRBuilder<> IRB(&I);
    symbolicExpressions[&I] = IRB.CreateCall(
        SP.buildTrunc,
        {I.getOperand(0), IRB.getInt8(I.getDestTy()->getIntegerBitWidth())});
  }

  void visitIntToPtrInst(IntToPtrInst &I) {
    IRBuilder<> IRB(&I);
    symbolicExpressions[&I] =
        getOrCreateSymbolicExpression(I.getOperand(0), IRB);
    // TODO handle truncation and zero extension
  }

  void visitPtrToIntInst(PtrToIntInst &I) {
    IRBuilder<> IRB(&I);
    symbolicExpressions[&I] =
        getOrCreateSymbolicExpression(I.getOperand(0), IRB);
    // TODO handle truncation and zero extension
  }

  void visitSIToFPInst(SIToFPInst &I) {
    IRBuilder<> IRB(&I);
    symbolicExpressions[&I] =
        IRB.CreateCall(SP.buildIntToFloat,
                       {getOrCreateSymbolicExpression(I.getOperand(0), IRB),
                        IRB.getInt1(I.getDestTy()->isDoubleTy())});
  }

  void visitFPExtInst(FPExtInst &I) {
    IRBuilder<> IRB(&I);
    symbolicExpressions[&I] =
        IRB.CreateCall(SP.buildFloatToFloat,
                       {getOrCreateSymbolicExpression(I.getOperand(0), IRB),
                        IRB.getInt1(I.getDestTy()->isDoubleTy())});
  }

  void visitFPTruncInst(FPTruncInst &I) {
    IRBuilder<> IRB(&I);
    symbolicExpressions[&I] =
        IRB.CreateCall(SP.buildFloatToFloat,
                       {getOrCreateSymbolicExpression(I.getOperand(0), IRB),
                        IRB.getInt1(I.getDestTy()->isDoubleTy())});
  }

  void visitFPToSI(FPToSIInst &I) {
    IRBuilder<> IRB(&I);
    symbolicExpressions[&I] =
        IRB.CreateCall(SP.buildFloatToSignedInt,
                       {getOrCreateSymbolicExpression(I.getOperand(0), IRB),
                        IRB.getInt8(I.getType()->getIntegerBitWidth())});
  }

  void visitFPToUI(FPToUIInst &I) {
    IRBuilder<> IRB(&I);
    symbolicExpressions[&I] =
        IRB.CreateCall(SP.buildFloatToUnsignedInt,
                       {getOrCreateSymbolicExpression(I.getOperand(0), IRB),
                        IRB.getInt8(I.getType()->getIntegerBitWidth())});
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

  void visitPHINode(PHINode &I) {
    // PHI nodes just assign values based on the origin of the last jump, so we
    // assign the corresponding symbolic expression the same way.

    IRBuilder<> IRB(&I);
    unsigned numIncomingValues = I.getNumIncomingValues();

    auto exprPHI = IRB.CreatePHI(IRB.getInt8PtrTy(), numIncomingValues);
    for (unsigned incoming = 0; incoming < numIncomingValues; incoming++) {
      auto block = I.getIncomingBlock(incoming);
      // Any code we may have to generate for the symbolic expressions will have
      // to live in the basic block that the respective value comes from: PHI
      // nodes can't be preceded by regular code in a basic block.
      IRBuilder<> blockIRB(block->getTerminator());
      exprPHI->addIncoming(
          getOrCreateSymbolicExpression(I.getIncomingValue(incoming), blockIRB),
          block);
    }

    symbolicExpressions[&I] = exprPHI;
  }

  void visitExtractValueInst(ExtractValueInst &I) {
    uint64_t offset = 0;
    auto indexedType = I.getAggregateOperand()->getType();
    for (auto index : I.indices()) {
      // All indices in an extractvalue instruction are constant:
      // https://llvm.org/docs/LangRef.html#extractvalue-instruction

      if (auto structType = dyn_cast<StructType>(indexedType)) {
        offset +=
            SP.dataLayout->getStructLayout(structType)->getElementOffset(index);
        indexedType = structType->getElementType(index);
      } else {
        auto arrayType = cast<ArrayType>(indexedType);
        unsigned elementSize =
            SP.dataLayout->getTypeAllocSize(arrayType->getArrayElementType());
        offset += elementSize * index;
        indexedType = arrayType->getArrayElementType();
      }
    }

    IRBuilder<> IRB(&I);
    auto aggregateExpr =
        getOrCreateSymbolicExpression(I.getAggregateOperand(), IRB);
    symbolicExpressions[&I] = IRB.CreateCall(
        SP.buildExtract,
        {aggregateExpr, IRB.getInt64(offset),
         IRB.getInt64(SP.dataLayout->getTypeStoreSize(I.getType())),
         IRB.getInt8(isLittleEndian(I.getType()) ? 1 : 0)});
  }

  void visitUnreachableInst(UnreachableInst &) {
    // Nothing to do here...
  }

  void visitInstruction(Instruction &I) {
    errs() << "Warning: unknown instruction " << I << '\n';
  }

private:
  const SymbolizePass &SP;

  /// Mapping from SSA values to symbolic expressions.
  ///
  /// For pointer values, the stored value is not an expression but a pointer to
  /// the expression of the referenced value.
  ValueMap<Value *, Value *> symbolicExpressions;
}; // namespace

void addSymbolizePass(const PassManagerBuilder & /* unused */,
                      legacy::PassManagerBase &PM) {
  PM.add(new SymbolizePass());
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
  dataLayout = &M.getDataLayout();
  intPtrType = M.getDataLayout().getIntPtrType(M.getContext());
  ptrBits = M.getDataLayout().getPointerSizeInBits();

  IRBuilder<> IRB(M.getContext());
  auto ptrT = IRB.getInt8PtrTy();
  auto int8T = IRB.getInt8Ty();
  auto voidT = IRB.getVoidTy();

  buildInteger = M.getOrInsertFunction("_sym_build_integer", ptrT,
                                       IRB.getInt64Ty(), int8T);
  buildFloat = M.getOrInsertFunction("_sym_build_float", ptrT,
                                     IRB.getDoubleTy(), IRB.getInt1Ty());
  buildNullPointer = M.getOrInsertFunction("_sym_build_null_pointer", ptrT);
  buildTrue = M.getOrInsertFunction("_sym_build_true", ptrT);
  buildFalse = M.getOrInsertFunction("_sym_build_false", ptrT);
  buildNeg = M.getOrInsertFunction("_sym_build_neg", ptrT, ptrT);
  buildSExt = M.getOrInsertFunction("_sym_build_sext", ptrT, ptrT, int8T);
  buildZExt = M.getOrInsertFunction("_sym_build_zext", ptrT, ptrT, int8T);
  buildTrunc = M.getOrInsertFunction("_sym_build_trunc", ptrT, ptrT, int8T);
  buildIntToFloat = M.getOrInsertFunction("_sym_build_int_to_float", ptrT, ptrT,
                                          IRB.getInt1Ty());
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
  pushPathConstraint = M.getOrInsertFunction("_sym_push_path_constraint", ptrT,
                                             ptrT, IRB.getInt1Ty());

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

#undef LOAD_COMPARISON_HANDLER

#define LOAD_ARRAY_INITIALIZER(bits)                                           \
  initializeArray##bits = M.getOrInsertFunction(                               \
      "_sym_initialize_array_" #bits, voidT, PointerType::get(ptrT, 0),        \
      PointerType::getInt##bits##PtrTy(M.getContext()), IRB.getInt64Ty());

  LOAD_ARRAY_INITIALIZER(8)
  LOAD_ARRAY_INITIALIZER(16)
  LOAD_ARRAY_INITIALIZER(32)
  LOAD_ARRAY_INITIALIZER(64)

#undef LOAD_ARRAY_INITIALIZER

  memcpy = M.getOrInsertFunction("_sym_memcpy", voidT, ptrT, ptrT, intPtrType);
  registerMemory = M.getOrInsertFunction("_sym_register_memory", voidT,
                                         intPtrType, ptrT, intPtrType);
  readMemory = M.getOrInsertFunction("_sym_read_memory", ptrT, intPtrType,
                                     intPtrType, int8T);
  writeMemory = M.getOrInsertFunction("_sym_write_memory", voidT, intPtrType,
                                      intPtrType, ptrT, int8T);
  initializeMemory = M.getOrInsertFunction("_sym_initialize_memory", voidT,
                                           intPtrType, ptrT, intPtrType);
  buildExtract =
      M.getOrInsertFunction("_sym_build_extract", ptrT, ptrT, IRB.getInt64Ty(),
                            IRB.getInt64Ty(), int8T);

  // For each global variable, we need another global variable that holds the
  // corresponding symbolic expression.
  for (auto &global : M.globals()) {
    auto valueLength = dataLayout->getTypeAllocSize(global.getValueType());
    auto shadowType = ArrayType::get(IRB.getInt8PtrTy(), valueLength);

    // The expression has to be initialized at run time and can therefore never
    // be constant, even if the value that it represents is.
    globalExpressions[&global] =
        new GlobalVariable(M, shadowType,
                           /* isConstant */ false, global.getLinkage(),
                           Constant::getNullValue(shadowType),
                           global.getName() + ".sym_expr", &global);
  }

  // Insert a constructor that initializes the runtime and any globals.
  Function *ctor;
  std::tie(ctor, std::ignore) = createSanitizerCtorAndInitFunctions(
      M, kSymCtorName, "_sym_initialize", {}, {});
  IRB.SetInsertPoint(ctor->getEntryBlock().getTerminator());
  for (auto &&[value, expression] : globalExpressions) {
    IRB.CreateCall(
        initializeMemory,
        {IRB.CreatePtrToInt(value, intPtrType),
         IRB.CreateBitCast(expression, ptrT),
         ConstantInt::get(intPtrType,
                          expression->getValueType()->getArrayNumElements())});
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

  return true;
}
