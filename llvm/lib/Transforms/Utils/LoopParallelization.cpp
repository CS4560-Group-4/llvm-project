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


bool areValuesEquivalent(Value *V1, Value *V2) {
    if (V1 == V2) return true; // Same Value object

    // If one is null and the other is not, they aren't equivalent
    if (!V1 || !V2) return false;

    // errs() << "reached here 0\n";

    Instruction *I1 = dyn_cast<Instruction>(V1);
    Instruction *I2 = dyn_cast<Instruction>(V2);

    // errs() << "reached here 1\n";
    if (!I1 || !I2) {
        // One or both are not instructions (e.g., constants, function arguments)
        // This is a simplification. You'd need to handle these cases more
        // comprehensively.  For example, compare constant values.
        if (isa<Constant>(V1) && isa<Constant>(V2)) {
            return V1 == V2; // check if the Constant pointer addresses match
        }
        return false; // for now, assume not equivalent
    }
    // errs() << "reached here 2\n";

    if (I1->getOpcode() != I2->getOpcode()) return false; // Different opcodes

    // Compare operands recursively
    if (I1->getNumOperands() != I2->getNumOperands()) return false;
    for (unsigned i = 0, e = I1->getNumOperands(); i != e; ++i) {
        if (!areValuesEquivalent(I1->getOperand(i), I2->getOperand(i))) {
            return false;
        }
    }


    return true; // If we get here, the values are likely equivalent
}


bool isLoopIndexIterationVariable(Value *index_, Value *V) {
    Value *index = index_;

    if (auto *CASTI = dyn_cast<CastInst>(index)) {
        if (CASTI->getOpcode() == Instruction::ZExt || CASTI->getOpcode() == Instruction::SExt) {
            if (Value *Op = CASTI->getOperand(0)) {
                index = dyn_cast<Instruction>(Op);
            }
        }
    }

    if (index == V) {
        return true;
    } else if (auto *LI = dyn_cast<LoadInst>(index)) {
        if (LI->getPointerOperand() == V) {
            return true;
        }
    } else if (auto *I = dyn_cast<Instruction>(index)) {
        if (I->getOpcode() == Instruction::Add || I->getOpcode() == Instruction::Sub) {
            if (auto *LI = dyn_cast<LoadInst>(I->getOperand(0))) {
                if (LI->getPointerOperand() == V) {
                    return true;
                }
            }
            if (auto *LI = dyn_cast<LoadInst>(I->getOperand(1))) {
                if (LI->getPointerOperand() == V) {
                    return true;
                }
            }
        }
    }

    return false;
}

Value* getIterationVariable(BasicBlock *Header) {
    // Get the iteration variable of the loop
    if (!Header->empty()) {
        Instruction &FirstInst = Header->front();
        if (auto *SI = dyn_cast<LoadInst>(&FirstInst)) {
            if (Value *Ptr = SI->getPointerOperand()) {
                if (Ptr->hasName()) {
                    return Ptr;
                }
            }
        }
    } else {
        // return "";
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
                    std::string iterationVariable,
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

void handleVariables_new(Instruction &I,
    const std::string iterationVariable) {

    if (auto *SI = dyn_cast<StoreInst>(&I)) {
        // Check if it assigns to a variable
        if (auto *Ptr = SI->getPointerOperand()) {
            if (!Ptr->hasName()) {
                return;    
            }
            errs() << "Variable: " << Ptr->getName() << "\n";
            // Get pointer uses
            for (auto &U : Ptr->uses()) {
                if (auto *User = dyn_cast<Instruction>(U.getUser())) {
                    if (User->hasName()) {
                        errs() << "Variable use: " << User->getName() << "\n";
                    }
                }
            }
            
            if (auto *LI = dyn_cast<LoadInst>(Ptr)) {
                if (LI->hasName()) {
                    errs() << "LOAD Variable: " << LI->getName() << "\n";
                }
                if (auto *Var = LI->getPointerOperand()) {
                    if (Var == Ptr) {
                        errs() << "1 DANGER: Variable \'" << Ptr->getName() << "\' was stored after being loaded.\n";
                    }
                    if (Var->getName() == Ptr->getName()) {
                        errs() << "2 DANGER: Variable \'" << Ptr->getName() << "\' was stored after being loaded.\n";
                    }
                }
            }
        }
    }
}

void handleArrays(Instruction &I,
                    Value* iterationVariable,
                    std::set<std::string>* assignedInsideLoopArrays,
                    std::set<std::string>* assignedInsideLoopVars,
                    std::set<std::string>* loadedInsideLoopVars,
                    std::set<std::string>* arrayAddressPtrs,
                    std::set<Value*>* assignedInsideArrayAddressPtrs,
                    std::set<Value*>* loadedInsideArrayAddressPtrs) {
    // // Instruction that assign array location to a variable
    // if (auto *GEPI = dyn_cast<GetElementPtrInst>(&I)) {
    //     if (GEPI->hasName()) {
    //         arrayAddressPtrs->insert(GEPI->getName().str());
    //     }
    //     if (Value *Ptr = GEPI->getPointerOperand()) {
    //         if (Ptr->hasName()) {
    //             assignedInsideLoopArrays->insert(Ptr->getName().str());
    //         }
    //     }

    //     if (Value *index = GEPI->getOperand(1)) {
    //         if (index->hasName()) {
    //             // assignedInsideLoopArrays->insert(index->getName().str());
    //             errs() << "Array index: " << index->getName() << "\n";
    //             // errs() << index->getContext().diagnose() << "\n";
    //             // errs() << index->getType() << "\n";
    //             // errs() << index << "\n";
    //         }
    //     }

    //     if (Value *index = GEPI->getOperand(2)) {
    //         if (index->hasName()) {
    //             // assignedInsideLoopArrays->insert(index->getName().str());
    //             errs() << "Array index: " << index->getName() << "\n";
    //             // errs() << index->getContext().diagnose() << "\n";
    //             // errs() << index->getType(). << "\n";
    //             // errs() << index << "\n";
    //         }
    //     }
    // }

    // Check if the instruction is a store instruction
    if (auto *SI = dyn_cast<StoreInst>(&I)) {
        // Check if it assigns to a variable
        if (Value *Ptr = SI->getPointerOperand()) {

            if (!Ptr->hasName()) {
                return;   
            }
            
            if (auto *GEPI = dyn_cast<GetElementPtrInst>(Ptr)) {
                if (GEPI->hasName()) {
                    errs() << "Array address pointer: " << GEPI->getName() << "\n";
                }

                std::string loopName = "undefined loop";
                if (Value *Ptr = GEPI->getPointerOperand()) {
                    if (Ptr->hasName()) {
                        loopName = Ptr->getName().str();
                    }
                }

                if (Value *index = GEPI->getOperand(2)) {
                    if (!isLoopIndexIterationVariable(index, iterationVariable)) {
                        assignedInsideLoopVars->insert(index->getName().str());
                    }

                    assignedInsideArrayAddressPtrs->insert(index);

                    for (Value *loadedValues : *loadedInsideArrayAddressPtrs) {

                        if (!areValuesEquivalent(loadedValues, index)) {
                            errs() << "DANGER: Array \""<< loopName << "\" is loading and storing with 2 different indexes\n";
                            errs() << "Location: " << getLocationString(SI->getDebugLoc()) << "\n\n";
                        }
                    }
                }
            }
        }
    }
    if (auto *SI = dyn_cast<LoadInst>(&I)) {
        // Check if it assigns to a variable
        if (Value *Ptr = SI->getPointerOperand()) {

            if (!Ptr->hasName()) {
                return;   
            }
            
            if (auto *GEPI = dyn_cast<GetElementPtrInst>(Ptr)) {
                if (GEPI->hasName()) {
                    errs() << "Array address pointer: " << GEPI->getName() << "\n";
                }


                std::string loopName = "undefined loop";
                if (Value *Ptr = GEPI->getPointerOperand()) {
                    if (Ptr->hasName()) {
                        loopName = Ptr->getName().str();
                    }
                }

                if (Value *index = GEPI->getOperand(2)) {
                    if (index->hasName()) {
                        errs() << "Array index: " << index->getName() << "\n";
                    }

                    if (!isLoopIndexIterationVariable(index, iterationVariable)) {
                        loadedInsideLoopVars->insert(index->getName().str());
                    }

                    loadedInsideArrayAddressPtrs->insert(index);

                    for (Value *assignedValues : *assignedInsideArrayAddressPtrs) {
                        if (!areValuesEquivalent(assignedValues, index)) {
                            errs() << "DANGER: Array \""<< loopName << "\" is loading and storing with 2 different indexes\n";
                            errs() << "Location: " << getLocationString(SI->getDebugLoc()) << "\n\n";
                        }
                    }
                }
            }
        }
    }
}
                    // if (!index->getType()->isIntegerTy()) {
                    //     errs() << "Array index is not an integer type: " << *index->getType() << "\n";
                    // }
                    // if (auto *LI = dyn_cast<LoadInst>(index)) {
                    //     if (LI->hasName()) {
                    //         errs() << "LOAD Array index: " << LI->getName() << "\n";
                    //     }
                    // } else if (auto *SI = dyn_cast<StoreInst>(index)) {
                    //     if (SI->hasName()) {
                    //         errs() << "STORE Array index: " << SI->getName() << "\n";
                    //     }
                    // } else if (auto *SEXTI = dyn_cast<CastInst>(index)) {
                    //     if (SEXTI->hasName()) {
                    //         errs() << "SEXT Array index 1: " << SEXTI->getName() << "\n";
                    //     }
                    //     if (int a = SEXTI->getNumOperands()) {
                    //         errs() << "SEXT Array index 4: " << a << "\n";
                    //     }

                    //     if (auto *Assign = SEXTI->getOperand(0)) {
                    //         indexAssignments.insert(Assign);
                    //         // if (auto *Var = dyn_cast<Constant>(Assign)) {
                    //         //     if (Var->hasName()) {
                    //         //         errs() << "SEXT Array index 2: " << Var->getName() << "\n"; // TODO: handle as a variable
                    //         //     }
                    //         // }
                    //         // else if (auto *LI = dyn_cast<LoadInst>(Assign)) {
                    //         //     if (Value *Ptr = LI->getPointerOperand()) {
                    //         //         if (Ptr->hasName()) {
                    //         //             errs() << "SEXT Array index 2: " << Ptr->getName() << "\n";
                    //         //         }
                    //         //     }
                                
                    //         // }

                    //     }

                    // }
                    // else {
                    //     errs() << "Array index: " << index->getType() << "\n";
                    // }


PreservedAnalyses LoopParallelizationPass::run(Function &F,
  FunctionAnalysisManager &AM) {
    errs() << "Hello" << "\n";
    auto &LI = AM.getResult<LoopAnalysis>(F);
    auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
    // OS << "Loop info for function '" << F.getName() << "':\n";
    // LI.print(OS);
    
    for (Loop *L : LI) {
        Value* iterationVariable;

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
        std::set<Value*> assignedInsideArrayAddressPtrs;
        std::set<Value*> loadedInsideArrayAddressPtrs;
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
        errs() << "Iteration variable: " << *iterationVariable << "\n\n";

        // Collect variables used inside the loop
        for (BasicBlock *BB : L->blocks()) {
            for (Instruction &I : *BB) {
                handleArrays(I, iterationVariable, &assignedInsideLoopArrays, &assignedInsideLoopVars, &loadedInsideLoopVars,  &arrayAddressPtrs, &assignedInsideArrayAddressPtrs, &loadedInsideArrayAddressPtrs);
                
                
                handleVariables(I, iterationVariable->getName().str(), &initializedInsideLoopVars, &loadedInsideLoopVars, &arrayAddressPtrs);  
                // handleVariables_new(I, iterationVariable);
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
            errs() << "    " << *Var << "\n";
        }
        errs() << "  Variables loaded inside the loop (array address pointers):\n";
        for (const auto &Var : loadedInsideArrayAddressPtrs) {
            errs() << "    " << *Var << "\n";
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