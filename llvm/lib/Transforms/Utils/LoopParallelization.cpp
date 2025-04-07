#include "llvm/Transforms/Utils/LoopParallelization.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/PassManager.h"

#include "llvm/IR/Dominators.h"
using namespace llvm;


PreservedAnalyses LoopParallelizationPass::run(Function &F,
  FunctionAnalysisManager &AM) {
    errs() << "Hello" << "\n";
    auto &LI = AM.getResult<LoopAnalysis>(F);
    auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
    // OS << "Loop info for function '" << F.getName() << "':\n";
    // LI.print(OS);
    
    for (Loop *L : LI) {
        // Sets to store variables
        std::set<std::string> insideLoopVars;
        std::set<std::string> outsideLoopVars;
        std::set<std::string> declaredInsideLoopVars;

        std::set<std::string> assignedBeforeLoopVars;
        std::set<std::string> assignedOutsideLoopVars;
        std::set<std::string> assignedInsideLoopVars;

        BasicBlock *Preheader = L->getLoopPreheader();
        BasicBlock *Header = L->getHeader();

        if (!Preheader) {
            errs() << "Loop does not have a preheader, skipping.\n";
            continue;
        }

        // Find all blocks that dominate the preheader
        for (BasicBlock &BB : F) {
            if (DT.dominates(&BB, Preheader)) { // Check if BB dominates the preheader
                for (Instruction &I : BB) {
                    // Look for store instructions (assignments)
                    if (auto *SI = dyn_cast<StoreInst>(&I)) {
                        if (Value *Ptr = SI->getPointerOperand()) {
                            if (Ptr->hasName()) {
                                assignedBeforeLoopVars.insert(Ptr->getName().str());
                            }
                        }
                    }
                }
            }
        }

        // Collect variables used inside the loop
        for (BasicBlock *BB : L->blocks()) {
            for (Instruction &I : *BB) {
                if (auto *SI = dyn_cast<StoreInst>(&I)) {
                    if (Value *Ptr = SI->getPointerOperand()) {
                        if (Ptr->hasName()) {
                            assignedInsideLoopVars.insert(Ptr->getName().str());
                        }
                    }
                }

                if (auto *AI = dyn_cast<AllocaInst>(&I)) {
                    if (AI->hasName()) {
                        declaredInsideLoopVars.insert(AI->getName().str());
                    }
                }
                for (Use &U : I.operands()) {
                    if (Value *V = U.get()) {
                        if (V->hasName()) {
                            insideLoopVars.insert(V->getName().str());
                        }
                    }
                }
            }
        }

        // Collect variables used outside the loop
        for (BasicBlock &BB : F) {
            if (!L->contains(&BB)) { // Only consider blocks outside the loop
                for (Instruction &I : BB) {

                    if (auto *SI = dyn_cast<StoreInst>(&I)) {
                        if (Value *Ptr = SI->getPointerOperand()) {
                            if (Ptr->hasName()) {
                                assignedOutsideLoopVars.insert(Ptr->getName().str());
                            }
                        }
                    }

                    for (Use &U : I.operands()) {
                        if (Value *V = U.get()) {
                            if (V->hasName()) {
                                outsideLoopVars.insert(V->getName().str());
                            }
                        }
                    }
                }
            }
        }

        // Compute the intersection of variables
        // std::set<std::string> intersection;
        // for (const auto &Var : insideLoopVars) {
        //     if (outsideLoopVars.find(Var) != outsideLoopVars.end()) {
        //         intersection.insert(Var);
        //     }
        // }

        std::set<std::string> intersection;
        for (const auto &Var : assignedBeforeLoopVars) {
            if (assignedInsideLoopVars.find(Var) != assignedInsideLoopVars.end()) {
                intersection.insert(Var);
            }
        }

        // Print the results
        errs() << "Loop:\n" << L->getHeader()->getName() << "\n";
        errs() << "  Variables inside the loop:\n";
        for (const auto &Var : insideLoopVars) {
            errs() << "    " << Var << "\n";
        }

        errs() << "  Variables outside the loop:\n";
        for (const auto &Var : outsideLoopVars) {
            errs() << "    " << Var << "\n";
        }

        errs() << "  Variables assigned inside the loop:\n";
        for (const auto &Var : assignedInsideLoopVars) {
            errs() << "    " << Var << "\n";
        }

        errs() << "  Variables assigned before the loop:\n";
        for (const auto &Var : assignedBeforeLoopVars) {
            errs() << "    " << Var << "\n";
        }

        errs() << "  Intersection of variables:\n";
        for (const auto &Var : intersection) {
            errs() << "    " << Var << "\n";
        }
    }
    return PreservedAnalyses::all();
}
  // errs() << "Hello"<<"\n";
  // static int Counter = 1;
  // unsigned int InstructionCount = 0;
  //   errs() << "Function: " << F.getName() << "\n";
  //   errs() << F.getEntryBlock().getName() << "\n";
  //   // errs() <<  << "\n";
  //   for (auto &Sym : *F.getValueSymbolTable()) {
  //     errs() << "Symbol: " << Sym.getKey() << " : " << Sym.getValue() << "   " << Sym.getKeyData() << "\n";
  //   }

  // for( auto &BB : F){
    
  //   errs() << "Basic Block: " << BB.getName()<< " Parent: "<< BB.getParent()->getName() << "\n";
  //   // Iterate through the instructions in the basic block
  //   for (Instruction &I : BB) {
  //     errs() << "  Instruction: " << I << "\n";
  //   }
  //   InstructionCount+=BB.size();

  // }
  // errs() << "Function #" << Counter++ << ": " << F.getName()
  //   << " has " << InstructionCount << " instructions"<<"\n";
  // return PreservedAnalyses::all();
  



  // LoopInfo &LI = AM.getResult<LoopAnalysis>(F);

  // // Iterate through the loops in the function
  // for (Loop *L : LI) {
  //   errs() << "LoopParallelization: Found a loop in function " << F.getName() << "\n";

  //   // Get the loop's basic blocks
  //   //SmallVector<BasicBlock *, 8> LoopBasicBlocks;
  //   ArrayRef<BasicBlock *> LoopBasicBlocks = L->getBlocks();

  //   errs() << "LoopParallelization: Loop contains " << LoopBasicBlocks.size()
  //           << " basic blocks\n";

  //   // Iterate through the basic blocks in the loop
  //   for (BasicBlock *BB : LoopBasicBlocks) {
  //     errs() << "LoopParallelization:   Basic Block: " << BB->getName() << "\n";

  //     // Iterate through the instructions in the basic block
  //     for (Instruction &I : *BB) {
  //       errs() << "LoopParallelization:     Instruction: " << I << "\n";
  //     }
  //   }
  // }

  // return PreservedAnalyses::all();


// PassPluginLibraryInfo LoopParallelizationPass::getPluginInfo() {
//   return {LLVM_PLUGIN_API_VERSION, "LoopParallelizationPass", LLVM_VERSION_STRING,
//           [](PassBuilder &PB) {
//             PB.registerPipelineParsingCallback(
//                 [](StringRef Name, FunctionPassManager &FPM,
//                    ArrayRef<PassBuilder::PipelineElement>) {
//                   if (Name == "loop-parallelization") {
//                     FPM.addPass(LoopParallelizationPass());
//                     return true;
//                   }
//                   return false;
//                 });
//           }};
// }

// extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
// llvmGetPassPluginInfo() {
//   return LoopParallelizationPass::getPluginInfo();
// }