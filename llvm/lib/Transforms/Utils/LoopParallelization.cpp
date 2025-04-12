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

// Function to check if two values are equivalent
bool areValuesEquivalent(Value *V1, Value *V2) {
    if (V1 == V2){
        return true;
    }
    if (!V1 || !V2){
        return false;
    }
    Instruction *I1 = dyn_cast<Instruction>(V1);
    Instruction *I2 = dyn_cast<Instruction>(V2);

    if (!I1 || !I2) {
        if (isa<Constant>(V1) && isa<Constant>(V2)) {
            return V1 == V2;
        }
        return false;
    }

    if (I1->getOpcode() != I2->getOpcode()){
        return false;
    }

    // Compare operands recursively
    if (I1->getNumOperands() != I2->getNumOperands()){
        return false;
    }
    for (unsigned i = 0, e = I1->getNumOperands(); i != e; ++i) {
        if (!areValuesEquivalent(I1->getOperand(i), I2->getOperand(i))) {
            return false;
        }
    }

    return true;
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

// Function to get the iteration variable from the loop header
Value* getIterationVariable(BasicBlock *Header) {
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

            if (varName == iterationVariable) {
                return; // Skip the iteration variable
            } else if (arrayAddressPtrs->find(varName) != arrayAddressPtrs->end()) {
                return; // Skip array address pointers
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


            
            if (varName == iterationVariable) {
                return; // Skip the iteration variable
            } else if (arrayAddressPtrs->find(varName) != arrayAddressPtrs->end()) {
                return; // Skip array address pointers
            }

            if(loadedInsideLoopVars->find(varName) == loadedInsideLoopVars->end()) {
                initializedInsideLoopVars->insert(varName);
            } else {
                if (initializedInsideLoopVars->find(varName) == initializedInsideLoopVars->end()) {
                    errs() << "DANGER: Variable \'" << varName << "\' was stored after being loaded.\n";
                    errs() << "Location: " << getLocationString(SI->getDebugLoc()) << "\n\n";
                }
            }
        }
    }
}

void handleArrays(Instruction &I,
                    Value* iterationVariable,
                    std::set<std::string>* initializedInsideLoopVars,
                    std::set<std::string>* loadedInsideLoopVars,
                    std::set<std::string>* arrayAddressPtrs,
                    std::set<Value*>* assignedInsideArrayAddressPtrs,
                    std::set<Value*>* loadedInsideArrayAddressPtrs) {

    // Check if the instruction is a store instruction
    if (auto *SI = dyn_cast<StoreInst>(&I)) {
        // Check if it assigns to a variable
        if (Value *Ptr = SI->getPointerOperand()) {

            if (!Ptr->hasName()) {
                return;   
            }
            
            if (auto *GEPI = dyn_cast<GetElementPtrInst>(Ptr)) {
                arrayAddressPtrs->insert(GEPI->getName().str());

                std::string arrayName = "undefined loop";
                if (Value *Ptr = GEPI->getPointerOperand()) {
                    if (Ptr->hasName()) {
                        arrayName = Ptr->getName().str();
                    }
                }

                if (Value *index = GEPI->getOperand(2)) {
                    if (!isLoopIndexIterationVariable(index, iterationVariable)) { // TODO handle this
                        if (loadedInsideLoopVars->find(index->getName().str()) == loadedInsideLoopVars->end()) {
                            initializedInsideLoopVars->insert(index->getName().str());
                        } else {
                            if (initializedInsideLoopVars->find(index->getName().str()) == initializedInsideLoopVars->end()) {
                                errs() << "DANGER: Address in array \'" << arrayName << "\' was stored after being loaded.\n";
                                errs() << "Location: " << getLocationString(SI->getDebugLoc()) << "\n\n";
                            }
                        }
                    }

                    assignedInsideArrayAddressPtrs->insert(index);

                    for (Value *loadedValue : *loadedInsideArrayAddressPtrs) {

                        if (!areValuesEquivalent(loadedValue, index)) {
                            errs() << "DANGER: Array \""<< arrayName << "\" is loading and storing with 2 different indexes\n";
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

                std::string arrayName = "undefined loop";
                if (Value *Ptr = GEPI->getPointerOperand()) {
                    if (Ptr->hasName()) {
                        arrayName = Ptr->getName().str();
                    }
                }

                if (Value *index = GEPI->getOperand(2)) {
                    if (!isLoopIndexIterationVariable(index, iterationVariable)) {
                        loadedInsideLoopVars->insert(index->getName().str());
                    }

                    loadedInsideArrayAddressPtrs->insert(index);

                    for (Value *assignedValue : *assignedInsideArrayAddressPtrs) {
                        if (!areValuesEquivalent(assignedValue, index)) {
                            errs() << "DANGER: Array \""<< arrayName << "\" is loading and storing with 2 different indexes\n";
                            errs() << "Location: " << getLocationString(SI->getDebugLoc()) << "\n\n";
                        }
                    }
                }
            }
        }
    }
}


PreservedAnalyses LoopParallelizationPass::run(Function &F,
  FunctionAnalysisManager &AM) {

    if (F.isDeclaration()) {
        return PreservedAnalyses::all();
    }

    if (F.getLinkage() != GlobalValue::ExternalLinkage) {
        // Function is likely not from analyzed file, skip it.
        return PreservedAnalyses::all();
    }

    auto &LI = AM.getResult<LoopAnalysis>(F);
    auto &DT = AM.getResult<DominatorTreeAnalysis>(F);
    // OS << "Loop info for function '" << F.getName() << "':\n";
    // LI.print(OS);
    
    for (Loop *L : LI) {
        Value* iterationVariable;

        std::set<std::string> initializedInsideLoopVars;
        std::set<std::string> loadedInsideLoopVars;

        std::set<std::string> arrayAddressPtrs;
        std::set<Value*> assignedInsideArrayAddressPtrs;
        std::set<Value*> loadedInsideArrayAddressPtrs;

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
        if (!iterationVariable) {
            errs() << "No iteration variable found in loop header.\n";
            continue;
        }
        
        errs() << "Iteration variable: " << iterationVariable->getName().str() << "\n\n";

        // Collect variables used inside the loop
        for (BasicBlock *BB : L->blocks()) {
            for (Instruction &I : *BB) {
                handleArrays(I, iterationVariable, &initializedInsideLoopVars, &loadedInsideLoopVars,  &arrayAddressPtrs, &assignedInsideArrayAddressPtrs, &loadedInsideArrayAddressPtrs);
                
                handleVariables(I, iterationVariable->getName().str(), &initializedInsideLoopVars, &loadedInsideLoopVars, &arrayAddressPtrs);  
                // handleVariables_new(I, iterationVariable);
            }
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

    }
    return PreservedAnalyses::all();
}