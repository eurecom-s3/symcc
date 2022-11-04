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

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

#if LLVM_VERSION_MAJOR >= 13
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Passes/PassPlugin.h>
#endif

#include "Pass.h"

using namespace llvm;

//
// Legacy pass registration (up to LLVM 13)
//

void addSymbolizeLegacyPass(const PassManagerBuilder & /* unused */,
                            legacy::PassManagerBase &PM) {
  PM.add(new SymbolizeLegacyPass());
}

// Make the pass known to opt.
static RegisterPass<SymbolizeLegacyPass> X("symbolize", "Symbolization Pass");
// Tell frontends to run the pass automatically.
static struct RegisterStandardPasses Y(PassManagerBuilder::EP_VectorizerStart,
                                       addSymbolizeLegacyPass);
static struct RegisterStandardPasses
    Z(PassManagerBuilder::EP_EnabledOnOptLevel0, addSymbolizeLegacyPass);

//
// New pass registration (LLVM 13 and above)
//

#if LLVM_VERSION_MAJOR >= 13

PassPluginLibraryInfo getSymbolizePluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "Symbolization Pass", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            // We need to act on the entire module as well as on each function.
            // Those actions are independent from each other, so we register a
            // module pass at the start of the pipeline and a function pass just
            // before the vectorizer. (There doesn't seem to be a way to run
            // module passes at the start of the vectorizer, hence the split.)
            PB.registerPipelineStartEPCallback(
                [](ModulePassManager &PM, PassBuilder::OptimizationLevel) {
                  PM.addPass(SymbolizePass());
                });
            PB.registerVectorizerStartEPCallback(
                [](FunctionPassManager &PM, PassBuilder::OptimizationLevel) {
                  PM.addPass(SymbolizePass());
                });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return getSymbolizePluginInfo();
}

#endif
