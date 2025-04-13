#ifndef LLVM_TRANSFORMS_HELLONEW_LOOPPARALLELIZATION_H
#define LLVM_TRANSFORMS_HELLONEW_LOOPPARALLELIZATION_H

#include "llvm/IR/PassManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include <map>
#include <vector>
namespace llvm {

class LoopParallelizationPass : public PassInfoMixin<LoopParallelizationPass> {
private:
    bool parallelizable = true;
    void checkSharedVariables(std::set<std::string>* assignedBeforeLoopVars,
        std::set<std::string>* assignedInsideLoopVars,
        std::set<std::string>* loadedInsideLoopVars,
        std::map<std::string, std::vector<DILocation*>>* varUseLocations);
    void checkArrayIndexes(std::set<std::pair<Value*, std::string>>* storedInsideArrayAddressPtrs,
        std::set<std::pair<Value*, std::string>>* loadedInsideArrayAddressPtrs);
    void handleVariables(Instruction &I,
        std::string iterationVariable,
        std::set<std::string>* assignedBeforeLoopVars,
        std::set<std::string>* assignedInsideLoopVars,
        std::map<std::string, std::vector<DILocation*>>* varUseLocations,
        std::set<std::string>* loadedInsideLoopVars,
        std::set<std::string>* arrayAddressPtrs);
    void handleArrays(Instruction &I,
        Value* iterationVariable,
        std::set<std::string>* assignedInsideLoopArrays,
        std::set<std::string>* loadedInsideLoopArrays,
        std::set<std::string>* arrayAddressPtrs,
        std::set<std::pair<Value*, std::string>>* storedInsideArrayAddressPtrs,
        std::set<std::pair<Value*, std::string>>* loadedInsideArrayAddressPtrs);
    void handleFunctions(Instruction &I);
public:
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

}

#endif