//=============================================================================
// FILE:
//    Add2Sub.cpp
//
// DESCRIPTION:
//    Visit each function and replace each 'add' instruction with a 'sub'
//    instruction. This is a transformation pass. The pass is registered as a
//    plugin so that it can be invoked via the '-passes' flag in opt.
//
// USAGE:
//    New PM
//      opt -load-pass-plugin=libAdd2Sub.dylib -passes="add-2-sub" `\`
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

using namespace llvm;

//-----------------------------------------------------------------------------
// Add2Sub implementation
//-----------------------------------------------------------------------------
// No need to expose the internals of the pass to the outside world - keep
// everything in an anonymous namespace.
namespace {

// This method implements what the pass does
void visitor(Function &F) {
    // traverse each instruction in the function
    for(llvm::BasicBlock &BB : F) {
        for(auto I = BB.begin(), E = BB.end(); I != E; ) {
            // Get current instruction and increment iterator BEFORE any modification
            Instruction &Inst = *I++;
            // check if the instruction is an add instruction
            if(auto *BO = llvm::dyn_cast<llvm::BinaryOperator>(&Inst)) {
                if(BO->getOpcode() == llvm::Instruction::Add) {
                    // replace the add instruction with a sub instruction
                    llvm::Value *LHS = BO->getOperand(0);
                    llvm::Value *RHS = BO->getOperand(1);
                    IRBuilder<> Builder(BO);
                    Value *Sub = Builder.CreateSub(LHS, RHS, "sub");
                    // Copy flags
                    if(BO->hasNoSignedWrap())
                        cast<BinaryOperator>(Sub)->setHasNoSignedWrap(true);
                    if(BO->hasNoUnsignedWrap())
                        cast<BinaryOperator>(Sub)->setHasNoUnsignedWrap(true);
                    // Replace all uses and erase
                    BO->replaceAllUsesWith(Sub);
                    // Don't modify the iterator in the iteration loop
                    BO->eraseFromParent();
                }
            }
        }
    }
}

// New PM implementation
struct Add2Sub : PassInfoMixin<Add2Sub> {
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
llvm::PassPluginLibraryInfo getAdd2SubPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "Add2Sub", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM,
                   ArrayRef<PassBuilder::PipelineElement>) {
                  if (Name == "add-2-sub") {
                    FPM.addPass(Add2Sub());
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
  return getAdd2SubPluginInfo();
}
