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

#ifndef SYMBOLIZE_H
#define SYMBOLIZE_H

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InstVisitor.h>
#include <llvm/IR/ValueMap.h>
#include <llvm/Support/raw_ostream.h>

#include "Runtime.h"

class Symbolizer : public llvm::InstVisitor<Symbolizer> {
public:
  explicit Symbolizer(llvm::Module &M)
      : runtime(M), dataLayout(M.getDataLayout()),
        ptrBits(M.getDataLayout().getPointerSizeInBits()),
        intPtrType(M.getDataLayout().getIntPtrType(M.getContext())) {}

  /// Insert code to obtain the symbolic expressions for the function arguments.
  void symbolizeFunctionArguments(llvm::Function &F);

  /// Insert a call to the run-time library to notify it of the basic block
  /// entry.
  void insertBasicBlockNotification(llvm::BasicBlock &B);

  /// Finish the processing of PHI nodes.
  ///
  /// This assumes that there is a dummy PHI node for each such instruction in
  /// the function, and that we have recorded all PHI nodes in the member
  /// phiNodes. In other words, the function has to be called after all
  /// instructions have been processed in order to fix up PHI nodes. See the
  /// documentation of member phiNodes for why we process PHI nodes in two
  /// steps.
  ///
  /// Important! Calling this function invalidates symbolicExpressions.
  void finalizePHINodes();

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
  void shortCircuitExpressionUses();

  void handleIntrinsicCall(llvm::CallBase &I);
  void handleInlineAssembly(llvm::CallInst &I);
  void handleFunctionCall(llvm::CallBase &I, llvm::Instruction *returnPoint);

  //
  // Implementation of InstVisitor
  //
  void visitBinaryOperator(llvm::BinaryOperator &I);
  void visitSelectInst(llvm::SelectInst &I);
  void visitCmpInst(llvm::CmpInst &I);
  void visitReturnInst(llvm::ReturnInst &I);
  void visitBranchInst(llvm::BranchInst &I);
  void visitIndirectBrInst(llvm::IndirectBrInst &I);
  void visitCallInst(llvm::CallInst &I);
  void visitInvokeInst(llvm::InvokeInst &I);
  void visitAllocaInst(llvm::AllocaInst &);
  void visitLoadInst(llvm::LoadInst &I);
  void visitStoreInst(llvm::StoreInst &I);
  void visitGetElementPtrInst(llvm::GetElementPtrInst &I);
  void visitBitCastInst(llvm::BitCastInst &I);
  void visitTruncInst(llvm::TruncInst &I);
  void visitIntToPtrInst(llvm::IntToPtrInst &I);
  void visitPtrToIntInst(llvm::PtrToIntInst &I);
  void visitSIToFPInst(llvm::SIToFPInst &I);
  void visitUIToFPInst(llvm::UIToFPInst &I);
  void visitFPExtInst(llvm::FPExtInst &I);
  void visitFPTruncInst(llvm::FPTruncInst &I);
  void visitFPToSI(llvm::FPToSIInst &I);
  void visitFPToUI(llvm::FPToUIInst &I);
  void visitCastInst(llvm::CastInst &I);
  void visitPHINode(llvm::PHINode &I);
  void visitInsertValueInst(llvm::InsertValueInst &I);
  void visitExtractValueInst(llvm::ExtractValueInst &I);
  void visitSwitchInst(llvm::SwitchInst &I);
  void visitUnreachableInst(llvm::UnreachableInst &);
  void visitInstruction(llvm::Instruction &I);

private:
  static constexpr unsigned kExpectedMaxPHINodesPerFunction = 16;
  static constexpr unsigned kExpectedSymbolicArgumentsPerComputation = 2;

  /// A symbolic input.
  struct Input {
    llvm::Value *concreteValue;
    unsigned operandIndex;
    llvm::Instruction *user;

    llvm::Value *getSymbolicOperand() const {
      return user->getOperand(operandIndex);
    }

    void replaceOperand(llvm::Value *newOperand) {
      user->setOperand(operandIndex, newOperand);
    }
  };

  /// A symbolic computation with its inputs.
  struct SymbolicComputation {
    llvm::Instruction *firstInstruction = nullptr, *lastInstruction = nullptr;
    llvm::SmallVector<Input, kExpectedSymbolicArgumentsPerComputation> inputs;

    SymbolicComputation() = default;

    SymbolicComputation(llvm::Instruction *first, llvm::Instruction *last,
                        llvm::ArrayRef<Input> in)
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

    friend llvm::raw_ostream &
    operator<<(llvm::raw_ostream &out,
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
  llvm::CallInst *createValueExpression(llvm::Value *V, llvm::IRBuilder<> &IRB);

  /// Get the (already created) symbolic expression for a value.
  llvm::Value *getSymbolicExpression(llvm::Value *V) {
    auto exprIt = symbolicExpressions.find(V);
    return (exprIt != symbolicExpressions.end()) ? exprIt->second : nullptr;
  }

  llvm::Value *getSymbolicExpressionOrNull(llvm::Value *V) {
    auto *expr = getSymbolicExpression(V);
    if (expr == nullptr)
      return llvm::ConstantPointerNull::get(
          llvm::IntegerType::getInt8PtrTy(V->getContext()));
    return expr;
  }

  bool isLittleEndian(llvm::Type *type) {
    return (!type->isAggregateType() && dataLayout.isLittleEndian());
  }

  /// Like buildRuntimeCall, but the call is always generated.
  SymbolicComputation
  forceBuildRuntimeCall(llvm::IRBuilder<> &IRB, SymFnT function,
                        llvm::ArrayRef<std::pair<llvm::Value *, bool>> args);

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
  buildRuntimeCall(llvm::IRBuilder<> &IRB, SymFnT function,
                   llvm::ArrayRef<std::pair<llvm::Value *, bool>> args) {
    if (std::all_of(args.begin(), args.end(),
                    [this](std::pair<llvm::Value *, bool> arg) {
                      return (getSymbolicExpression(arg.first) == nullptr);
                    })) {
      return {};
    }

    return forceBuildRuntimeCall(IRB, function, args);
  }

  /// Convenience overload that treats all arguments as symbolic.
  std::optional<SymbolicComputation>
  buildRuntimeCall(llvm::IRBuilder<> &IRB, SymFnT function,
                   llvm::ArrayRef<llvm::Value *> symbolicArgs) {
    std::vector<std::pair<llvm::Value *, bool>> args;
    for (const auto &arg : symbolicArgs) {
      args.emplace_back(arg, true);
    }

    return buildRuntimeCall(IRB, function, args);
  }

  /// Register the result of the computation as the symbolic expression
  /// corresponding to the concrete value and record the computation for
  /// short-circuiting.
  void registerSymbolicComputation(const SymbolicComputation &computation,
                                   llvm::Value *concrete = nullptr) {
    if (concrete != nullptr)
      symbolicExpressions[concrete] = computation.lastInstruction;
    expressionUses.push_back(computation);
  }

  /// Convenience overload for chaining with buildRuntimeCall.
  void registerSymbolicComputation(
      const std::optional<SymbolicComputation> &computation,
      llvm::Value *concrete = nullptr) {
    if (computation)
      registerSymbolicComputation(*computation, concrete);
  }

  /// Generate code that makes the solver try an alternative value for V.
  void tryAlternative(llvm::IRBuilder<> &IRB, llvm::Value *V);

  /// Helper to use a pointer to a host object as integer (truncating!).
  ///
  /// Note that the conversion will truncate the most significant bits of the
  /// pointer if the host uses larger addresses than the target. Therefore, use
  /// this function only when such loss is acceptable (e.g., when generating
  /// site identifiers to be passed to the backend, where collisions of the
  /// least significant bits are reasonably unlikely).
  ///
  /// Why not do a lossless conversion and make the backend accept 64-bit
  /// integers?
  ///
  /// 1. Performance: 32-bit architectures will process 32-bit values faster
  /// than 64-bit values.
  ///
  /// 2. Pragmatism: Changing the backend to accept and process 64-bit values
  /// would require modifying code that we don't control (in the case of Qsym).
  llvm::ConstantInt *getTargetPreferredInt(void *pointer) {
    return llvm::ConstantInt::get(intPtrType,
                                  reinterpret_cast<uint64_t>(pointer));
  }

  /// Compute the offset of a member in a (possibly nested) aggregate.
  uint64_t aggregateMemberOffset(llvm::Type *aggregateType,
                                 llvm::ArrayRef<unsigned> indices) const;

  const Runtime runtime;

  /// The data layout of the currently processed module.
  const llvm::DataLayout &dataLayout;

  /// The width in bits of pointers in the module.
  unsigned ptrBits;

  /// An integer type at least as wide as a pointer.
  llvm::IntegerType *intPtrType;

  /// Mapping from SSA values to symbolic expressions.
  ///
  /// For pointer values, the stored value is an expression describing the value
  /// of the pointer itself (i.e., the address, not the referenced value). For
  /// structure values, the expression is a single large bit vector.
  ///
  /// TODO This member adds a lot of complexity: various methods rely on it, and
  /// finalizePHINodes invalidates it. We may want to pass the map around
  /// explicitly.
  llvm::ValueMap<llvm::Value *, llvm::Value *> symbolicExpressions;

  /// A record of all PHI nodes in this function.
  ///
  /// PHI nodes may refer to themselves, in which case we run into an infinite
  /// loop when trying to generate symbolic expressions recursively. Therefore,
  /// we only insert a dummy symbolic expression for each PHI node and fix it
  /// after all instructions have been processed.
  llvm::SmallVector<llvm::PHINode *, kExpectedMaxPHINodesPerFunction> phiNodes;

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

#endif
