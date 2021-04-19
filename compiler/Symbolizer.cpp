// This file is part of SymCC.
//
// SymCC is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// SymCC is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
// A PARTICULAR PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// SymCC. If not, see <https://www.gnu.org/licenses/>.

#include "Symbolizer.h"

#include <cstdint>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GetElementPtrTypeIterator.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/Transforms/Utils/BasicBlockUtils.h>

#include "Runtime.h"

using namespace llvm;

void Symbolizer::symbolizeFunctionArguments(Function &F) {
  // The main function doesn't receive symbolic arguments.
  if (F.getName() == "main")
    return;

  IRBuilder<> IRB(F.getEntryBlock().getFirstNonPHI());

  for (auto &arg : F.args()) {
    if (!arg.user_empty())
      symbolicExpressions[&arg] = IRB.CreateCall(runtime.getParameterExpression,
                                                 IRB.getInt8(arg.getArgNo()));
  }
}

void Symbolizer::insertBasicBlockNotification(llvm::BasicBlock &B) {
  IRBuilder<> IRB(&*B.getFirstInsertionPt());
  IRB.CreateCall(runtime.notifyBasicBlock, getTargetPreferredInt(&B));
}

void Symbolizer::finalizePHINodes() {
  SmallPtrSet<PHINode *, 32> nodesToErase;

  for (auto *phi : phiNodes) {
    auto symbolicPHI = cast<PHINode>(symbolicExpressions[phi]);

    // A PHI node that receives only compile-time constants can be replaced by
    // a null expression.
    if (std::all_of(phi->op_begin(), phi->op_end(), [this](Value *input) {
          return (getSymbolicExpression(input) == nullptr);
        })) {
      nodesToErase.insert(symbolicPHI);
      continue;
    }

    for (unsigned incoming = 0, totalIncoming = phi->getNumIncomingValues();
         incoming < totalIncoming; incoming++) {
      symbolicPHI->setIncomingValue(
          incoming,
          getSymbolicExpressionOrNull(phi->getIncomingValue(incoming)));
    }
  }

  for (auto *symbolicPHI : nodesToErase) {
    symbolicPHI->replaceAllUsesWith(
        ConstantPointerNull::get(cast<PointerType>(symbolicPHI->getType())));
    symbolicPHI->eraseFromParent();
  }

  // Replacing all uses has fixed uses of the symbolic PHI nodes in existing
  // code, but the nodes may still be referenced via symbolicExpressions. We
  // therefore invalidate symbolicExpressions, meaning that it cannot be used
  // after this point.
  symbolicExpressions.clear();
}

void Symbolizer::shortCircuitExpressionUses() {
  for (auto &symbolicComputation : expressionUses) {
    assert(!symbolicComputation.inputs.empty() &&
           "Symbolic computation has no inputs");

    IRBuilder<> IRB(symbolicComputation.firstInstruction);

    // Build the check whether any input expression is non-null (i.e., there
    // is a symbolic input).
    auto *nullExpression = ConstantPointerNull::get(IRB.getInt8PtrTy());
    std::vector<Value *> nullChecks;
    for (const auto &input : symbolicComputation.inputs) {
      nullChecks.push_back(
          IRB.CreateICmpEQ(nullExpression, input.getSymbolicOperand()));
    }
    auto *allConcrete = nullChecks[0];
    for (unsigned argIndex = 1; argIndex < nullChecks.size(); argIndex++) {
      allConcrete = IRB.CreateAnd(allConcrete, nullChecks[argIndex]);
    }

    // The main branch: if we don't enter here, we can short-circuit the
    // symbolic computation. Otherwise, we need to check all input expressions
    // and create an output expression.
    auto *head = symbolicComputation.firstInstruction->getParent();
    auto *slowPath = SplitBlock(head, symbolicComputation.firstInstruction);
    auto *tail = SplitBlock(slowPath,
                            symbolicComputation.lastInstruction->getNextNode());
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
      auto *originalArgExpression = argument.getSymbolicOperand();
      auto *argCheckBlock = symbolicComputation.firstInstruction->getParent();

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
        auto *argExpressionBlock = SplitBlockAndInsertIfThen(
            nullChecks[argIndex], symbolicComputation.firstInstruction,
            /* unreachable */ false);
        IRB.SetInsertPoint(argExpressionBlock);
      } else {
        IRB.SetInsertPoint(symbolicComputation.firstInstruction);
      }

      auto *newArgExpression =
          createValueExpression(argument.concreteValue, IRB);

      Value *finalArgExpression;
      if (needRuntimeCheck) {
        IRB.SetInsertPoint(symbolicComputation.firstInstruction);
        auto *argPHI = IRB.CreatePHI(IRB.getInt8PtrTy(), 2);
        argPHI->addIncoming(originalArgExpression, argCheckBlock);
        argPHI->addIncoming(newArgExpression, newArgExpression->getParent());
        finalArgExpression = argPHI;
      } else {
        finalArgExpression = newArgExpression;
      }

      argument.replaceOperand(finalArgExpression);
    }

    // Finally, the overall result (if the computation produces one) is null
    // if we've taken the fast path and the symbolic expression computed above
    // if short-circuiting wasn't possible.
    if (!symbolicComputation.lastInstruction->use_empty()) {
      IRB.SetInsertPoint(&tail->front());
      auto *finalExpression = IRB.CreatePHI(IRB.getInt8PtrTy(), 2);
      symbolicComputation.lastInstruction->replaceAllUsesWith(finalExpression);
      finalExpression->addIncoming(ConstantPointerNull::get(IRB.getInt8PtrTy()),
                                   head);
      finalExpression->addIncoming(
          symbolicComputation.lastInstruction,
          symbolicComputation.lastInstruction->getParent());
    }
  }
}

void Symbolizer::handleIntrinsicCall(CallBase &I) {
  auto *callee = I.getCalledFunction();

  switch (callee->getIntrinsicID()) {
  case Intrinsic::lifetime_start:
  case Intrinsic::lifetime_end:
  case Intrinsic::dbg_declare:
  case Intrinsic::dbg_value:
  case Intrinsic::is_constant:
  case Intrinsic::trap:
  case Intrinsic::invariant_start:
  case Intrinsic::invariant_end:
  case Intrinsic::assume:
    // These are safe to ignore.
    break;
  case Intrinsic::memcpy: {
    IRBuilder<> IRB(&I);

    tryAlternative(IRB, I.getOperand(0));
    tryAlternative(IRB, I.getOperand(1));
    tryAlternative(IRB, I.getOperand(2));

    // The intrinsic allows both 32 and 64-bit integers to specify the length;
    // convert to the right type if necessary. This may truncate the value on
    // 32-bit architectures. However, what's the point of specifying a length to
    // memcpy that is larger than your address space?

    IRB.CreateCall(runtime.memcpy,
                   {I.getOperand(0), I.getOperand(1),
                    IRB.CreateZExtOrTrunc(I.getOperand(2), intPtrType)});
    break;
  }
  case Intrinsic::memset: {
    IRBuilder<> IRB(&I);

    tryAlternative(IRB, I.getOperand(0));
    tryAlternative(IRB, I.getOperand(2));

    // The comment on memcpy's length parameter applies analogously.

    IRB.CreateCall(runtime.memset,
                   {I.getOperand(0),
                    getSymbolicExpressionOrNull(I.getOperand(1)),
                    IRB.CreateZExtOrTrunc(I.getOperand(2), intPtrType)});
    break;
  }
  case Intrinsic::memmove: {
    IRBuilder<> IRB(&I);

    tryAlternative(IRB, I.getOperand(0));
    tryAlternative(IRB, I.getOperand(1));
    tryAlternative(IRB, I.getOperand(2));

    // The comment on memcpy's length parameter applies analogously.

    IRB.CreateCall(runtime.memmove,
                   {I.getOperand(0), I.getOperand(1),
                    IRB.CreateZExtOrTrunc(I.getOperand(2), intPtrType)});
    break;
  }
  case Intrinsic::stacksave: {
    // The intrinsic returns an opaque pointer that should only be passed to
    // the stackrestore intrinsic later. We treat the pointer as a constant.
    break;
  }
  case Intrinsic::stackrestore:
    // Ignored; see comment on stacksave above.
    break;
  case Intrinsic::expect:
    // Just a hint for the optimizer; the value is the first parameter.
    if (auto *expr = getSymbolicExpression(I.getArgOperand(0)))
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
    break;
  }
  case Intrinsic::returnaddress: {
    // Obtain the return address of the current function or one of its parents
    // on the stack. We just concretize.

    errs() << "Warning: using concrete value for return address\n";
    break;
  }
  case Intrinsic::bswap: {
    // Bswap changes the endian-ness of integer values.

    IRBuilder<> IRB(&I);
    auto swapped = buildRuntimeCall(IRB, runtime.buildBswap, I.getOperand(0));
    registerSymbolicComputation(swapped, &I);
    break;
  }
  default:
    errs() << "Warning: unhandled LLVM intrinsic " << callee->getName()
           << "; the result will be concretized\n";
    break;
  }
}

void Symbolizer::handleInlineAssembly(CallInst &I) {
  if (I.getType()->isVoidTy()) {
    errs() << "Warning: skipping over inline assembly " << I << '\n';
    return;
  }

  errs() << "Warning: losing track of symbolic expressions at inline assembly "
         << I << '\n';
}

void Symbolizer::handleFunctionCall(CallBase &I, Instruction *returnPoint) {
  auto *callee = I.getCalledFunction();
  if (callee != nullptr && callee->isIntrinsic()) {
    handleIntrinsicCall(I);
    return;
  }

  IRBuilder<> IRB(returnPoint);
  IRB.CreateCall(runtime.notifyRet, getTargetPreferredInt(&I));
  IRB.SetInsertPoint(&I);
  IRB.CreateCall(runtime.notifyCall, getTargetPreferredInt(&I));

  if (callee == nullptr)
    tryAlternative(IRB, I.getCalledOperand());

  for (Use &arg : I.args())
    IRB.CreateCall(runtime.setParameterExpression,
                   {ConstantInt::get(IRB.getInt8Ty(), arg.getOperandNo()),
                    getSymbolicExpressionOrNull(arg)});

  if (!I.user_empty()) {
    // The result of the function is used somewhere later on. Since we have no
    // way of knowing whether the function is instrumented (and thus sets a
    // proper return expression), we have to account for the possibility that
    // it's not: in that case, we'll have to treat the result as an opaque
    // concrete value. Therefore, we set the return expression to null here in
    // order to avoid accidentally using whatever is stored there from the
    // previous function call. (If the function is instrumented, it will just
    // override our null with the real expression.)
    IRB.CreateCall(runtime.setReturnExpression,
                   ConstantPointerNull::get(IRB.getInt8PtrTy()));
    IRB.SetInsertPoint(returnPoint);
    symbolicExpressions[&I] = IRB.CreateCall(runtime.getReturnExpression);
  }
}

void Symbolizer::visitBinaryOperator(BinaryOperator &I) {
  // Binary operators propagate into the symbolic expression.

  IRBuilder<> IRB(&I);
  SymFnT handler = runtime.binaryOperatorHandlers.at(I.getOpcode());

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
    case Instruction::Xor:
      handler = runtime.buildBoolXor;
      break;
    default:
      errs() << "Can't handle Boolean operator " << I << '\n';
      llvm_unreachable("Unknown Boolean operator");
      break;
    }
  }

  assert(handler && "Unable to handle binary operator");
  auto runtimeCall =
      buildRuntimeCall(IRB, handler, {I.getOperand(0), I.getOperand(1)});
  registerSymbolicComputation(runtimeCall, &I);
}

void Symbolizer::visitSelectInst(SelectInst &I) {
  // Select is like the ternary operator ("?:") in C. We push the (potentially
  // negated) condition to the path constraints and copy the symbolic
  // expression over from the chosen argument.

  IRBuilder<> IRB(&I);
  auto runtimeCall = buildRuntimeCall(IRB, runtime.pushPathConstraint,
                                      {{I.getCondition(), true},
                                       {I.getCondition(), false},
                                       {getTargetPreferredInt(&I), false}});
  registerSymbolicComputation(runtimeCall);
}

void Symbolizer::visitCmpInst(CmpInst &I) {
  // ICmp is integer comparison, FCmp compares floating-point values; we
  // simply include either in the resulting expression.

  IRBuilder<> IRB(&I);
  SymFnT handler = runtime.comparisonHandlers.at(I.getPredicate());
  assert(handler && "Unable to handle icmp/fcmp variant");
  auto runtimeCall =
      buildRuntimeCall(IRB, handler, {I.getOperand(0), I.getOperand(1)});
  registerSymbolicComputation(runtimeCall, &I);
}

void Symbolizer::visitReturnInst(ReturnInst &I) {
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

void Symbolizer::visitBranchInst(BranchInst &I) {
  // Br can jump conditionally or unconditionally. We are only interested in
  // the former case, in which we push the branch condition or its negation to
  // the path constraints.

  if (I.isUnconditional())
    return;

  IRBuilder<> IRB(&I);
  auto runtimeCall = buildRuntimeCall(IRB, runtime.pushPathConstraint,
                                      {{I.getCondition(), true},
                                       {I.getCondition(), false},
                                       {getTargetPreferredInt(&I), false}});
  registerSymbolicComputation(runtimeCall);
}

void Symbolizer::visitIndirectBrInst(IndirectBrInst &I) {
  IRBuilder<> IRB(&I);
  tryAlternative(IRB, I.getAddress());
}

void Symbolizer::visitCallInst(CallInst &I) {
  if (I.isInlineAsm())
    handleInlineAssembly(I);
  else
    handleFunctionCall(I, I.getNextNode());
}

void Symbolizer::visitInvokeInst(InvokeInst &I) {
  // Invoke is like a call but additionally establishes an exception handler. We
  // can obtain the return expression only in the success case, but the target
  // block may have multiple incoming edges (i.e., our edge may be critical). In
  // this case, we split the edge and query the return expression in the new
  // block that is specific to our edge.
  auto *newBlock = SplitCriticalEdge(I.getParent(), I.getNormalDest());
  handleFunctionCall(I, newBlock != nullptr
                            ? newBlock->getFirstNonPHI()
                            : I.getNormalDest()->getFirstNonPHI());
}

void Symbolizer::visitAllocaInst(AllocaInst & /*unused*/) {
  // Nothing to do: the shadow for the newly allocated memory region will be
  // created on first write; until then, the memory contents are concrete.
}

void Symbolizer::visitLoadInst(LoadInst &I) {
  IRBuilder<> IRB(&I);

  auto *addr = I.getPointerOperand();
  tryAlternative(IRB, addr);

  auto *dataType = I.getType();
  auto *data = IRB.CreateCall(
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

void Symbolizer::visitStoreInst(StoreInst &I) {
  IRBuilder<> IRB(&I);

  tryAlternative(IRB, I.getPointerOperand());

  auto *data = getSymbolicExpressionOrNull(I.getValueOperand());
  auto *dataType = I.getValueOperand()->getType();
  if (dataType->isFloatingPointTy()) {
    data = IRB.CreateCall(runtime.buildFloatToBits, data);
  }

  IRB.CreateCall(
      runtime.writeMemory,
      {IRB.CreatePtrToInt(I.getPointerOperand(), intPtrType),
       ConstantInt::get(intPtrType, dataLayout.getTypeStoreSize(dataType)),
       data,
       ConstantInt::get(IRB.getInt8Ty(), dataLayout.isLittleEndian() ? 1 : 0)});
}

void Symbolizer::visitGetElementPtrInst(GetElementPtrInst &I) {
  // GEP performs address calculations but never actually accesses memory. In
  // order to represent the result of a GEP symbolically, we start from the
  // symbolic expression of the original pointer and duplicate its
  // computations at the symbolic level.

  // If everything is compile-time concrete, we don't need to emit code.
  if (getSymbolicExpression(I.getPointerOperand()) == nullptr &&
      std::all_of(I.idx_begin(), I.idx_end(), [this](Value *index) {
        return (getSymbolicExpression(index) == nullptr);
      })) {
    return;
  }

  // If there are no indices or if they are all zero we can return early as
  // well.
  if (std::all_of(I.idx_begin(), I.idx_end(), [](Value *index) {
        auto *ci = dyn_cast<ConstantInt>(index);
        return (ci != nullptr && ci->isZero());
      })) {
    symbolicExpressions[&I] = getSymbolicExpression(I.getPointerOperand());
    return;
  }

  IRBuilder<> IRB(&I);
  SymbolicComputation symbolicComputation;
  Value *currentAddress = I.getPointerOperand();

  for (auto type_it = gep_type_begin(I), type_end = gep_type_end(I);
       type_it != type_end; ++type_it) {
    auto *index = type_it.getOperand();
    std::pair<Value *, bool> addressContribution;

    // There are two cases for the calculation:
    // 1. If the indexed type is a struct, we need to add the offset of the
    //    desired member.
    // 2. If it is an array or a pointer, compute the offset of the desired
    //    element.
    if (auto *structType = type_it.getStructTypeOrNull()) {
      // Structs can only be indexed with constants
      // (https://llvm.org/docs/LangRef.html#getelementptr-instruction).

      unsigned memberIndex = cast<ConstantInt>(index)->getZExtValue();
      unsigned memberOffset =
          dataLayout.getStructLayout(structType)->getElementOffset(memberIndex);
      addressContribution = {ConstantInt::get(intPtrType, memberOffset), true};
    } else {
      if (auto *ci = dyn_cast<ConstantInt>(index);
          ci != nullptr && ci->isZero()) {
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
          indexWidth != ptrBits) {
        symbolicComputation.merge(forceBuildRuntimeCall(
            IRB, runtime.buildZExt,
            {{index, true},
             {ConstantInt::get(IRB.getInt8Ty(), ptrBits - indexWidth),
              false}}));
        symbolicComputation.merge(forceBuildRuntimeCall(
            IRB, runtime.binaryOperatorHandlers[Instruction::Mul],
            {{symbolicComputation.lastInstruction, false},
             {ConstantInt::get(intPtrType, elementSize), true}}));
      } else {
        symbolicComputation.merge(forceBuildRuntimeCall(
            IRB, runtime.binaryOperatorHandlers[Instruction::Mul],
            {{index, true},
             {ConstantInt::get(intPtrType, elementSize), true}}));
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

void Symbolizer::visitBitCastInst(BitCastInst &I) {
  if (I.getSrcTy()->isIntegerTy() && I.getDestTy()->isFloatingPointTy()) {
    IRBuilder<> IRB(&I);
    auto conversion =
        buildRuntimeCall(IRB, runtime.buildBitsToFloat,
                         {{I.getOperand(0), true},
                          {IRB.getInt1(I.getDestTy()->isDoubleTy()), false}});
    registerSymbolicComputation(conversion, &I);
    return;
  }

  if (I.getSrcTy()->isFloatingPointTy() && I.getDestTy()->isIntegerTy()) {
    IRBuilder<> IRB(&I);
    auto conversion = buildRuntimeCall(IRB, runtime.buildFloatToBits,
                                       {{I.getOperand(0), true}});
    registerSymbolicComputation(conversion);
    return;
  }

  assert(I.getSrcTy()->isPointerTy() && I.getDestTy()->isPointerTy() &&
         "Unhandled non-pointer bit cast");
  if (auto *expr = getSymbolicExpression(I.getOperand(0)))
    symbolicExpressions[&I] = expr;
}

void Symbolizer::visitTruncInst(TruncInst &I) {
  IRBuilder<> IRB(&I);
  auto trunc = buildRuntimeCall(
      IRB, runtime.buildTrunc,
      {{I.getOperand(0), true},
       {IRB.getInt8(I.getDestTy()->getIntegerBitWidth()), false}});
  registerSymbolicComputation(trunc, &I);
}

void Symbolizer::visitIntToPtrInst(IntToPtrInst &I) {
  if (auto *expr = getSymbolicExpression(I.getOperand(0)))
    symbolicExpressions[&I] = expr;
  // TODO handle truncation and zero extension
}

void Symbolizer::visitPtrToIntInst(PtrToIntInst &I) {
  if (auto *expr = getSymbolicExpression(I.getOperand(0)))
    symbolicExpressions[&I] = expr;
  // TODO handle truncation and zero extension
}

void Symbolizer::visitSIToFPInst(SIToFPInst &I) {
  IRBuilder<> IRB(&I);
  auto conversion =
      buildRuntimeCall(IRB, runtime.buildIntToFloat,
                       {{I.getOperand(0), true},
                        {IRB.getInt1(I.getDestTy()->isDoubleTy()), false},
                        {/* is_signed */ IRB.getInt1(true), false}});
  registerSymbolicComputation(conversion, &I);
}

void Symbolizer::visitUIToFPInst(UIToFPInst &I) {
  IRBuilder<> IRB(&I);
  auto conversion =
      buildRuntimeCall(IRB, runtime.buildIntToFloat,
                       {{I.getOperand(0), true},
                        {IRB.getInt1(I.getDestTy()->isDoubleTy()), false},
                        {/* is_signed */ IRB.getInt1(false), false}});
  registerSymbolicComputation(conversion, &I);
}

void Symbolizer::visitFPExtInst(FPExtInst &I) {
  IRBuilder<> IRB(&I);
  auto conversion =
      buildRuntimeCall(IRB, runtime.buildFloatToFloat,
                       {{I.getOperand(0), true},
                        {IRB.getInt1(I.getDestTy()->isDoubleTy()), false}});
  registerSymbolicComputation(conversion, &I);
}

void Symbolizer::visitFPTruncInst(FPTruncInst &I) {
  IRBuilder<> IRB(&I);
  auto conversion =
      buildRuntimeCall(IRB, runtime.buildFloatToFloat,
                       {{I.getOperand(0), true},
                        {IRB.getInt1(I.getDestTy()->isDoubleTy()), false}});
  registerSymbolicComputation(conversion, &I);
}

void Symbolizer::visitFPToSI(FPToSIInst &I) {
  IRBuilder<> IRB(&I);
  auto conversion = buildRuntimeCall(
      IRB, runtime.buildFloatToSignedInt,
      {{I.getOperand(0), true},
       {IRB.getInt8(I.getType()->getIntegerBitWidth()), false}});
  registerSymbolicComputation(conversion, &I);
}

void Symbolizer::visitFPToUI(FPToUIInst &I) {
  IRBuilder<> IRB(&I);
  auto conversion = buildRuntimeCall(
      IRB, runtime.buildFloatToUnsignedInt,
      {{I.getOperand(0), true},
       {IRB.getInt8(I.getType()->getIntegerBitWidth()), false}});
  registerSymbolicComputation(conversion, &I);
}

void Symbolizer::visitCastInst(CastInst &I) {
  auto opcode = I.getOpcode();
  if (opcode != Instruction::SExt && opcode != Instruction::ZExt) {
    errs() << "Warning: unhandled cast instruction " << I << '\n';
    return;
  }

  IRBuilder<> IRB(&I);

  // LLVM bitcode represents Boolean values as i1. In Z3, those are a not a
  // bit-vector sort, so trying to cast one into a bit vector of any length
  // raises an error. The run-time library provides a dedicated conversion
  // function for this case.
  if (I.getSrcTy()->getIntegerBitWidth() == 1) {
    auto boolToBitConversion = buildRuntimeCall(
        IRB, runtime.buildBoolToBits,
        {{I.getOperand(0), true},
         {IRB.getInt8(I.getDestTy()->getIntegerBitWidth()), false}});
    registerSymbolicComputation(boolToBitConversion, &I);
  } else {
    SymFnT target;

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

void Symbolizer::visitPHINode(PHINode &I) {
  // PHI nodes just assign values based on the origin of the last jump, so we
  // assign the corresponding symbolic expression the same way.

  phiNodes.push_back(&I); // to be finalized later, see finalizePHINodes

  IRBuilder<> IRB(&I);
  unsigned numIncomingValues = I.getNumIncomingValues();
  auto *exprPHI = IRB.CreatePHI(IRB.getInt8PtrTy(), numIncomingValues);
  for (unsigned incoming = 0; incoming < numIncomingValues; incoming++) {
    exprPHI->addIncoming(
        // The null pointer will be replaced in finalizePHINodes.
        ConstantPointerNull::get(cast<PointerType>(IRB.getInt8PtrTy())),
        I.getIncomingBlock(incoming));
  }

  symbolicExpressions[&I] = exprPHI;
}

void Symbolizer::visitInsertValueInst(InsertValueInst &I) {
  IRBuilder<> IRB(&I);
  auto insert = buildRuntimeCall(
      IRB, runtime.buildInsert,
      {{I.getAggregateOperand(), true},
       {I.getInsertedValueOperand(), true},
       {IRB.getInt64(aggregateMemberOffset(I.getAggregateOperand()->getType(),
                                           I.getIndices())),
        false},
       {IRB.getInt8(isLittleEndian(I.getInsertedValueOperand()->getType()) ? 1 : 0), false}});
  registerSymbolicComputation(insert, &I);
}

void Symbolizer::visitExtractValueInst(ExtractValueInst &I) {
  IRBuilder<> IRB(&I);
  auto extract = buildRuntimeCall(
      IRB, runtime.buildExtract,
      {{I.getAggregateOperand(), true},
       {IRB.getInt64(aggregateMemberOffset(I.getAggregateOperand()->getType(),
                                           I.getIndices())),
        false},
       {IRB.getInt64(dataLayout.getTypeStoreSize(I.getType())), false},
       {IRB.getInt8(isLittleEndian(I.getType()) ? 1 : 0), false}});
  registerSymbolicComputation(extract, &I);
}

void Symbolizer::visitSwitchInst(SwitchInst &I) {
  // Switch compares a value against a set of integer constants; duplicate
  // constants are not allowed
  // (https://llvm.org/docs/LangRef.html#switch-instruction).

  IRBuilder<> IRB(&I);
  auto *condition = I.getCondition();
  auto *conditionExpr = getSymbolicExpression(condition);
  if (conditionExpr == nullptr)
    return;

  // Build a check whether we have a symbolic condition, to be used later.
  auto *haveSymbolicCondition = IRB.CreateICmpNE(
      conditionExpr, ConstantPointerNull::get(IRB.getInt8PtrTy()));
  auto *constraintBlock = SplitBlockAndInsertIfThen(haveSymbolicCondition, &I,
                                                    /* unreachable */ false);

  // In the constraint block, we push one path constraint per case.
  IRB.SetInsertPoint(constraintBlock);
  for (auto &caseHandle : I.cases()) {
    auto *caseTaken = IRB.CreateICmpEQ(condition, caseHandle.getCaseValue());
    auto *caseConstraint = IRB.CreateCall(
        runtime.comparisonHandlers[CmpInst::ICMP_EQ],
        {conditionExpr, createValueExpression(caseHandle.getCaseValue(), IRB)});
    IRB.CreateCall(runtime.pushPathConstraint,
                   {caseConstraint, caseTaken, getTargetPreferredInt(&I)});
  }
}

void Symbolizer::visitUnreachableInst(UnreachableInst & /*unused*/) {
  // Nothing to do here...
}

void Symbolizer::visitInstruction(Instruction &I) {
  // Some instructions are only used in the context of exception handling, which
  // we ignore for now.
  if (isa<LandingPadInst>(I) || isa<ResumeInst>(I))
    return;

  errs() << "Warning: unknown instruction " << I
         << "; the result will be concretized\n";
}

CallInst *Symbolizer::createValueExpression(Value *V, IRBuilder<> &IRB) {
  auto *valueType = V->getType();

  if (isa<ConstantPointerNull>(V)) {
    return IRB.CreateCall(runtime.buildNullPointer, {});
  }

  if (valueType->isIntegerTy()) {
    auto bits = valueType->getPrimitiveSizeInBits();
    if (bits == 1) {
      // Special case: LLVM uses the type i1 to represent Boolean values, but
      // for Z3 we have to create expressions of a separate sort.
      return IRB.CreateCall(runtime.buildBool, {V});
    } else if (bits <= 64) {
      return IRB.CreateCall(runtime.buildInteger,
                            {IRB.CreateZExtOrBitCast(V, IRB.getInt64Ty()),
                             IRB.getInt8(valueType->getPrimitiveSizeInBits())});
    } else {
      // Anything up to the maximum supported 128 bits. Those integers are a bit
      // tricky because the symbolic backends don't support them per se. We have
      // a special function in the run-time library that handles them, usually
      // by assembling expressions from smaller chunks.
      return IRB.CreateCall(
          runtime.buildInteger128,
          {IRB.CreateTrunc(IRB.CreateLShr(V, ConstantInt::get(valueType, 64)),
                           IRB.getInt64Ty()),
           IRB.CreateTrunc(V, IRB.getInt64Ty())});
    }
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

    auto *memory = IRB.CreateAlloca(V->getType());
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

Symbolizer::SymbolicComputation
Symbolizer::forceBuildRuntimeCall(IRBuilder<> &IRB, SymFnT function,
                                  ArrayRef<std::pair<Value *, bool>> args) {
  std::vector<Value *> functionArgs;
  for (const auto &[arg, symbolic] : args) {
    functionArgs.push_back(symbolic ? getSymbolicExpressionOrNull(arg) : arg);
  }
  auto *call = IRB.CreateCall(function, functionArgs);

  std::vector<Input> inputs;
  for (unsigned i = 0; i < args.size(); i++) {
    const auto &[arg, symbolic] = args[i];
    if (symbolic)
      inputs.push_back({arg, i, call});
  }

  return SymbolicComputation(call, call, inputs);
}

void Symbolizer::tryAlternative(IRBuilder<> &IRB, Value *V) {
  auto *destExpr = getSymbolicExpression(V);
  if (destExpr != nullptr) {
    auto *concreteDestExpr = createValueExpression(V, IRB);
    auto *destAssertion =
        IRB.CreateCall(runtime.comparisonHandlers[CmpInst::ICMP_EQ],
                       {destExpr, concreteDestExpr});
    auto *pushAssertion = IRB.CreateCall(
        runtime.pushPathConstraint,
        {destAssertion, IRB.getInt1(true), getTargetPreferredInt(V)});
    registerSymbolicComputation(SymbolicComputation(
        concreteDestExpr, pushAssertion, {{V, 0, destAssertion}}));
  }
}

uint64_t Symbolizer::aggregateMemberOffset(Type *aggregateType,
                                           ArrayRef<unsigned> indices) const {
  uint64_t offset = 0;
  auto *indexedType = aggregateType;
  for (auto index : indices) {
    // All indices in an extractvalue instruction are constant:
    // https://llvm.org/docs/LangRef.html#extractvalue-instruction

    if (auto *structType = dyn_cast<StructType>(indexedType)) {
      offset += dataLayout.getStructLayout(structType)->getElementOffset(index);
      indexedType = structType->getElementType(index);
    } else {
      auto *arrayType = cast<ArrayType>(indexedType);
      unsigned elementSize =
          dataLayout.getTypeAllocSize(arrayType->getArrayElementType());
      offset += elementSize * index;
      indexedType = arrayType->getArrayElementType();
    }
  }

  return offset;
}
