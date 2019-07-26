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

#ifndef NDEBUG
#define DEBUG(X)                                                               \
  do {                                                                         \
    X;                                                                         \
  } while (false)
#else
#define DEBUG(X) ((void)0)
#endif

using namespace llvm;

namespace {

/// Runtime functions
struct Runtime {
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
  Value *buildBoolAnd{};
  Value *buildBoolOr{};
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

  Runtime(Module &M) {
    IRBuilder<> IRB(M.getContext());
    auto intPtrType = M.getDataLayout().getIntPtrType(M.getContext());
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
    buildIntToFloat =
        M.getOrInsertFunction("_sym_build_int_to_float", ptrT, ptrT,
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
    buildBoolAnd =
        M.getOrInsertFunction("_sym_build_bool_and", ptrT, ptrT, ptrT);
    buildBoolOr = M.getOrInsertFunction("_sym_build_bool_or", ptrT, ptrT, ptrT);
    pushPathConstraint = M.getOrInsertFunction("_sym_push_path_constraint",
                                               voidT, ptrT, IRB.getInt1Ty());

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
    LOAD_COMPARISON_HANDLER(FCMP_UNO, float_unordered)
    LOAD_COMPARISON_HANDLER(FCMP_UGT, float_unordered_greater_than)
    LOAD_COMPARISON_HANDLER(FCMP_UGE, float_unordered_greater_equal)
    LOAD_COMPARISON_HANDLER(FCMP_ULT, float_unordered_less_than)
    LOAD_COMPARISON_HANDLER(FCMP_ULE, float_unordered_less_equal)
    LOAD_COMPARISON_HANDLER(FCMP_UEQ, float_unordered_equal)
    LOAD_COMPARISON_HANDLER(FCMP_UNE, float_unordered_not_equal)

#undef LOAD_COMPARISON_HANDLER

    memcpy =
        M.getOrInsertFunction("_sym_memcpy", voidT, ptrT, ptrT, intPtrType);
    memset = M.getOrInsertFunction("_sym_memset", voidT, ptrT, ptrT,
                                   IRB.getInt64Ty());
    registerMemory = M.getOrInsertFunction("_sym_register_memory", voidT,
                                           intPtrType, ptrT, intPtrType);
    readMemory = M.getOrInsertFunction("_sym_read_memory", ptrT, intPtrType,
                                       intPtrType, int8T);
    writeMemory = M.getOrInsertFunction("_sym_write_memory", voidT, intPtrType,
                                        intPtrType, ptrT, int8T);
    initializeMemory = M.getOrInsertFunction("_sym_initialize_memory", voidT,
                                             intPtrType, ptrT, intPtrType);
    buildExtract =
        M.getOrInsertFunction("_sym_build_extract", ptrT, ptrT,
                              IRB.getInt64Ty(), IRB.getInt64Ty(), int8T);
  }
};

/// Decide whether a function is called symbolically.
bool isSymbolizedFunction(const Function &f) {
  static const StringSet<> kConcretizedFunctions = {
      // Some libc functions whose results we can concretize
      "printf", "err", "exit", "munmap", "free", "perror", "getenv", "select",
      "write", "rand", "setjmp", "longjmp",
      // Returns the address of errno, so always concrete
      "__errno_location",
      // CGC run-time functions that Z3 can't really represent in the logic of
      // bit vectors
      "cgc_rint", "cgc_pow", "cgc_log10", "cgc_sin", "cgc_cos", "cgc_sqrt",
      "cgc_log", "cgc_log2", "cgc_atan2", "cgc_tan", "cgc_log2f", "cgc_logf",
      "cgc_exp2f", "cgc_sqrtf",
      // TODO CGC uses sscanf only on concrete data, so for now we can
      // concretize
      "__isoc99_sscanf",
      // TODO
      "strlen", "cgc_remainder", "cgc_fabs", "cgc_setjmp", "cgc_longjmp",
      "__stack_chk_fail"};

  if (kConcretizedFunctions.count(f.getName()))
    return false;

  return true;
}

class SymbolizePass : public FunctionPass {
public:
  static char ID;

  SymbolizePass() : FunctionPass(ID) {}

  bool doInitialization(Module &M) override;
  bool runOnFunction(Function &F) override;

private:
  static constexpr char kSymCtorName[] = "__sym_ctor";

  /// Mapping from global variables to their corresponding symbolic expressions.
  ValueMap<GlobalVariable *, GlobalVariable *> globalExpressions;

  friend class Symbolizer;
};

class Symbolizer : public InstVisitor<Symbolizer> {
public:
  explicit Symbolizer(Module &M)
      : runtime(M), dataLayout(M.getDataLayout()),
        ptrBits(M.getDataLayout().getPointerSizeInBits()),
        intPtrType(M.getDataLayout().getIntPtrType(M.getContext())) {}

  /// Insert code to obtain the symbolic expressions for the function arguments.
  void symbolizeFunctionArguments(Function &F) {
    // The main function doesn't receive symbolic arguments.
    if (F.getName() == "main")
      return;

    IRBuilder<> IRB(F.getEntryBlock().getFirstNonPHI());

    for (auto &arg : F.args()) {
      symbolicExpressions[&arg] = IRB.CreateCall(runtime.getParameterExpression,
                                                 IRB.getInt8(arg.getArgNo()));
    }
  }

  /// Finish the processing of switch instructions.
  ///
  /// Handling switch instructions requires the insertion of new basic blocks.
  /// We can't create new blocks during the main pass (due to a limitation of
  /// InstVisitor), so switch instructions are recorded in the member variable
  /// switchInstructions and processed in this function.
  void finalizeSwitchInstructions() {
    for (auto switchInst : switchInstructions) {
      // TODO should we try to generate inputs for *all* other paths?

      IRBuilder<> IRB(switchInst);
      auto conditionExpr = getSymbolicExpression(switchInst->getCondition());
      if (!conditionExpr)
        continue;

      // The default case needs to assert that the condition doesn't equal any
      // of the cases.
      if (switchInst->getNumCases() > 0) {
        auto finalDest = switchInst->getDefaultDest();
        auto constraintBlock =
            BasicBlock::Create(switchInst->getContext(), /* name */ "",
                               finalDest->getParent(), finalDest);

        IRB.SetInsertPoint(constraintBlock);

        auto currentCase = switchInst->case_begin();
        auto constraint = IRB.CreateCall(
            runtime.comparisonHandlers[CmpInst::ICMP_NE],
            {conditionExpr,
             createValueExpression(currentCase->getCaseValue(), IRB)});
        ++currentCase;

        for (auto endCase = switchInst->case_end(); currentCase != endCase;
             ++currentCase) {
          constraint = IRB.CreateCall(
              runtime.binaryOperatorHandlers[Instruction::And],
              {constraint,
               IRB.CreateCall(
                   runtime.comparisonHandlers[CmpInst::ICMP_NE],
                   {conditionExpr,
                    createValueExpression(currentCase->getCaseValue(), IRB)})});
        }

        IRB.CreateCall(runtime.pushPathConstraint,
                       {constraint, IRB.getInt1(true)});
        IRB.CreateBr(finalDest);

        auto constraintCheckBlock =
            BasicBlock::Create(switchInst->getContext(), /* name */ "",
                               finalDest->getParent(), constraintBlock);
        IRB.SetInsertPoint(constraintCheckBlock);
        auto haveSymbolicCondition = IRB.CreateICmpNE(
            conditionExpr, ConstantPointerNull::get(IRB.getInt8PtrTy()));
        IRB.CreateCondBr(haveSymbolicCondition, /* then */ constraintBlock,
                         /* else */ finalDest);

        switchInst->setDefaultDest(constraintCheckBlock);
      }

      // The individual cases just assert that the condition equals their
      // respective value.
      for (auto &caseHandle : switchInst->cases()) {
        auto finalDest = caseHandle.getCaseSuccessor();
        auto constraintBlock =
            BasicBlock::Create(switchInst->getContext(), /* name */ "",
                               finalDest->getParent(), finalDest);

        IRB.SetInsertPoint(constraintBlock);
        auto constraint = IRB.CreateCall(
            runtime.comparisonHandlers[CmpInst::ICMP_EQ],
            {conditionExpr,
             createValueExpression(caseHandle.getCaseValue(), IRB)});
        IRB.CreateCall(runtime.pushPathConstraint,
                       {constraint, IRB.getInt1(true)});
        IRB.CreateBr(finalDest);

        auto constraintCheckBlock =
            BasicBlock::Create(switchInst->getContext(), /* name */ "",
                               finalDest->getParent(), constraintBlock);
        IRB.SetInsertPoint(constraintCheckBlock);
        auto haveSymbolicCondition = IRB.CreateICmpNE(
            conditionExpr, ConstantPointerNull::get(IRB.getInt8PtrTy()));
        IRB.CreateCondBr(haveSymbolicCondition, /* then */ constraintBlock,
                         /* else */ finalDest);

        caseHandle.setSuccessor(constraintCheckBlock);
      }
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
      auto symbolicPHI = cast<PHINode>(symbolicExpressions[phi]);

      // A PHI node that receives only compile-time constants can be replaced by
      // a null expression.
      if (std::all_of(phi->op_begin(), phi->op_end(), [this](Value *input) {
            return (getSymbolicExpression(input) == nullptr);
          })) {
        symbolicPHI->replaceAllUsesWith(ConstantPointerNull::get(
            cast<PointerType>(symbolicPHI->getType())));
        symbolicPHI->eraseFromParent();
        // Replacing all uses will fix uses of the symbolic PHI node in existing
        // code, but the node may still be referenced via symbolicExpressions in
        // the generation of new code (e.g., if the current PHI is the input to
        // another PHI that we process later). Therefore, we need to delete it
        // from symbolicExpressions, indicating that the current PHI does not
        // have a symbolic expression.
        symbolicExpressions.erase(phi);
        continue;
      }

      for (unsigned incoming = 0, totalIncoming = phi->getNumIncomingValues();
           incoming < totalIncoming; incoming++) {
        symbolicPHI->addIncoming(
            getSymbolicExpressionOrNull(phi->getIncomingValue(incoming)),
            phi->getIncomingBlock(incoming));
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
      auto numUnknownConcreteness = std::count_if(
          symbolicComputation.inputs.begin(), symbolicComputation.inputs.end(),
          [&](const Input &input) {
            return (input.getSymbolicOperand() != nullExpression);
          });
      for (unsigned argIndex = 0; argIndex < symbolicComputation.inputs.size();
           argIndex++) {
        auto &argument = symbolicComputation.inputs[argIndex];
        auto originalArgExpression = argument.getSymbolicOperand();
        auto argCheckBlock = symbolicComputation.firstInstruction->getParent();

        // We only need a run-time check for concreteness if the argument isn't
        // known to be concrete at compile time already. However, there is one
        // exception: if the computation only has a single argument of unknown
        // concreteness, then we know that it must be symbolic since we ended up
        // in the slow path. Therefore, we can skip expression generation in
        // that case.
        bool needRuntimeCheck = originalArgExpression != nullExpression;
        if (needRuntimeCheck && (numUnknownConcreteness == 1))
          continue;

        if (needRuntimeCheck) {
          auto argExpressionBlock = SplitBlockAndInsertIfThen(
              nullChecks[argIndex], symbolicComputation.firstInstruction,
              /* unreachable */ false);
          IRB.SetInsertPoint(argExpressionBlock);
        } else {
          IRB.SetInsertPoint(symbolicComputation.firstInstruction);
        }

        auto newArgExpression =
            createValueExpression(argument.concreteValue, IRB);

        Value *finalArgExpression;
        if (needRuntimeCheck) {
          IRB.SetInsertPoint(symbolicComputation.firstInstruction);
          auto argPHI = IRB.CreatePHI(IRB.getInt8PtrTy(), 2);
          argPHI->addIncoming(originalArgExpression, argCheckBlock);
          argPHI->addIncoming(newArgExpression, newArgExpression->getParent());
          finalArgExpression = argPHI;
        } else {
          finalArgExpression = newArgExpression;
        }

        argument.user->replaceUsesOfWith(originalArgExpression,
                                         finalArgExpression);
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
      IRBuilder<> IRB(&I);

      tryAlternative(IRB, I.getOperand(0));
      tryAlternative(IRB, I.getOperand(1));
      tryAlternative(IRB, I.getOperand(2));

      IRB.CreateCall(runtime.memcpy,
                     {I.getOperand(0), I.getOperand(1), I.getOperand(2)});
      break;
    }
    case Intrinsic::memset: {
      IRBuilder<> IRB(&I);

      tryAlternative(IRB, I.getOperand(0));
      tryAlternative(IRB, I.getOperand(2));

      IRB.CreateCall(runtime.memset,
                     {I.getOperand(0),
                      getSymbolicExpressionOrNull(I.getOperand(1)),
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
    case Intrinsic::expect:
      // Just a hint for the optimizer; the value is the first parameter.
      if (auto expr = getSymbolicExpression(I.getArgOperand(0)))
        symbolicExpressions[&I] = expr;
      break;
    case Intrinsic::fabs: {
      // Floating-point absolute value; use the runtime to build the
      // corresponding symbolic expression.

      IRBuilder<> IRB(&I);
      auto abs = buildRuntimeCall(IRB, runtime.buildFloatAbs, I.getOperand(0));
      registerSymbolicComputation(abs, &I);
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
      symbolicExpressions[&I] =
          IRB.CreateCall(runtime.buildInteger,
                         {IRB.CreateZExt(&I, IRB.getInt64Ty()),
                          IRB.getInt8(I.getType()->getIntegerBitWidth())});
      break;
    }
    case Intrinsic::returnaddress: {
      // Obtain the return address of the current function or one of its parents
      // on the stack. We just concretize.

      errs() << "Warning: using concrete value for return address\n";
      IRBuilder<> IRB(I.getNextNode());
      symbolicExpressions[&I] = IRB.CreateCall(
          runtime.buildInteger,
          {IRB.CreatePtrToInt(&I, intPtrType), IRB.getInt8(ptrBits)});
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

    errs()
        << "Warning: losing track of symbolic expressions at inline assembly "
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
    Value *handler = runtime.binaryOperatorHandlers.at(I.getOpcode());

    // Special case: the run-time library distinguishes between "and" and "or"
    // on Boolean values and bit vectors.
    if (I.getOperand(0)->getType() == IRB.getInt1Ty()) {
      switch (I.getOpcode()) {
      case Instruction::And:
        handler = runtime.buildBoolAnd;
        break;
      case Instruction::Or:
        handler = runtime.buildBoolOr;
        break;
      default:
        llvm_unreachable("Unknown Boolean operator");
      }
    }

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
    auto runtimeCall =
        buildRuntimeCall(IRB, runtime.pushPathConstraint,
                         {{I.getCondition(), true}, {I.getCondition(), false}});
    registerSymbolicComputation(runtimeCall);
  }

  void visitCmpInst(CmpInst &I) {
    // ICmp is integer comparison, FCmp compares floating-point values; we
    // simply include either in the resulting expression.

    IRBuilder<> IRB(&I);
    Value *handler = runtime.comparisonHandlers.at(I.getPredicate());
    assert(handler && "Unable to handle icmp/fcmp variant");
    auto runtimeCall =
        buildRuntimeCall(IRB, handler, {I.getOperand(0), I.getOperand(1)});
    registerSymbolicComputation(runtimeCall, &I);
  }

  void visitReturnInst(ReturnInst &I) {
    // Upon return, we just store the expression for the return value.

    if (I.getReturnValue() == nullptr)
      return;

    // We can't short-circuit this call because the return expression needs to
    // be set even if it's null; otherwise we break the caller. Therefore,
    // create the call directly without registering it for short-circuit
    // processing.
    IRBuilder<> IRB(&I);
    IRB.CreateCall(runtime.setReturnExpression,
                   getSymbolicExpressionOrNull(I.getReturnValue()));
  }

  void visitBranchInst(BranchInst &I) {
    // Br can jump conditionally or unconditionally. We are only interested in
    // the former case, in which we push the branch condition or its negation to
    // the path constraints.

    if (I.isUnconditional())
      return;

    IRBuilder<> IRB(&I);
    auto runtimeCall =
        buildRuntimeCall(IRB, runtime.pushPathConstraint,
                         {{I.getCondition(), true}, {I.getCondition(), false}});
    registerSymbolicComputation(runtimeCall);
  }

  void visitIndirectBrInst(IndirectBrInst &I) {
    IRBuilder<> IRB(&I);
    tryAlternative(IRB, I.getAddress());
  }

  void visitCallInst(CallInst &I) {
    if (I.isInlineAsm()) {
      handleInlineAssembly(I);
      return;
    }

    auto callee = I.getCalledFunction();
    if (callee && callee->isIntrinsic()) {
      handleIntrinsicCall(I);
      return;
    }

    IRBuilder<> IRB(&I);

    if (!callee)
      tryAlternative(IRB, I.getCalledValue());

    auto targetName = callee ? callee->getName() : StringRef{};
    bool isMakeSymbolic = (targetName == "sym_make_symbolic");

    if (targetName.startswith("_sym_") && !isMakeSymbolic)
      return;

    if (!isMakeSymbolic) {
      for (Use &arg : I.args())
        IRB.CreateCall(runtime.setParameterExpression,
                       {ConstantInt::get(IRB.getInt8Ty(), arg.getOperandNo()),
                        getSymbolicExpressionOrNull(arg)});
    }

    if (!callee || isSymbolizedFunction(*callee)) {
      IRB.SetInsertPoint(I.getNextNode());
      symbolicExpressions[&I] = IRB.CreateCall(runtime.getReturnExpression);
    }
  }

  void visitAllocaInst(AllocaInst &I) {
    IRBuilder<> IRB(I.getNextNode());
    auto allocationLength = dataLayout.getTypeAllocSize(I.getAllocatedType());
    auto shadow =
        IRB.CreateAlloca(ArrayType::get(IRB.getInt8PtrTy(), allocationLength));
    IRB.CreateCall(runtime.registerMemory,
                   {IRB.CreatePtrToInt(&I, intPtrType),
                    IRB.CreateBitCast(shadow, IRB.getInt8PtrTy()),
                    ConstantInt::get(intPtrType, allocationLength)});
  }

  void visitLoadInst(LoadInst &I) {
    IRBuilder<> IRB(&I);

    auto addr = I.getPointerOperand();
    tryAlternative(IRB, addr);

    auto dataType = I.getType();
    auto data = IRB.CreateCall(
        runtime.readMemory,
        {IRB.CreatePtrToInt(addr, intPtrType),
         ConstantInt::get(intPtrType, dataLayout.getTypeStoreSize(dataType)),
         ConstantInt::get(IRB.getInt8Ty(), isLittleEndian(dataType) ? 1 : 0)});

    if (dataType->isFloatingPointTy()) {
      data = IRB.CreateCall(runtime.buildBitsToFloat,
                            {data, IRB.getInt1(dataType->isDoubleTy())});
    }

    symbolicExpressions[&I] = data;
  }

  void visitStoreInst(StoreInst &I) {
    IRBuilder<> IRB(&I);

    tryAlternative(IRB, I.getPointerOperand());

    auto data = getSymbolicExpressionOrNull(I.getValueOperand());
    auto dataType = I.getValueOperand()->getType();
    if (dataType->isFloatingPointTy()) {
      data = IRB.CreateCall(runtime.buildFloatToBits, data);
    }

    IRB.CreateCall(
        runtime.writeMemory,
        {IRB.CreatePtrToInt(I.getPointerOperand(), intPtrType),
         ConstantInt::get(intPtrType, dataLayout.getTypeStoreSize(dataType)),
         data,
         ConstantInt::get(IRB.getInt8Ty(),
                          dataLayout.isLittleEndian() ? 1 : 0)});
  }

  void visitGetElementPtrInst(GetElementPtrInst &I) {
    // GEP performs address calculations but never actually accesses memory. In
    // order to represent the result of a GEP symbolically, we start from the
    // symbolic expression of the original pointer and duplicate its
    // computations at the symbolic level.

    // If everything is compile-time concrete, we don't need to emit code.
    if (!getSymbolicExpression(I.getPointerOperand()) &&
        std::all_of(I.idx_begin(), I.idx_end(), [this](Value *index) {
          return (getSymbolicExpression(index) == nullptr);
        })) {
      return;
    }

    // If there are no indices or if they are all zero we can return early as
    // well.
    if (std::all_of(I.idx_begin(), I.idx_end(), [](Value *index) {
          auto ci = dyn_cast<ConstantInt>(index);
          return (ci && ci->isZero());
        })) {
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
        unsigned memberOffset = dataLayout.getStructLayout(structType)
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
            dataLayout.getTypeAllocSize(type_it.getIndexedType());
        if (auto indexWidth = index->getType()->getIntegerBitWidth();
            indexWidth != 64) {
          symbolicComputation.merge(forceBuildRuntimeCall(
              IRB, runtime.buildZExt,
              {{index, true},
               {ConstantInt::get(IRB.getInt8Ty(), 64 - indexWidth), false}}));
          symbolicComputation.merge(forceBuildRuntimeCall(
              IRB, runtime.binaryOperatorHandlers[Instruction::Mul],
              {{symbolicComputation.lastInstruction, false},
               {ConstantInt::get(IRB.getInt64Ty(), elementSize), true}}));
        } else {
          symbolicComputation.merge(forceBuildRuntimeCall(
              IRB, runtime.binaryOperatorHandlers[Instruction::Mul],
              {{index, true},
               {ConstantInt::get(IRB.getInt64Ty(), elementSize), true}}));
        }

        addressContribution = {symbolicComputation.lastInstruction, false};
      }

      symbolicComputation.merge(forceBuildRuntimeCall(
          IRB, runtime.binaryOperatorHandlers[Instruction::Add],
          {addressContribution,
           {currentAddress, (currentAddress == I.getPointerOperand())}}));
      currentAddress = symbolicComputation.lastInstruction;
    }

    registerSymbolicComputation(symbolicComputation, &I);
  }

  void visitBitCastInst(BitCastInst &I) {
    assert(I.getSrcTy()->isPointerTy() && I.getDestTy()->isPointerTy() &&
           "Unhandled non-pointer bit cast");
    if (auto expr = getSymbolicExpression(I.getOperand(0)))
      symbolicExpressions[&I] = expr;
  }

  void visitTruncInst(TruncInst &I) {
    IRBuilder<> IRB(&I);
    auto trunc = buildRuntimeCall(
        IRB, runtime.buildTrunc,
        {{I.getOperand(0), true},
         {IRB.getInt8(I.getDestTy()->getIntegerBitWidth()), false}});
    registerSymbolicComputation(trunc, &I);
  }

  void visitIntToPtrInst(IntToPtrInst &I) {
    if (auto expr = getSymbolicExpression(I.getOperand(0)))
      symbolicExpressions[&I] = expr;
    // TODO handle truncation and zero extension
  }

  void visitPtrToIntInst(PtrToIntInst &I) {
    if (auto expr = getSymbolicExpression(I.getOperand(0)))
      symbolicExpressions[&I] = expr;
    // TODO handle truncation and zero extension
  }

  void visitSIToFPInst(SIToFPInst &I) {
    IRBuilder<> IRB(&I);
    auto conversion =
        buildRuntimeCall(IRB, runtime.buildIntToFloat,
                         {{I.getOperand(0), true},
                          {IRB.getInt1(I.getDestTy()->isDoubleTy()), false},
                          {/* is_signed */ IRB.getInt1(true), false}});
    registerSymbolicComputation(conversion, &I);
  }

  void visitUIToFPInst(UIToFPInst &I) {
    IRBuilder<> IRB(&I);
    auto conversion =
        buildRuntimeCall(IRB, runtime.buildIntToFloat,
                         {{I.getOperand(0), true},
                          {IRB.getInt1(I.getDestTy()->isDoubleTy()), false},
                          {/* is_signed */ IRB.getInt1(false), false}});
    registerSymbolicComputation(conversion, &I);
  }

  void visitFPExtInst(FPExtInst &I) {
    IRBuilder<> IRB(&I);
    auto conversion =
        buildRuntimeCall(IRB, runtime.buildFloatToFloat,
                         {{I.getOperand(0), true},
                          {IRB.getInt1(I.getDestTy()->isDoubleTy()), false}});
    registerSymbolicComputation(conversion, &I);
  }

  void visitFPTruncInst(FPTruncInst &I) {
    IRBuilder<> IRB(&I);
    auto conversion =
        buildRuntimeCall(IRB, runtime.buildFloatToFloat,
                         {{I.getOperand(0), true},
                          {IRB.getInt1(I.getDestTy()->isDoubleTy()), false}});
    registerSymbolicComputation(conversion, &I);
  }

  void visitFPToSI(FPToSIInst &I) {
    IRBuilder<> IRB(&I);
    auto conversion = buildRuntimeCall(
        IRB, runtime.buildFloatToSignedInt,
        {{I.getOperand(0), true},
         {IRB.getInt8(I.getType()->getIntegerBitWidth()), false}});
    registerSymbolicComputation(conversion, &I);
  }

  void visitFPToUI(FPToUIInst &I) {
    IRBuilder<> IRB(&I);
    auto conversion = buildRuntimeCall(
        IRB, runtime.buildFloatToUnsignedInt,
        {{I.getOperand(0), true},
         {IRB.getInt8(I.getType()->getIntegerBitWidth()), false}});
    registerSymbolicComputation(conversion, &I);
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
      if (auto expr = getSymbolicExpression(I.getOperand(0))) {
        symbolicExpressions[&I] = expr;
      }
    } else {
      Value *target;

      switch (I.getOpcode()) {
      case Instruction::SExt:
        target = runtime.buildSExt;
        break;
      case Instruction::ZExt:
        target = runtime.buildZExt;
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
            dataLayout.getStructLayout(structType)->getElementOffset(index);
        indexedType = structType->getElementType(index);
      } else {
        auto arrayType = cast<ArrayType>(indexedType);
        unsigned elementSize =
            dataLayout.getTypeAllocSize(arrayType->getArrayElementType());
        offset += elementSize * index;
        indexedType = arrayType->getArrayElementType();
      }
    }

    IRBuilder<> IRB(&I);
    auto extract = buildRuntimeCall(
        IRB, runtime.buildExtract,
        {{I.getAggregateOperand(), true},
         {IRB.getInt64(offset), false},
         {IRB.getInt64(dataLayout.getTypeStoreSize(I.getType())), false},
         {IRB.getInt8(isLittleEndian(I.getType()) ? 1 : 0), false}});
    registerSymbolicComputation(extract, &I);
  }

  void visitSwitchInst(SwitchInst &I) {
    // Switch compares a value against a set of integer constants; duplicate
    // constants are not allowed
    // (https://llvm.org/docs/LangRef.html#switch-instruction). We can't just
    // push the new path constraint at the jump destinations, because the
    // destination blocks may be the targets of other jumps as well. Therefore,
    // we insert a series of new blocks that construct and push the respective
    // path constraint before jumping to the original target. Since inserting
    // new basic blocks confuses InstVisitor, we have to defer processing of the
    // instruction.

    switchInstructions.push_back(&I);
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
      out << "\nComputation starting at " << *computation.firstInstruction
          << "\n...ending at " << *computation.lastInstruction
          << "\n...with inputs:\n";
      for (const auto &input : computation.inputs) {
        out << '\t' << *input.concreteValue << '\n';
      }
      return out;
    }
  };

  /// Create an expression that represents the concrete value.
  CallInst *createValueExpression(Value *V, IRBuilder<> &IRB) {
    auto valueType = V->getType();

    if (isa<ConstantPointerNull>(V)) {
      return IRB.CreateCall(runtime.buildNullPointer, {});
    }

    if (valueType->isIntegerTy()) {
      // Special case: LLVM uses the type i1 to represent Boolean values, but
      // for Z3 we have to create expressions of a separate sort.
      if (valueType->getPrimitiveSizeInBits() == 1) {
        return IRB.CreateCall(runtime.buildBool, {V});
      }

      return IRB.CreateCall(runtime.buildInteger,
                            {IRB.CreateZExtOrBitCast(V, IRB.getInt64Ty()),
                             IRB.getInt8(valueType->getPrimitiveSizeInBits())});
    }

    if (valueType->isFloatingPointTy()) {
      return IRB.CreateCall(runtime.buildFloat,
                            {IRB.CreateFPCast(V, IRB.getDoubleTy()),
                             IRB.getInt1(valueType->isDoubleTy())});
    }

    if (valueType->isPointerTy()) {
      return IRB.CreateCall(
          runtime.buildInteger,
          {IRB.CreatePtrToInt(V, IRB.getInt64Ty()), IRB.getInt8(ptrBits)});
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
          runtime.readMemory,
          {IRB.CreatePtrToInt(memory, intPtrType),
           ConstantInt::get(intPtrType,
                            dataLayout.getTypeStoreSize(V->getType())),
           IRB.getInt8(0)});
    }

    llvm_unreachable("Unhandled type for constant expression");
  }

  /// Get the (already created) symbolic expression for a value.
  Value *getSymbolicExpression(Value *V) {
    auto exprIt = symbolicExpressions.find(V);
    return (exprIt != symbolicExpressions.end()) ? exprIt->second : nullptr;
  }

  Value *getSymbolicExpressionOrNull(Value *V) {
    auto expr = getSymbolicExpression(V);
    if (!expr)
      return ConstantPointerNull::get(
          IntegerType::getInt8PtrTy(V->getContext()));
    return expr;
  }

  bool isLittleEndian(Type *type) {
    return (!type->isAggregateType() && dataLayout.isLittleEndian());
  }

  /// Like buildRuntimeCall, but the call is always generated.
  SymbolicComputation
  forceBuildRuntimeCall(IRBuilder<> &IRB, Value *function,
                        ArrayRef<std::pair<Value *, bool>> args) {
    std::vector<Value *> functionArgs;
    for (auto &[arg, symbolic] : args) {
      functionArgs.push_back(symbolic ? getSymbolicExpressionOrNull(arg) : arg);
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

  /// Create a call to the specified function in the run-time library.
  ///
  /// Each argument is specified as a pair of Value and Boolean. The Boolean
  /// specifies whether the Value is a symbolic argument, in which case the
  /// corresponding symbolic expression will be passed to the run-time function.
  /// Moreover, the use of symbolic expressions will be recorded in the
  /// resulting SymbolicComputation. If all symbolic arguments are known to be
  /// concrete (e.g., because they are compile-time constants), no call
  /// instruction is emitted and the function returns null.
  std::optional<SymbolicComputation>
  buildRuntimeCall(IRBuilder<> &IRB, Value *function,
                   ArrayRef<std::pair<Value *, bool>> args) {
    if (std::all_of(args.begin(), args.end(),
                    [this](std::pair<Value *, bool> arg) {
                      return (getSymbolicExpression(arg.first) == nullptr);
                    })) {
      return {};
    }

    return forceBuildRuntimeCall(IRB, function, args);
  }

  /// Convenience overload that treats all arguments as symbolic.
  std::optional<SymbolicComputation>
  buildRuntimeCall(IRBuilder<> &IRB, Value *function,
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
                                   Value *concrete = nullptr) {
    if (concrete) {
      symbolicExpressions[concrete] = computation.lastInstruction;
    }

    expressionUses.push_back(computation);
  }

  /// Convenience overload for chaining with buildRuntimeCall.
  void registerSymbolicComputation(
      const std::optional<SymbolicComputation> &computation,
      Value *concrete = nullptr) {
    if (computation)
      registerSymbolicComputation(*computation, concrete);
  }

  /// Generate code that makes the solver try an alternative value for V.
  void tryAlternative(IRBuilder<> &IRB, Value *V) {
    auto destExpr = getSymbolicExpression(V);
    if (destExpr) {
      auto concreteDestExpr = createValueExpression(V, IRB);
      auto destAssertion =
          IRB.CreateCall(runtime.comparisonHandlers[CmpInst::ICMP_EQ],
                         {destExpr, concreteDestExpr});
      auto pushAssertion = IRB.CreateCall(runtime.pushPathConstraint,
                                          {destAssertion, IRB.getInt1(true)});
      registerSymbolicComputation(SymbolicComputation(
          concreteDestExpr, pushAssertion, {{V, 0, destAssertion}}));
    }
  }

  const Runtime runtime;

  /// The data layout of the currently processed module.
  const DataLayout &dataLayout;

  /// The width in bits of pointers in the module.
  unsigned ptrBits;

  /// An integer type at least as wide as a pointer.
  IntegerType *intPtrType;

  /// Mapping from SSA values to symbolic expressions.
  ///
  /// For pointer values, the stored value is an expression describing the value
  /// of the pointer itself (i.e., the address, not the referenced value). For
  /// structure values, the expression is a single large bit vector.
  ValueMap<Value *, Value *> symbolicExpressions;

  /// A record of switch instructions in this function.
  ///
  /// The way we currently handle switch statements requires inserting new basic
  /// blocks, which would confuse InstVisitor if we did it while iterating over
  /// the function for the first time. Therefore, we just collect all switch
  /// instructions and finish them later in finalizeSwitchInstructions.
  SmallVector<SwitchInst *, 8> switchInstructions;

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
static struct RegisterStandardPasses Y(PassManagerBuilder::EP_VectorizerStart,
                                       addSymbolizePass);
static struct RegisterStandardPasses
    Z(PassManagerBuilder::EP_EnabledOnOptLevel0, addSymbolizePass);

bool SymbolizePass::doInitialization(Module &M) {
  DEBUG(errs() << "Symbolizer module init\n");

  // Redirect calls to external functions to the corresponding wrappers and
  // rename internal functions.
  for (auto &function : M.functions()) {
    auto name = function.getName();
    if (!isSymbolizedFunction(function) || name == "main" ||
        name.startswith("llvm.") || name == "sym_make_symbolic")
      continue;

    function.setName("__symbolized_" + name);
  }

  Runtime runtime(M);
  IRBuilder<> IRB(M.getContext());
  const DataLayout &dataLayout = M.getDataLayout();

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
      auto valueLength = dataLayout.getTypeAllocSize(global.getValueType());
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
  auto intPtrType = dataLayout.getIntPtrType(M.getContext());
  for (auto &&[value, expression] : globalExpressions) {
    // External globals are initialized in their respective module.
    if (value->isDeclaration())
      continue;

    IRB.CreateCall(
        runtime.initializeMemory,
        {IRB.CreatePtrToInt(value, intPtrType),
         IRB.CreateBitCast(expression, IRB.getInt8PtrTy()),
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

  Symbolizer symbolizer(*F.getParent());
  // DEBUG(errs() << F << '\n');
  symbolizer.symbolizeFunctionArguments(F);
  symbolizer.visit(F);
  symbolizer.finalizeSwitchInstructions();
  symbolizer.finalizePHINodes();
  symbolizer.shortCircuitExpressionUses();
  assert(!verifyFunction(F, &errs()) &&
         "SymbolizePass produced invalid bitcode");

  return true;
}
