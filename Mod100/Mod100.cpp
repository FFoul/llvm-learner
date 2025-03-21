//=============================================================================
// FILE:
//    Mod100.cpp
//
// DESCRIPTION:
//    Travese each ADD instruction and add a if statement after that to mod 100
//    of add result.
//
// USAGE:
//    New PM
//      opt -load-pass-plugin=libMod100.dylib -passes="mod-100" `\`
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
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

using namespace llvm;

//-----------------------------------------------------------------------------
// Mod100 implementation
//-----------------------------------------------------------------------------
// No need to expose the internals of the pass to the outside world - keep
// everything in an anonymous namespace.
namespace {

  void visitor(Function &F) {
    // Traverse each instruction in the function
    for (BasicBlock &BB : F) {
      for (auto I = BB.begin(), E = BB.end(); I != E; ) {
        // Get current instruction and increment iterator BEFORE any modification
        Instruction &Inst = *I++;
        
        // Check if the instruction is an add instruction
        if (auto *BO = dyn_cast<BinaryOperator>(&Inst)) {
          if (BO->getOpcode() == Instruction::Add) {
            // Find the next store instruction that uses the add result
            StoreInst *SI = nullptr;
            for (auto UI = BO->user_begin(), UE = BO->user_end(); UI != UE; ++UI) {
              if (auto *Store = dyn_cast<StoreInst>(*UI)) {
                SI = Store;
                break;
              }
            }
            // We should also find a store function for the real memory acces
            if (SI) {
              // Create reuqired BB
              BasicBlock *currentBB = SI->getParent();  
              BasicBlock *restBB = currentBB->splitBasicBlock(SI->getNextNode(), "rest");
              BasicBlock *modBB = BasicBlock::Create(F.getContext(), "mod", &F);
              currentBB->getTerminator()->eraseFromParent();
              // Create condition
              IRBuilder<> Builder(currentBB);
              Value *cond = Builder.CreateICmpSGT(BO, ConstantInt::get(BO->getType(), 100));
              Builder.CreateCondBr(cond, modBB, restBB);
              // Create mod instrution
              Builder.SetInsertPoint(modBB);
              Value *mod100 = Builder.CreateSRem(BO, ConstantInt::get(BO->getType(), 100));
              Builder.CreateStore(mod100, SI->getPointerOperand());
              Builder.CreateBr(restBB);
            }
          }
        }
      }
    }
  }

// New PM implementation
struct Mod100 : PassInfoMixin<Mod100> {
  // Main entry point, takes IR unit to run the pass on (&F) and the
  // corresponding pass manager (to be queried if need be)
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &) {
    visitor(F);
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
llvm::PassPluginLibraryInfo getMod100PluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "Mod100", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "mod-100") {
                    FPM.addPass(Mod100());
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
  return getMod100PluginInfo();
}
