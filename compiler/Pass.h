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

#ifndef PASS_H
#define PASS_H

#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/ValueMap.h>
#include <llvm/Pass.h>

#if LLVM_VERSION_MAJOR >= 13
#include <llvm/IR/PassManager.h>
#endif

class SymbolizeLegacyPass : public llvm::FunctionPass {
public:
  static char ID;

  SymbolizeLegacyPass() : FunctionPass(ID) {}

  virtual bool doInitialization(llvm::Module &M) override;
  virtual bool runOnFunction(llvm::Function &F) override;
};

#if LLVM_VERSION_MAJOR >= 13

class SymbolizePass : public llvm::PassInfoMixin<SymbolizePass> {
public:
  llvm::PreservedAnalyses run(llvm::Function &F,
                              llvm::FunctionAnalysisManager &);
  llvm::PreservedAnalyses run(llvm::Module &M, llvm::ModuleAnalysisManager &);

  static bool isRequired() { return true; }
};

#endif

#endif
