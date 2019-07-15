#include <llvm/ADT/StringSet.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GetElementPtrTypeIterator.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/ValueMap.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Pass.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>
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
  Value *buildBool{};
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
  Value *buildFloatAbs{};
  Value *pushPathConstraint{};
  Value *getParameterExpression{};
  Value *setParameterExpression{};
  Value *setReturnExpression{};
  Value *getReturnExpression{};
  Value *memcpy{};
  Value *memset{};
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

  /// Insert code to obtain the symbolic expressions for the function arguments.
  void symbolizeFunctionArguments(Function &F) {
    // The main function doesn't receive symbolic arguments.
    if (F.getName() == "main")
      return;

    IRBuilder<> IRB(F.getEntryBlock().getFirstNonPHI());

    for (auto &arg : F.args()) {
      symbolicExpressions[&arg] = IRB.CreateCall(SP.getParameterExpression,
                                                 IRB.getInt8(arg.getArgNo()));
    }
  }

  /// Finish the processing of PHI nodes.
  ///
  /// This assumes that there is a dummy PHI node for each such instruction in
  /// the function, and that we have recorded all PHI nodes in the member
  /// phiNodes. In other words, the function has to be called after all
  /// instructions have been processed in order to fix up PHI nodes. See the
  /// documentation of member phiNodes for why we process PHI nodes in two
  /// steps.
  void finalizePHINodes() {
    for (auto phi : phiNodes) {
      for (unsigned incoming = 0, totalIncoming = phi->getNumIncomingValues();
           incoming < totalIncoming; incoming++) {
        auto block = phi->getIncomingBlock(incoming);
        // Any code we may have to generate for the symbolic expressions will
        // have to live in the basic block that the respective value comes from:
        // PHI nodes can't be preceded by regular code in a basic block.
        IRBuilder<> blockIRB(block->getTerminator());
        auto symbolicPHI = cast<PHINode>(symbolicExpressions[phi]);
        symbolicPHI->addIncoming(getOrCreateSymbolicExpression(
                                     phi->getIncomingValue(incoming), blockIRB),
                                 block);
      }
    }
  }

  /// Rewrite symbolic computation to only occur if some operand is symbolic.
  ///
  /// We don't want to build up formulas for symbolic computation if all
  /// operands are concrete. Therefore, this function rewrites all places that
  /// build up formulas (as recorded during the main pass) to skip formula
  /// construction if all operands are concrete. Additionally, it inserts code
  /// that constructs formulas for concrete operands if necessary.
  ///
  /// The basic idea is to transform code like this...
  ///
  ///   res_expr = call _sym_some_computation(expr1, expr2, ...)
  ///   res      = some_computation(val1, val2, ...)
  ///
  /// ...into this:
  ///
  ///   start:
  ///   expr1_symbolic = icmp ne 0, expr1
  ///   ...
  ///   some_symbolic = or expr1_symbolic, ...
  ///   br some_symbolic, check_arg1, end
  ///
  ///   check_arg1:
  ///   need_expr1 = icmp eq 0, expr1
  ///   br need_expr1, create_expr1, check_arg2
  ///
  ///   create_expr1:
  ///   new_expr1 = ... (based on val1)
  ///   br check_arg2
  ///
  ///   check_arg2:
  ///   good_expr1 = phi [expr1, check_arg1], [new_expr1, create_expr1]
  ///   need_expr2 = ...
  ///   ...
  ///
  ///   sym_computation:
  ///   sym_expr = call _sym_some_computation(good_expr1, good_expr2, ...)
  ///   br end
  ///
  ///   end:
  ///   final_expr = phi [null, start], [sym_expr, sym_computation]
  ///
  /// The resulting code is much longer but avoids solver calls for all
  /// operations without symbolic data.
  void shortCircuitExpressionUses() {
    for (const auto &symbolicComputation : expressionUses) {
      assert(!symbolicComputation.inputs.empty() &&
             "Symbolic computation has no inputs");

      IRBuilder<> IRB(symbolicComputation.firstInstruction);

      // Build the check whether any input expression is non-null (i.e., there
      // is a symbolic input).
      auto nullExpression = ConstantPointerNull::get(IRB.getInt8PtrTy());
      std::vector<Value *> nullChecks;
      for (const auto &input : symbolicComputation.inputs) {
        nullChecks.push_back(
            IRB.CreateICmpEQ(nullExpression, input.getSymbolicOperand()));
      }
      auto allConcrete = nullChecks[0];
      for (unsigned argIndex = 1; argIndex < nullChecks.size(); argIndex++) {
        allConcrete = IRB.CreateAnd(allConcrete, nullChecks[argIndex]);
      }

      // The main branch: if we don't enter here, we can short-circuit the
      // symbolic computation. Otherwise, we need to check all input expressions
      // and create an output expression.
      auto head = symbolicComputation.firstInstruction->getParent();
      auto slowPath = SplitBlock(head, symbolicComputation.firstInstruction);
      auto tail = SplitBlock(
          slowPath, symbolicComputation.lastInstruction->getNextNode());
      ReplaceInstWithInst(head->getTerminator(),
                          BranchInst::Create(tail, slowPath, allConcrete));

      // In the slow case, we need to check each input expression for null
      // (i.e., the input is concrete) and create an expression from the
      // concrete value if necessary.
      for (unsigned argIndex = 0; argIndex < symbolicComputation.inputs.size();
           argIndex++) {
        auto &argument = symbolicComputation.inputs[argIndex];
        auto originalArgExpression = argument.getSymbolicOperand();
        auto argCheckBlock = symbolicComputation.firstInstruction->getParent();

        IRB.SetInsertPoint(symbolicComputation.firstInstruction);
        auto argExpressionBlock = SplitBlockAndInsertIfThen(
            nullChecks[argIndex], symbolicComputation.firstInstruction,
            /* unreachable */ false);

        IRB.SetInsertPoint(argExpressionBlock);
        auto newArgExpression =
            createValueExpression(argument.concreteValue, IRB);

        IRB.SetInsertPoint(symbolicComputation.firstInstruction);
        auto argExpression = IRB.CreatePHI(IRB.getInt8PtrTy(), 2);
        argExpression->addIncoming(originalArgExpression, argCheckBlock);
        argExpression->addIncoming(newArgExpression,
                                   argExpressionBlock->getParent());
        argument.user->replaceUsesOfWith(originalArgExpression, argExpression);
      }

      // Finally, the overall result (if the computation produces one) is null
      // if we've taken the fast path and the symbolic expression computed above
      // if short-circuiting wasn't possible.
      if (!symbolicComputation.lastInstruction->use_empty()) {
        IRB.SetInsertPoint(&tail->front());
        auto finalExpression = IRB.CreatePHI(IRB.getInt8PtrTy(), 2);
        symbolicComputation.lastInstruction->replaceAllUsesWith(
            finalExpression);
        finalExpression->addIncoming(
            ConstantPointerNull::get(IRB.getInt8PtrTy()), head);
        finalExpression->addIncoming(
            symbolicComputation.lastInstruction,
            symbolicComputation.lastInstruction->getParent());
      }

    }
  }

  void handleIntrinsicCall(CallInst &I) {
    auto callee = I.getCalledFunction();

    switch (callee->getIntrinsicID()) {
    case Intrinsic::lifetime_start:
    case Intrinsic::lifetime_end:
    case Intrinsic::dbg_declare:
      // These are safe to ignore.
      break;
    case Intrinsic::memcpy: {
      // auto destExpr = getOrCreateSymbolicExpression(I.getOperand(0), IRB);
      // auto srcExpr = getOrCreateSymbolicExpression(I.getOperand(1), IRB);
      // TODO generate diverging inputs for the source, destination and length
      // of the copy operation

      IRBuilder<> IRB(&I);
      IRB.CreateCall(SP.memcpy,
                     {I.getOperand(0), I.getOperand(1), I.getOperand(2)});
      break;
    }
    case Intrinsic::memset: {
      // TODO generate diverging inputs for the memory and length operands

      IRBuilder<> IRB(&I);
      IRB.CreateCall(SP.memset,
                     {I.getOperand(0),
                      getOrCreateSymbolicExpression(I.getOperand(1), IRB),
                      IRB.CreateZExt(I.getOperand(2), IRB.getInt64Ty())});
      break;
    }
    case Intrinsic::stacksave: {
      // The intrinsic returns an opaque pointer that should only be passed to
      // the stackrestore intrinsic later. We treat the pointer as a constant.
      IRBuilder<> IRB(I.getNextNode());
      symbolicExpressions[&I] = createValueExpression(&I, IRB);
      break;
    }
    case Intrinsic::stackrestore:
      // Ignored; see comment on stacksave above.
      break;
    case Intrinsic::expect: {
      // Just a hint for the optimizer; the value is the first parameter.
      IRBuilder<> IRB(&I);
      symbolicExpressions[&I] =
          getOrCreateSymbolicExpression(I.getArgOperand(0), IRB);
      break;
    }
    case Intrinsic::fabs: {
      // Floating-point absolute value; use the runtime to build the
      // corresponding symbolic expression.

      IRBuilder<> IRB(&I);
      symbolicExpressions[&I] =
          IRB.CreateCall(SP.buildFloatAbs,
                         {getOrCreateSymbolicExpression(I.getOperand(0), IRB)});
      break;
    }
    case Intrinsic::cttz:
    case Intrinsic::ctpop:
    case Intrinsic::ctlz: {
      // Various bit-count operations. Expressing these symbolically is
      // difficult, so for now we just concretize.

      errs() << "Warning: losing track of symbolic expressions at bit-count "
                "operation "
             << I << "\n";
      IRBuilder<> IRB(I.getNextNode());
      symbolicExpressions[&I] = IRB.CreateCall(
          SP.buildInteger, {IRB.CreateZExt(&I, IRB.getInt64Ty()),
                            IRB.getInt8(I.getType()->getIntegerBitWidth())});
      break;
    }
    case Intrinsic::returnaddress: {
      // Obtain the return address of the current function or one of its parents
      // on the stack. We just concretize.

      errs() << "Warning: using concrete value for return address\n";
      IRBuilder<> IRB(I.getNextNode());
      symbolicExpressions[&I] = IRB.CreateCall(
          SP.buildInteger,
          {IRB.CreatePtrToInt(&I, SP.intPtrType), IRB.getInt8(SP.ptrBits)});
      break;
    }
    default:
      errs() << "Warning: unhandled LLVM intrinsic " << callee->getName()
             << '\n';
      break;
    }
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
    symbolicExpressions[&I] = createValueExpression(&I, IRB);
  }

  //
  // Implementation of InstVisitor
  //

  void visitBinaryOperator(BinaryOperator &I) {
    // Binary operators propagate into the symbolic expression.

    IRBuilder<> IRB(&I);
    Value *handler = SP.binaryOperatorHandlers.at(I.getOpcode());
    assert(handler && "Unable to handle binary operator");
    auto runtimeCall =
        buildRuntimeCall(IRB, handler, {I.getOperand(0), I.getOperand(1)});
    registerSymbolicComputation(runtimeCall, &I);
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
    auto runtimeCall =
        buildRuntimeCall(IRB, handler, {I.getOperand(0), I.getOperand(1)});
    registerSymbolicComputation(runtimeCall, &I);
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
    // TODO prevent instrumentation of our own functions with attributes

    if (I.isInlineAsm()) {
      handleInlineAssembly(I);
      return;
    }

    auto callee = I.getCalledFunction();
    if (callee && callee->isIntrinsic()) {
      handleIntrinsicCall(I);
      return;
    }

    // TODO if this is an indirect call, create a diverging input

    IRBuilder<> IRB(&I);
    auto targetName = callee ? callee->getName() : StringRef{};
    bool isMakeSymbolic = (targetName == "sym_make_symbolic");

    if (targetName.startswith("_sym_") && !isMakeSymbolic)
      return;

    if (!isMakeSymbolic) {
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
    // GEP performs address calculations but never actually accesses memory. In
    // order to represent the result of a GEP symbolically, we start from the
    // symbolic expression of the original pointer and duplicate its
    // computations at the symbolic level.

    // If there are no indices we can return early (does this ever happen?).
    if (I.getNumIndices() == 0) {
      symbolicExpressions[&I] = getSymbolicExpression(I.getPointerOperand());
      return;
    }

    IRBuilder<> IRB(&I);
    SymbolicComputation symbolicComputation;
    Value *currentAddress = I.getPointerOperand();

    for (auto type_it = gep_type_begin(I), type_end = gep_type_end(I);
         type_it != type_end; ++type_it) {
      auto index = type_it.getOperand();
      std::pair<Value *, bool> addressContribution;

      // There are two cases for the calculation:
      // 1. If the indexed type is a struct, we need to add the offset of the
      //    desired member.
      // 2. If it is an array or a pointer, compute the offset of the desired
      //    element.
      if (auto structType = type_it.getStructTypeOrNull()) {
        // Structs can only be indexed with constants
        // (https://llvm.org/docs/LangRef.html#getelementptr-instruction).

        unsigned memberIndex = cast<ConstantInt>(index)->getZExtValue();
        unsigned memberOffset = SP.dataLayout->getStructLayout(structType)
                                    ->getElementOffset(memberIndex);
        addressContribution = {ConstantInt::get(IRB.getInt64Ty(), memberOffset),
                               true};
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
        if (auto indexWidth = index->getType()->getIntegerBitWidth();
            indexWidth != 64) {
          symbolicComputation.merge(buildRuntimeCall(
              IRB, SP.buildZExt,
              {{index, true},
               {ConstantInt::get(IRB.getInt8Ty(), 64 - indexWidth), false}}));
          symbolicComputation.merge(buildRuntimeCall(
              IRB, SP.binaryOperatorHandlers[Instruction::Mul],
              {{symbolicComputation.lastInstruction, false},
               {ConstantInt::get(IRB.getInt64Ty(), elementSize), true}}));
        } else {
          symbolicComputation.merge(buildRuntimeCall(
              IRB, SP.binaryOperatorHandlers[Instruction::Mul],
              {index, ConstantInt::get(IRB.getInt64Ty(), elementSize)}));
        }

        addressContribution = {symbolicComputation.lastInstruction, false};
      }

      symbolicComputation.merge(buildRuntimeCall(
          IRB, SP.binaryOperatorHandlers[Instruction::Add],
          {addressContribution,
           {currentAddress, (currentAddress == I.getPointerOperand())}}));
      currentAddress = symbolicComputation.lastInstruction;
    }

    registerSymbolicComputation(symbolicComputation, &I);
  }

  void visitBitCastInst(BitCastInst &I) {
    IRBuilder<> IRB(&I);
    assert(I.getSrcTy()->isPointerTy() && I.getDestTy()->isPointerTy() &&
           "Unhandled non-pointer bit cast");
    symbolicExpressions[&I] =
        getOrCreateSymbolicExpression(I.getOperand(0), IRB);
  }

  void visitTruncInst(TruncInst &I) {
    IRBuilder<> IRB(&I);
    symbolicExpressions[&I] = IRB.CreateCall(
        SP.buildTrunc, {getOrCreateSymbolicExpression(I.getOperand(0), IRB),
                        IRB.getInt8(I.getDestTy()->getIntegerBitWidth())});
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
                        IRB.getInt1(I.getDestTy()->isDoubleTy()),
                        /* is_signed */ IRB.getInt1(true)});
  }

  void visitUIToFPInst(UIToFPInst &I) {
    IRBuilder<> IRB(&I);
    symbolicExpressions[&I] =
        IRB.CreateCall(SP.buildIntToFloat,
                       {getOrCreateSymbolicExpression(I.getOperand(0), IRB),
                        IRB.getInt1(I.getDestTy()->isDoubleTy()),
                        /* is_signed */ IRB.getInt1(false)});
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

      auto symbolicCast =
          buildRuntimeCall(IRB, target,
                           {{I.getOperand(0), true},
                            {IRB.getInt8(I.getDestTy()->getIntegerBitWidth() -
                                         I.getSrcTy()->getIntegerBitWidth()),
                             false}});
      registerSymbolicComputation(symbolicCast, &I);
    }
  }

  void visitPHINode(PHINode &I) {
    // PHI nodes just assign values based on the origin of the last jump, so we
    // assign the corresponding symbolic expression the same way.

    phiNodes.push_back(&I); // to be finalized later, see finalizePHINodes

    IRBuilder<> IRB(&I);
    unsigned numIncomingValues = I.getNumIncomingValues();
    auto exprPHI = IRB.CreatePHI(IRB.getInt8PtrTy(), numIncomingValues);
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

  void visitSwitchInst(SwitchInst &I) {
    // Switch compares a value against a set of integer constants; duplicate
    // constants are not allowed
    // (https://llvm.org/docs/LangRef.html#switch-instruction). We can't just
    // push the new path constraint at the jump destinations, because the
    // destination blocks may be the targets of other jumps as well. Therefore,
    // we insert a series of new blocks that construct and push the respective
    // path constraint before jumping to the original target.

    // TODO should we try to generate inputs for *all* other paths?

    IRBuilder<> IRB(&I);
    auto conditionExpr = getOrCreateSymbolicExpression(I.getCondition(), IRB);

    // The default case needs to assert that the condition doesn't equal any of
    // the cases.
    if (I.getNumCases() > 0) {
      auto finalDest = I.getDefaultDest();
      auto constraintBlock = BasicBlock::Create(
          I.getContext(), /* name */ "", finalDest->getParent(), finalDest);
      I.setDefaultDest(constraintBlock);
      IRB.SetInsertPoint(constraintBlock);

      auto currentCase = I.case_begin();
      auto constraint = IRB.CreateCall(
          SP.comparisonHandlers[CmpInst::ICMP_NE],
          {conditionExpr,
           createValueExpression(currentCase->getCaseValue(), IRB)});
      ++currentCase;

      for (auto endCase = I.case_end(); currentCase != endCase; ++currentCase) {
        constraint = IRB.CreateCall(
            SP.binaryOperatorHandlers[Instruction::And],
            {constraint,
             IRB.CreateCall(
                 SP.comparisonHandlers[CmpInst::ICMP_NE],
                 {conditionExpr,
                  createValueExpression(currentCase->getCaseValue(), IRB)})});
      }

      IRB.CreateCall(SP.pushPathConstraint, {constraint, IRB.getInt1(true)});
      IRB.CreateBr(finalDest);
    }

    // The individual cases just assert that the condition equals their
    // respective value.
    for (auto &caseHandle : I.cases()) {
      auto finalDest = caseHandle.getCaseSuccessor();
      auto constraintBlock = BasicBlock::Create(
          I.getContext(), /* name */ "", finalDest->getParent(), finalDest);
      caseHandle.setSuccessor(constraintBlock);
      IRB.SetInsertPoint(constraintBlock);

      auto constraint = IRB.CreateCall(
          SP.comparisonHandlers[CmpInst::ICMP_EQ],
          {conditionExpr,
           createValueExpression(caseHandle.getCaseValue(), IRB)});
      IRB.CreateCall(SP.pushPathConstraint, {constraint, IRB.getInt1(true)});
      IRB.CreateBr(finalDest);
    }
  }

  void visitUnreachableInst(UnreachableInst &) {
    // Nothing to do here...
  }

  void visitInstruction(Instruction &I) {
    errs() << "Warning: unknown instruction " << I << '\n';
  }

private:
  static constexpr unsigned kExpectedMaxPHINodesPerFunction = 16;
  static constexpr unsigned kExpectedSymbolicArgumentsPerComputation = 2;

  /// A symbolic input.
  struct Input {
    Value *concreteValue;
    unsigned operandIndex;
    Instruction *user;

    Value *getSymbolicOperand() const { return user->getOperand(operandIndex); }
  };

  /// A symbolic computation with its inputs.
  struct SymbolicComputation {
    Instruction *firstInstruction = nullptr, *lastInstruction = nullptr;
    SmallVector<Input, kExpectedSymbolicArgumentsPerComputation> inputs;

    SymbolicComputation() = default;

    SymbolicComputation(Instruction *first, Instruction *last,
                        ArrayRef<Input> in)
        : firstInstruction(first), lastInstruction(last),
          inputs(in.begin(), in.end()) {}

    /// Append another symbolic computation to this one.
    ///
    /// The computation that is to be appended must occur after the one that
    /// this method is called on.
    void merge(const SymbolicComputation &other) {
      if (&other == this)
        return;

      if (firstInstruction == nullptr)
        firstInstruction = other.firstInstruction;
      lastInstruction = other.lastInstruction;

      for (const auto &input : other.inputs)
        inputs.push_back(input);
    }

    friend raw_ostream &
    operator<<(raw_ostream &out,
               const Symbolizer::SymbolicComputation &computation) {
      out << "\nComputation starting at " << computation.firstInstruction
          << "\n...ending at " << computation.lastInstruction
          << "\n...with inputs:\n";
      for (const auto &input : computation.inputs) {
        out << '\t' << *input.concreteValue << '\n';
      }
      return out;
    }
  };

  /// Create an expression that represents the concrete value.
  Value *createValueExpression(Value *V, IRBuilder<> &IRB) {
    auto valueType = V->getType();

    if (isa<ConstantPointerNull>(V)) {
      return IRB.CreateCall(SP.buildNullPointer, {});
    }

    if (valueType->isIntegerTy()) {
      // Special case: LLVM uses the type i1 to represent Boolean values, but
      // for Z3 we have to create expressions of a separate sort.
      if (valueType->getPrimitiveSizeInBits() == 1) {
        return IRB.CreateCall(SP.buildBool, {V});
      }

      return IRB.CreateCall(SP.buildInteger,
                            {IRB.CreateZExtOrBitCast(V, IRB.getInt64Ty()),
                             IRB.getInt8(valueType->getPrimitiveSizeInBits())});
    }

    if (valueType->isFloatingPointTy()) {
      return IRB.CreateCall(SP.buildFloat,
                            {IRB.CreateFPExt(V, IRB.getDoubleTy()),
                             IRB.getInt1(valueType->isDoubleTy())});
    }

    if (valueType->isPointerTy()) {
      return IRB.CreateCall(
          SP.buildInteger,
          {IRB.CreatePtrToInt(V, IRB.getInt64Ty()), IRB.getInt8(SP.ptrBits)});
    }

    if (valueType->isStructTy()) {
      // In unoptimized code we may see structures in SSA registers. What we
      // want is a single bit-vector expression describing their contents, but
      // unfortunately we can't take the address of a register. We fix the
      // problem with a hack: we write the register to memory and initialize the
      // expression from there.
      //
      // An alternative would be to change the representation of structures in
      // SSA registers to "shadow structures" that contain one expression per
      // member. However, this would put an additional burden on the handling of
      // cast instructions, because expressions would have to be converted
      // between different representations according to the type.

      auto memory = IRB.CreateAlloca(V->getType());
      IRB.CreateStore(V, memory);
      return IRB.CreateCall(
          SP.readMemory,
          {IRB.CreatePtrToInt(memory, SP.intPtrType),
           ConstantInt::get(SP.intPtrType,
                            SP.dataLayout->getTypeStoreSize(V->getType())),
           IRB.getInt8(0)});
    }

    llvm_unreachable("Unhandled type for constant expression");
  }

  /// Get the (already created) symbolic expression for a value.
  Value *getSymbolicExpression(Value *V) {
    auto exprIt = symbolicExpressions.find(V);
    return (exprIt != symbolicExpressions.end())
               ? exprIt->second
               : ConstantPointerNull::get(PointerType::get(
                     IntegerType::getInt8Ty(V->getContext()), 0));
  }

  /// Load or create the symbolic expression for a value.
  Value *getOrCreateSymbolicExpression(Value *V, IRBuilder<> &IRB) {
    if (auto expr = getSymbolicExpression(V); expr != nullptr)
      return expr;

    if (isa<Constant>(V) || isa<Argument>(V)) {
      // Don't create expressions for constants; the short-circuit logic will
      // create them if necessary (i.e., if the constants are used in
      // computations with non-constants).
      return ConstantPointerNull::get(IRB.getInt8PtrTy());
    }

    DEBUG(errs() << "Unable to obtain a symbolic expression for " << *V
                 << '\n');
    llvm_unreachable("No symbolic expression for value");
  }

  bool isLittleEndian(Type *type) {
    return (!type->isAggregateType() && SP.dataLayout->isLittleEndian());
  }

  /// Create a call to the specified function in the run-time library.
  ///
  /// Each argument is specified as a pair of Value and Boolean. The Boolean
  /// specifies whether the Value is a symbolic argument, in which case the
  /// corresponding symbolic expression will be passed to the run-time function.
  /// Moreover, the use of symbolic expressions will be recorded in the
  /// resulting SymbolicComputation.
  SymbolicComputation
  buildRuntimeCall(IRBuilder<> &IRB, Value *function,
                   ArrayRef<std::pair<Value *, bool>> args) {
    std::vector<Value *> functionArgs;
    for (auto &[arg, symbolic] : args) {
      functionArgs.push_back(symbolic ? getSymbolicExpression(arg) : arg);
    }
    auto call = IRB.CreateCall(function, functionArgs);

    std::vector<Input> inputs;
    for (unsigned i = 0; i < args.size(); i++) {
      auto &[arg, symbolic] = args[i];
      if (symbolic)
        inputs.push_back({arg, i, call});
    }

    return SymbolicComputation(call, call, inputs);
  }

  /// Convenience overload that treats all arguments as symbolic.
  SymbolicComputation buildRuntimeCall(IRBuilder<> &IRB, Value *function,
                                       ArrayRef<Value *> symbolicArgs) {
    std::vector<std::pair<Value *, bool>> args;
    for (const auto &arg : symbolicArgs) {
      args.push_back({arg, true});
    }

    return buildRuntimeCall(IRB, function, args);
  }

  /// Register the result of the computation as the symbolic expression
  /// corresponding to the concrete value and record the computation for
  /// short-circuiting.
  void registerSymbolicComputation(const SymbolicComputation &computation,
                                   Value *concrete) {
    symbolicExpressions[concrete] = computation.lastInstruction;
    expressionUses.push_back(computation);
  }

  const SymbolizePass &SP;

  /// Mapping from SSA values to symbolic expressions.
  ///
  /// For pointer values, the stored value is an expression describing the value
  /// of the pointer itself (i.e., the address, not the referenced value). For
  /// structure values, the expression is a single large bit vector.
  ValueMap<Value *, Value *> symbolicExpressions;

  /// A record of all PHI nodes in this function.
  ///
  /// PHI nodes may refer to themselves, in which case we run into an infinite
  /// loop when trying to generate symbolic expressions recursively. Therefore,
  /// we only insert a dummy symbolic expression for each PHI node and fix it
  /// after all instructions have been processed.
  SmallVector<PHINode *, kExpectedMaxPHINodesPerFunction> phiNodes;

  /// A record of expression uses that can be short-circuited.
  ///
  /// Most values in a program are concrete, even if they're not constant (in
  /// which case we would know that they're concrete at compile time already).
  /// There is no point in building up formulas if all values involved in a
  /// computation are concrete, so we short-circuit those cases. Since this
  /// process requires splitting basic blocks, we can't do it during the main
  /// analysis phase (because InstVisitor gets out of step if we try).
  /// Therefore, we keep a record of all the places that construct expressions
  /// and insert the fast path later.
  std::vector<SymbolicComputation> expressionUses;
};

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

  // Redirect calls to external functions to the corresponding wrappers and
  // rename internal functions.
  static const StringSet<> kFunctionsNoHook = {
      "printf", "err", "exit", "munmap",
      // TODO
      "cgc_rint", "cgc_pow", "cgc_log10", "__isoc99_sscanf", "strlen",
      "__errno_location"};
  for (auto &function : M.functions()) {
    auto name = function.getName();
    if (kFunctionsNoHook.count(name) || name == "main" ||
        name.startswith("llvm.") || name == "sym_make_symbolic")
      continue;

    function.setName("__symbolized_" + name);
  }

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
  pushPathConstraint = M.getOrInsertFunction("_sym_push_path_constraint", voidT,
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
    // Don't create symbolic shadows for LLVM's special variables.
    if (global.getName().startswith("llvm."))
      continue;

    Type *shadowType;
    if (global.isDeclaration()) {
      shadowType = IRB.getInt8PtrTy();
    } else {
      auto valueLength = dataLayout->getTypeAllocSize(global.getValueType());
      shadowType = ArrayType::get(IRB.getInt8PtrTy(), valueLength);
    }

    // The expression has to be initialized at run time and can therefore never
    // be constant, even if the value that it represents is.
    globalExpressions[&global] = new GlobalVariable(
        M, shadowType,
        /* isConstant */ false, global.getLinkage(),
        global.isDeclaration() ? nullptr : Constant::getNullValue(shadowType),
        global.getName() + ".sym_expr", &global, global.getThreadLocalMode(),
        global.isExternallyInitialized());
  }

  // Insert a constructor that initializes the runtime and any globals.
  Function *ctor;
  std::tie(ctor, std::ignore) = createSanitizerCtorAndInitFunctions(
      M, kSymCtorName, "_sym_initialize", {}, {});
  IRB.SetInsertPoint(ctor->getEntryBlock().getTerminator());
  for (auto &&[value, expression] : globalExpressions) {
    // External globals are initialized in their respective module.
    if (value->isDeclaration())
      continue;

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
  auto functionName = F.getName();
  if (functionName == kSymCtorName)
    return false;

  DEBUG(errs() << "Symbolizing function ");
  DEBUG(errs().write_escaped(functionName) << '\n');

  Symbolizer symbolizer(*this);
  // DEBUG(errs() << F << '\n');
  symbolizer.symbolizeFunctionArguments(F);
  symbolizer.visit(F);
  symbolizer.finalizePHINodes();
  symbolizer.shortCircuitExpressionUses();
  assert(!verifyFunction(F, &errs()) &&
         "SymbolizePass produced invalid bitcode");

  return true;
}
