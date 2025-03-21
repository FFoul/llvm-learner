//=============================================================================
// FILE:
//    AddFunction.cpp
//
// DESCRIPTION:
//     Add a modulo function after each add operation in the source code.
//
// USAGE:
//    New PM
//      opt -load-pass-plugin=libAddFunction.dylib|so -passes="add-function" `\`
//        -disable-output <input-llvm-file>
//
//
// License: MIT
//=============================================================================
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

//-----------------------------------------------------------------------------
// Mod100 implementation
//-----------------------------------------------------------------------------
// No need to expose the internals of the pass to the outside world - keep
// everything in an anonymous namespace.
namespace {

    Function* CreateModuloFunction(Module &M) {
        LLVMContext &Ctx = M.getContext();
        
        // Check if function already exists to avoid duplicates
        if (Function *F = M.getFunction("Modulo"))
            return F;
        
        // Create function type (int Modulo(int))
        Type *IntTy = Type::getInt32Ty(Ctx);
        std::vector<Type *> argTypes = {IntTy};
        FunctionType *FuncTy = FunctionType::get(IntTy, argTypes, false);
        
        // Create function
        Function *ModuloFunction = Function::Create(
            FuncTy, Function::ExternalLinkage, "Modulo", M);
        
        // Set argument name
        Function::arg_iterator args = ModuloFunction->arg_begin();
        Value *Arg = args++;
        Arg->setName("num");
        
        // Create basic blocks
        BasicBlock *EntryBB = BasicBlock::Create(Ctx, "entry", ModuloFunction);
        BasicBlock *ModBB = BasicBlock::Create(Ctx, "mod", ModuloFunction);
        BasicBlock *ContinueBB = BasicBlock::Create(Ctx, "continue", ModuloFunction);
        
        // Entry block: check if value > 100
        IRBuilder<> Builder(EntryBB);
        Value *Cmp = Builder.CreateICmpSGT(Arg, ConstantInt::get(IntTy, 100), "cmp");
        Builder.CreateCondBr(Cmp, ModBB, ContinueBB);
        
        // Mod block: perform modulo operation
        Builder.SetInsertPoint(ModBB);
        Value *Mod = Builder.CreateSRem(Arg, ConstantInt::get(IntTy, 100), "mod");
        Builder.CreateBr(ContinueBB);
        
        // Continue block: return either original value or modulo result
        Builder.SetInsertPoint(ContinueBB);
        PHINode *Result = Builder.CreatePHI(IntTy, 2, "result");
        Result->addIncoming(Arg, EntryBB);
        Result->addIncoming(Mod, ModBB);
        Builder.CreateRet(Result);
        
        // Verify the function
        verifyFunction(*ModuloFunction);
        
        return ModuloFunction;
    }
    
    void visitor(Function &F, Function *ModuloFunction) {
        // Skip processing the Modulo function itself to avoid recursion
        if (F.getName() == "Modulo")
            return;
        
        // Traverse each instruction in the function
        for (BasicBlock &BB : F) {
            for (auto I = BB.begin(), E = BB.end(); I != E; ) {
                // Get current instruction and increment iterator BEFORE any modification
                Instruction &Inst = *I++;
                
                // Check if the instruction is an add instruction
                if (auto *BO = dyn_cast<BinaryOperator>(&Inst)) {
                    if (BO->getOpcode() == Instruction::Add) {
                        // Find store instructions that use the add result
                        StoreInst *SI = nullptr;
                        for (auto UI = BO->user_begin(), UE = BO->user_end(); UI != UE; ++UI) {
                            if (auto *Store = dyn_cast<StoreInst>(*UI)) {
                                SI = Store;
                                break;
                            }
                        }    
                        if (SI) {
                            // Insert call to Modulo function after the ADD instruction
                            IRBuilder<> Builder(BO->getNextNode());
                            std::vector<Value*> Args = {BO}; // Pass the ADD result as argument
                            CallInst *Call = Builder.CreateCall(ModuloFunction, Args, "modulo_result");
                            
                            // Replace the original value in the store instruction with the modulo result
                            SI->setOperand(0, Call);
                        }
                    }
                }
            }
        }
    }
    

// New PM implementation
struct AddFunction : PassInfoMixin<AddFunction> {
  // Main entry point, takes IR unit to run the pass on (&F) and the
  // corresponding pass manager (to be queried if need be)
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    Function *ModuloFunction = CreateModuloFunction(*F.getParent());
    visitor(F, ModuloFunction);
    return PreservedAnalyses::all();
  }

  // Without isRequired returning true, this pass will be skipped for functions
  // decorated with the optnone LLVM attribute. Note that clang -O0 decorates
  // all functions with optnone.
  static bool isRequired() { return true; }
};
} // namespace

//-----------------------------------------------------------------------------
// New PM Registration
//-----------------------------------------------------------------------------
llvm::PassPluginLibraryInfo getAddFunctionPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "AddFunction", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "add-function") {
                    FPM.addPass(AddFunction());
                    return true;
                  }
                  return false;
                });
          }};
}

// This is the core interface for pass plugins. It guarantees that 'opt' will
// be able to recognize Add2Sub when added to the pass pipeline on the
// command line, i.e. via '-passes=add-2-sub'
extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getAddFunctionPluginInfo();
}
