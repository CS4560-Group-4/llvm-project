#ifndef LLVM_TRANSFORMS_HELLONEW_LOOPPARALLELIZATION_H
#define LLVM_TRANSFORMS_HELLONEW_LOOPPARALLELIZATION_H

#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

class LoopParallelizationPass : public PassInfoMixin<LoopParallelizationPass> {
  // raw_ostream &OS;
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  // static PassPluginLibraryInfo getPluginInfo();
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_HELLONEW_LOOPPARALLELIZATION_H