#include "llvm/Transforms/Utils/LoopParallelization2.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

PreservedAnalyses LoopParallelization2Pass::run(Loop &L, LoopAnalysisManager &AM,
  LoopStandardAnalysisResults &AR,
  LPMUpdater &U) {
  errs() << "Hello"<<"\n";
  std::set<std::string> insideLoopVars;
  std::set<std::string> outsideLoopVars;

  // Get the loop's preheader and body
  BasicBlock *preheader = L.getLoopPreheader();
  if (!preheader) {
    errs() << "Loop does not have a preheader, skipping.\n";
    return PreservedAnalyses::all();
  }

  // Collect variables used outside the loop (in the preheader)
  for (Instruction &I : *preheader) {
    for (Use &U : I.operands()) {
      if (Value *V = U.get()) {
        if (V->hasName()) {
          outsideLoopVars.insert(V->getName().str());
        }
      }
    }
  }

  // Collect variables used inside the loop
  for (BasicBlock *BB : L.blocks()) {
    for (Instruction &I : *BB) {
      for (Use &U : I.operands()) {
        if (Value *V = U.get()) {
          if (V->hasName() && outsideLoopVars.find(V->getName().str()) == outsideLoopVars.end()) {
            outsideLoopVars.insert(V->getName().str());
          }
        }
      }
    }
  }

  // Print the results
  errs() << "Inside Loop Variables:\n";
  for (const auto &Var : insideLoopVars) {
    errs() << "  " << Var << "\n";
  }

  errs() << "Outside Loop Variables:\n";
  for (const auto &Var : outsideLoopVars) {
    errs() << "  " << Var << "\n";
  }
  return PreservedAnalyses::all();
  
}
