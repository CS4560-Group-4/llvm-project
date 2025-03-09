#ifndef LLVM_TRANSFORMS_HELLONEW_LOOPPARALLELIZATION_H
#define LLVM_TRANSFORMS_HELLONEW_LOOPPARALLELIZATION_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class LoopParallelizationPass : public PassInfoMixin<LoopParallelizationPass> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_HELLONEW_LOOPPARALLELIZATION_H