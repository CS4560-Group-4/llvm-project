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


std::string getIterationVariable(BasicBlock *Header) {
    // Get the iteration variable of the loop
    if (!Header->empty()) {
        Instruction &FirstInst = Header->front();
        if (auto *SI = dyn_cast<LoadInst>(&FirstInst)) {
            if (Value *Ptr = SI->getPointerOperand()) {
                if (Ptr->hasName()) {
                    return Ptr->getName().str();
                }
            }
        }
    } else {
        return "";
        errs() << "Conditional block is empty.\n";
    }
}

std::string getLocationString(DILocation *Loc) {
    if (Loc) {
        unsigned Line = Loc->getLine();
        StringRef File = Loc->getFilename();
        StringRef Directory = Loc->getDirectory();
        return (Directory + "/" + File + ":" + std::to_string(Line)).str();
    }
    return "No location info";
}

std::string getLoopLocationString(BasicBlock *Header) {
    for (Instruction &I : *Header) {
        if (DILocation *Loc = I.getDebugLoc()) { // Check if debug info exists
            return getLocationString(Loc);
        }
    }
}

void handleVariables(Instruction &I,
                    const std::string iterationVariable,
                    std::set<std::string>* initializedInsideLoopVars,
                    std::set<std::string>* loadedInsideLoopVars,
                    std::set<std::string>* arrayAddressPtrs) {
    // Check if the instruction is a load instruction
    if (auto *LI = dyn_cast<LoadInst>(&I)) {
        // Check if it loads from a variable
        if (Value *Ptr = LI->getPointerOperand()) {
            if (!Ptr->hasName()) {
                return;
            }
            std::string varName = Ptr->getName().str();

            // Skip the iteration variable
            if (varName == iterationVariable) {
                return;
            }

            loadedInsideLoopVars->insert(varName);
        }
    } else if (auto *SI = dyn_cast<StoreInst>(&I)) {
        // Check if it assigns to a variable
        if (Value *Ptr = SI->getPointerOperand()) {
            if (!Ptr->hasName()) {
                return;    
            }
        
            std::string varName = Ptr->getName().str();


            // Skip the iteration variable
            if (varName == iterationVariable) {
                return;
            } else if (arrayAddressPtrs->find(varName) != arrayAddressPtrs->end()) {
                return;
            }

            if(loadedInsideLoopVars->find(varName) == loadedInsideLoopVars->end()) {
                initializedInsideLoopVars->insert(varName);
            } else {
                if (initializedInsideLoopVars->find(varName) == initializedInsideLoopVars->end()) {
                    errs() << "DANGER: Variable \'" << varName << "\' was stored after being loaded.\n";
                    errs() << "Location: " << getLocationString(SI->getDebugLoc()) << "\n\n";
                }
            }

            // Type *Ty = Ptr->getType();
            // // Check if it's a vector
            // else if (Ty->isVectorTy()) {
            //     assignedInsideLoopVectors.insert(varName);
            // }

            // Check if it's a basic type (int, float, etc.)
            // if (Ty->isSingleValueType()) {
            //     assignedInsideLoopBasicTypes.insert(varName);
            // }
        }
    }
}

void handleArrays(Instruction &I,
                    const std::string iterationVariable,
                    std::set<std::string>* assignedInsideLoopArrays,
                    std::set<std::string>* arrayAddressPtrs,
                    std::set<std::string>* assignedInsideArrayAddressPtrs,
                    std::set<std::string>* loadedInsideArrayAddressPtrs) {
    // Instruction that assign array location to a variable
    if (auto *GEPI = dyn_cast<GetElementPtrInst>(&I)) {
        if (GEPI->hasName()) {
            arrayAddressPtrs->insert(GEPI->getName().str());
        }
        if (Value *Ptr = GEPI->getPointerOperand()) {
            if (Ptr->hasName()) {
                assignedInsideLoopArrays->insert(Ptr->getName().str());
            }
        }

        if (Value *index = GEPI->getOperand(1)) {
            if (index->hasName()) {
                // assignedInsideLoopArrays->insert(index->getName().str());
                errs() << "Array index: " << index->getName() << "\n";
                // errs() << index->getContext().diagnose() << "\n";
                // errs() << index->getType() << "\n";
                // errs() << index << "\n";
            }
        }

        if (Value *index = GEPI->getOperand(2)) {
            if (index->hasName()) {
                // assignedInsideLoopArrays->insert(index->getName().str());
                errs() << "Array index: " << index->getName() << "\n";
                // errs() << index->getContext().diagnose() << "\n";
                // errs() << index->getType(). << "\n";
                // errs() << index << "\n";
            }
        }
    }

    // Check if the instruction is a store instruction
    if (auto *SI = dyn_cast<StoreInst>(&I)) {
        // Check if it assigns to a variable
        if (Value *Ptr = SI->getPointerOperand()) {

            if (!Ptr->hasName()) {
                return;   
            }
            
            // if (Value *GEPI = dyn_cast<GetElementPtrInst>(Ptr)) {
            //     if (GEPI->hasName()) {
            //         errs() << "Array address pointer: " << GEPI->getName() << "\n";
            //     }
            // }

            std::string varName = Ptr->getName().str();

            // Check if the variable is an array address pointer
            if (arrayAddressPtrs->find(varName) != arrayAddressPtrs->end()) {
                assignedInsideArrayAddressPtrs->insert(varName);
            }
        }
    }

    // Check if the instruction is a load instruction
    if (auto *LI = dyn_cast<LoadInst>(&I)) {
        // Check if it loads from a variable
        if (Value *Ptr = LI->getPointerOperand()) {
            if (!Ptr->hasName()) {
                return;
            }
            std::string varName = Ptr->getName().str();

            // Skip the iteration variable
            if (varName == iterationVariable) {
                return;
            }

            if (arrayAddressPtrs->find(varName) != arrayAddressPtrs->end()) {
                loadedInsideArrayAddressPtrs->insert(varName);
            }
        }
    }
}

PreservedAnalyses LoopParallelizationPass::run(Function &F,
  FunctionAnalysisManager &AM) {
    errs() << "Hello" << "\n";
    auto &LI = AM.getResult<LoopAnalysis>(F);
    auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
    // OS << "Loop info for function '" << F.getName() << "':\n";
    // LI.print(OS);
    
    for (Loop *L : LI) {
        std::string iterationVariable;

        std::set<std::string> declaredInsideLoopVars;
        std::set<std::string> assignedBeforeLoopVars;
        std::set<std::string> assignedOutsideLoopVars;

        std::set<std::string> initializedInsideLoopVars;
        std::set<std::string> assignedInsideLoopVars;
        std::set<std::string> loadedInsideLoopVars;

        std::set<std::string> assignedInsideLoopVectors;
        std::set<std::string> assignedInsideLoopArrays;
        std::set<std::string> assignedInsideLoopBasicTypes;

        std::set<std::string> arrayAddressPtrs;
        std::set<std::string> assignedInsideArrayAddressPtrs;
        std::set<std::string> loadedInsideArrayAddressPtrs;
        // TODO check if loop address points to something other than i

        BasicBlock *Preheader = L->getLoopPreheader();
        BasicBlock *Header = L->getHeader();

        if (!Preheader) {
            errs() << "Loop does not have a preheader, skipping.\n";
            continue;
        }
        errs() << "\n\n================================================================\n";
        errs() << "Loop found in function: " << F.getName() << "\n";
        errs() << "Location: " << getLoopLocationString(Header) << "\n\n";

        iterationVariable = getIterationVariable(Header);
        errs() << "Iteration variable: " << iterationVariable << "\n\n";

        // Collect variables used inside the loop
        for (BasicBlock *BB : L->blocks()) {
            for (Instruction &I : *BB) {
                handleArrays(I, iterationVariable, &assignedInsideLoopArrays, &arrayAddressPtrs, &assignedInsideArrayAddressPtrs, &loadedInsideArrayAddressPtrs);
                handleVariables(I, iterationVariable, &initializedInsideLoopVars, &loadedInsideLoopVars, &arrayAddressPtrs);  
            }
        }
        
        
        std::set<std::string> dangerousVars;
        for (const auto &Var : assignedInsideLoopBasicTypes) {
            if (declaredInsideLoopVars.find(Var) == declaredInsideLoopVars.end()) {
                dangerousVars.insert(Var);
            }
        }

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

        errs() << "  Variables initialized inside the loop:\n";
        for (const auto &Var : initializedInsideLoopVars) {
            errs() << "    " << Var << "\n";
        }

        errs() << "  Variables loaded inside the loop:\n";
        for (const auto &Var : loadedInsideLoopVars) {
            errs() << "    " << Var << "\n";
        }

        errs() << "  Variables assigned inside the loop (array address pointers):\n";
        for (const auto &Var : assignedInsideArrayAddressPtrs) {
            errs() << "    " << Var << "\n";
        }
        errs() << "  Variables loaded inside the loop (array address pointers):\n";
        for (const auto &Var : loadedInsideArrayAddressPtrs) {
            errs() << "    " << Var << "\n";
        }
        

        if(dangerousVars.size() > 0) {
            errs() << "Loop CANNOT be parallelized" << "\n";
            errs() << "Dangerous variables: " << "\n";
            for (const auto &Var : dangerousVars) {
                errs() << "    " << Var << "\n";
            }
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