#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

#include "Pass.h"

void addSymbolizePass(const llvm::PassManagerBuilder & /* unused */,
                      llvm::legacy::PassManagerBase &PM) {
  PM.add(new SymbolizePass());
}

// Make the pass known to opt.
static llvm::RegisterPass<SymbolizePass> X("symbolize", "Symbolization Pass");
// Tell frontends to run the pass automatically.
static struct llvm::RegisterStandardPasses
    Y(llvm::PassManagerBuilder::EP_VectorizerStart, addSymbolizePass);
static struct llvm::RegisterStandardPasses
    Z(llvm::PassManagerBuilder::EP_EnabledOnOptLevel0, addSymbolizePass);
