#include "llvm/Transforms/Utils/LoopParallelization.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/PassManager.h"

#include "llvm/IR/Dominators.h"


#include "llvm/IR/DebugInfoMetadata.h"

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
        std::set<std::string> declaredInsideLoopVars;
        std::set<std::string> assignedBeforeLoopVars;
        std::set<std::string> assignedOutsideLoopVars;

        std::set<std::string> assignedInsideLoopVars;
        std::set<std::string> assignedInsideLoopVectors;
        std::set<std::string> assignedInsideLoopArrays;
        std::set<std::string> assignedInsideLoopBasicTypes;

        BasicBlock *Preheader = L->getLoopPreheader();
        BasicBlock *Header = L->getHeader();

        std::string iterationVariable;

        if (!Preheader) {
            errs() << "Loop does not have a preheader, skipping.\n";
            continue;
        }

        // Get the first instruction in the conditional block
        if (!Header->empty()) {
            Instruction &FirstInst = Header->front();
            if (auto *SI = dyn_cast<LoadInst>(&FirstInst)) {
                if (Value *Ptr = SI->getPointerOperand()) {
                    if (Ptr->hasName()) {
                        errs() << "Iteration variable: " << Ptr->getName() << "\n";
                        iterationVariable = Ptr->getName().str();
                    }
                }
            }
            errs() << "First instruction in the conditional block: ";
            FirstInst.print(errs());
            errs() << "\n";
        } else {
            errs() << "Conditional block is empty.\n";
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
                if (auto *AI = dyn_cast<AllocaInst>(&I)) {
                    if (AI->hasName()) {
                        declaredInsideLoopVars.insert(AI->getName().str());
                    }
                }

                // Instruction that assign array location to a variable
                if (auto *GEPI = dyn_cast<GetElementPtrInst>(&I)) {
                    if (GEPI->hasName()) {
                        declaredInsideLoopVars.insert(GEPI->getName().str());
                    }
                }

                if (auto *SI = dyn_cast<StoreInst>(&I)) {
                    if (Value *Ptr = SI->getPointerOperand()) {

                        if (!Ptr->hasName()) {
                            continue;    
                        }
                        std::string varName = Ptr->getName().str();

                        if (varName == iterationVariable) {
                            continue; // Skip the iteration variable
                        }

                        assignedInsideLoopVars.insert(varName);
                        
                        Type *Ty = Ptr->getType();

                        // Check if it's an array
                        if (Ty->isArrayTy()) {
                            assignedInsideLoopArrays.insert(varName);
                        }
                        // // Check if it's a vector
                        // else if (Ty->isVectorTy()) {
                        //     assignedInsideLoopVectors.insert(varName);
                        // }
                        // Check if it's a basic type (int, float, etc.)
                        else if (Ty->isSingleValueType()) {
                            assignedInsideLoopBasicTypes.insert(varName);
                        }
                        // TODO: include pointer conditions
                    }
                }
            }
        }


        std::set<std::string> intersection;
        for (const auto &Var : assignedBeforeLoopVars) {
            if (assignedInsideLoopVars.find(Var) != assignedInsideLoopVars.end()) {
                intersection.insert(Var);
            }
        }

        
        
        std::set<std::string> dangerousVars;
        for (const auto &Var : assignedInsideLoopBasicTypes) {
            if (declaredInsideLoopVars.find(Var) == declaredInsideLoopVars.end()) {
                dangerousVars.insert(Var);
            }
        }

        // Print the results
        errs() << "Loop:\n" << L->getHeader()->getName() << "\n";

        errs() << "  Variables assigned inside the loop:\n";
        for (const auto &Var : assignedInsideLoopVars) {
            errs() << "    " << Var << "\n";
        }

        errs() << "  Variables assigned inside the loop (arrays):\n";
        for (const auto &Var : assignedInsideLoopArrays) {
            errs() << "    " << Var << "\n";
        }

        errs() << "  Variables assigned inside the loop (vectors):\n";
        for (const auto &Var : assignedInsideLoopVectors) {
            errs() << "    " << Var << "\n";
        }

        errs() << "  Variables assigned inside the loop (basic types):\n";
        for (const auto &Var : assignedInsideLoopBasicTypes) {
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

        errs() << "  Variables declared inside the loop:\n";
        for (const auto &Var : declaredInsideLoopVars) {
            errs() << "    " << Var << "\n";
        }

        errs() << " Dangerous variables:\n";
        for (const auto &Var : dangerousVars) {
            errs() << "    " << Var << "\n";
        }

        for (Instruction &I : *Header) {
            if (DILocation *Loc = I.getDebugLoc()) { // Check if debug info exists
                unsigned Line = Loc->getLine();      // Get the line number
                StringRef File = Loc->getFilename(); // Get the source file name
                StringRef Directory = Loc->getDirectory(); // Get the directory
    
                errs() << "Loop declared at " << Directory << "/" << File
                       << ":" << Line << "\n";
                break; // Found the debug info, no need to check further
            }
        }

        

        if(dangerousVars.size() > 0) {
            errs() << "Loop CANNOT be parallelized" << "\n";
            // continue;
        } else {
            errs() << "Loop CAN be parallelized" << "\n";
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