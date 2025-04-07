#ifndef LLVM_TRANSFORMS_HELLONEW_LOOPPARALLELIZATION2_H
#define LLVM_TRANSFORMS_HELLONEW_LOOPPARALLELIZATION2_H

#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

namespace llvm {

class LoopParallelization2Pass : public PassInfoMixin<LoopParallelization2Pass> {
public:
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
    LoopStandardAnalysisResults &AR,
    LPMUpdater &U);
  // static PassPluginLibraryInfo getPluginInfo();
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_HELLONEW_LOOPPARALLELIZATION2_H