#include "llvm/Transforms/Utils/LoopParallelization.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/ValueSymbolTable.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/Module.h"
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

// Check if the array index contains the iteration variable e.g. i, i+1, i-5
bool doesArrayIndexContainIterationVariable(Value *index_, Value *V) {
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

// Get the source code location string from the debug location of an instruction
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
        if (DILocation *Loc = I.getDebugLoc()) {
            return getLocationString(Loc);
        }
    }
}

// Check and report any shared variables which are both loaded and assigned in the same iteration
void LoopParallelizationPass::checkSharedVariables(std::set<std::string>* assignedBeforeLoopVars,
    std::set<std::string>* assignedInsideLoopVars,
    std::set<std::string>* loadedInsideLoopVars,
    std::map<std::string, std::vector<DILocation*>>* varUseLocations) {
    
    for (std::string varName : *assignedBeforeLoopVars) {
        if(loadedInsideLoopVars->find(varName) != loadedInsideLoopVars->end()
        && assignedInsideLoopVars->find(varName) != assignedInsideLoopVars->end()) {
            errs() << "Problem for parallelization: Shared variable \"" << varName << "\" written to and read from inside for loop.\n";
            parallelizable = false;
            for (DILocation* loc : varUseLocations->at(varName)){
                errs() << "Location: " << getLocationString(loc) << "\n";
            }
            errs() << "\n";
        }
    }
    
}

// Check and report if the loop is accessed by different indexes (which still use the iteration variable) e.g. i, i+1, i-5
void LoopParallelizationPass::checkArrayIndexes(std::set<std::pair<Value*, std::string>>* storedInsideArrayAddressPtrs,
    std::set<std::pair<Value*, std::string>>* loadedInsideArrayAddressPtrs) {
    for (auto loadedValue : *loadedInsideArrayAddressPtrs) {
        for (auto storedValue : *storedInsideArrayAddressPtrs) {
            if (loadedValue.second == storedValue.second && !areValuesEquivalent(loadedValue.first, storedValue.first)) {
                errs() << "Problem for parallelization: Array or struct \""<< loadedValue.second << "\" is loading and storing with 2 different indexes\n";
                if (auto *I = dyn_cast<Instruction>(loadedValue.first)) {
                    if (DILocation *Loc = I->getDebugLoc()) {
                        errs() << "Location load: " << getLocationString(Loc) << "\n";
                    }
                }
                if (auto *I = dyn_cast<Instruction>(storedValue.first)) {
                    if (DILocation *Loc = I->getDebugLoc()) {
                        errs() << "Location store: " << getLocationString(Loc) << "\n\n";
                    }
                }
                parallelizable = false;
            }
        }
    }
}

// Find stored and loaded variables and record them
void LoopParallelizationPass::handleVariables(Instruction &I,
                    std::string iterationVariable,
                    std::set<std::string>* assignedBeforeLoopVars,
                    std::set<std::string>* assignedInsideLoopVars,
                    std::map<std::string, std::vector<DILocation*>>* varUseLocations,
                    std::set<std::string>* loadedInsideLoopVars,
                    std::set<std::string>* arrayAddressPtrs) {
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
            if (varUseLocations->find(varName) == varUseLocations->end()) {
                (*varUseLocations)[varName] = std::vector<DILocation*>();
            }
            if (DILocation *Loc = LI->getDebugLoc()) {
                varUseLocations->at(varName).push_back(Loc);
            }
            
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

            assignedInsideLoopVars->insert(varName);
            if (varUseLocations->find(varName) == varUseLocations->end()) {
                (*varUseLocations)[varName] = std::vector<DILocation*>();
            }
            if (DILocation *Loc = SI->getDebugLoc()) {
                varUseLocations->at(varName).push_back(Loc);
            }
        }
    }
}

// Get array(and struct) accesses and keep track of loads and stores for different indexes
void LoopParallelizationPass::handleArrays(Instruction &I,
                    Value* iterationVariable,
                    std::set<std::string>* assignedInsideLoopArrays,
                    std::set<std::string>* loadedInsideLoopArrays,
                    std::set<std::string>* arrayAddressPtrs,
                    std::set<std::pair<Value*, std::string>>* storedInsideArrayAddressPtrs,
                    std::set<std::pair<Value*, std::string>>* loadedInsideArrayAddressPtrs) {

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
                    if (!doesArrayIndexContainIterationVariable(index, iterationVariable)) {
                        assignedInsideLoopArrays->insert(arrayName);
                    }

                    storedInsideArrayAddressPtrs->insert({index, arrayName});
                }
            }
        }
    }
    if (auto *LI = dyn_cast<LoadInst>(&I)) {
        // Check if it loads from a variable
        if (Value *Ptr = LI->getPointerOperand()) {

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
                    if (!doesArrayIndexContainIterationVariable(index, iterationVariable)) {
                        loadedInsideLoopArrays->insert(index->getName().str());
                    }

                    loadedInsideArrayAddressPtrs->insert({index, arrayName});
                }
            }
        }
    }
}



void LoopParallelizationPass::handleFunctions(Instruction &I) {
    
    if (auto *CI = dyn_cast<CallBase>(&I)) {
        if (isa<CallInst>(CI) || isa<InvokeInst>(CI)) {
            Function *F = CI->getCalledFunction();
            if (F) {
                errs() << "Problem for parallelization: Called function: " << F->getName() << "\n";
                errs() << "Location: " << getLocationString(CI->getDebugLoc()) << "\n\n";
                parallelizable = false;
            }
            for (unsigned i = 0; i < F->arg_size(); i++) {
                Value *argOperand = CI->getArgOperand(i);
                if (auto *LI = dyn_cast<LoadInst>(argOperand)) {
                    if (Value *Ptr = LI->getPointerOperand()) {
                        if (Ptr->hasName()) {
                            errs() << "    Arg "<< i << ": " << Ptr->getName().str() << "\n";
                        }
                    }
                }
            }
            errs() << "\n";
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
    
    for (Loop *L : LI) {
        Value* iterationVariable;

        std::set<std::string> assignedBeforeLoopVars;

        std::map<std::string, std::vector<DILocation*>> varUseLocations;

        std::set<std::string> assignedInsideLoopVars;
        std::set<std::string> loadedInsideLoopVars;
        
        std::set<std::string> assignedInsideLoopArrays;
        std::set<std::string> loadedInsideLoopArrays;

        std::set<std::string> arrayAddressPtrs;
        std::set<std::pair<Value*, std::string>> storedInsideArrayAddressPtrs;
        std::set<std::pair<Value*, std::string>> loadedInsideArrayAddressPtrs;

        BasicBlock *Preheader = L->getLoopPreheader();
        BasicBlock *Header = L->getHeader();

        if (!Preheader) {
            errs() << "Loop does not have a preheader, skipping.\n";
            continue;
        }
        if (!Header) {
            errs() << "Loop does not have a header, skipping.\n";
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

        for (BasicBlock &BB : F) {
            if (DT.dominates(&BB, Preheader)) {
                for (Instruction &I : BB) {
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

        for (BasicBlock *BB : L->blocks()) {
            for (Instruction &I : *BB) {
                handleArrays(I, iterationVariable, &assignedInsideLoopArrays, &loadedInsideLoopArrays,  &arrayAddressPtrs, &storedInsideArrayAddressPtrs, &loadedInsideArrayAddressPtrs);
                handleVariables(I, iterationVariable->getName().str(), &assignedBeforeLoopVars, &assignedInsideLoopVars, &varUseLocations, &loadedInsideLoopVars, &arrayAddressPtrs);  
                handleFunctions(I);
            }
        }
        checkSharedVariables(&assignedBeforeLoopVars, &assignedInsideLoopVars, &loadedInsideLoopVars, &varUseLocations);
        checkArrayIndexes(&storedInsideArrayAddressPtrs, &loadedInsideArrayAddressPtrs);
        
        if (parallelizable) {
            errs() << "No detected race conditions" << "\n";
            errs() << "Loop is parallelizable" << "\n";
        }
    }
    return PreservedAnalyses::all();
}