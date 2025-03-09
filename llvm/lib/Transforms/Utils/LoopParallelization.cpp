#include "llvm/Transforms/Utils/LoopParallelization.h"
#include "llvm/IR/Function.h"

using namespace llvm;

PreservedAnalyses LoopParallelizationPass::run(Function &F,
                                      FunctionAnalysisManager &AM) {
  errs() << F.getName() << "\n";
  return PreservedAnalyses::all();
}