#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"

using namespace llvm;

int main()
{
    LLVMContext Context;
    auto ModPtr = std::make_unique<Module>("i128_add", Context);
    Module *Mod = ModPtr.get(); // raw pointer for convenience while building

    IRBuilder<> Builder(Context);
    Type *Int128Ty = Type::getInt128Ty(Context);

    std::vector<Type *> ParamTypes = {Int128Ty, Int128Ty};
    FunctionType *FuncType = FunctionType::get(Int128Ty, ParamTypes, false);

    Function *AddFunc = Function::Create(
        FuncType, Function::ExternalLinkage, "add_i128", Mod);

    BasicBlock *EntryBlock = BasicBlock::Create(Context, "entry", AddFunc);
    Builder.SetInsertPoint(EntryBlock);
    Argument *A = AddFunc->getArg(0);
    Argument *B = AddFunc->getArg(1);
    Value *Sum = Builder.CreateAdd(A, B, "sum");
    Builder.CreateRet(Sum);

    if (verifyFunction(*AddFunc, &llvm::errs()))
    {
        llvm::errs() << "Function verification failed!\n";
        return 1;
    }
    llvm::errs() << "Function verified successfully.\n\n";
    Mod->print(llvm::outs(), nullptr);

    // ---- JIT execution ----
    InitializeNativeTarget();
    InitializeNativeTargetAsmPrinter();

    std::string ErrStr;
    ExecutionEngine *EE = EngineBuilder(std::move(ModPtr))
                              .setErrorStr(&ErrStr)
                              .create();
    if (!EE)
    {
        llvm::errs() << "Failed to create ExecutionEngine: " << ErrStr << "\n";
        return 1;
    }

    uint64_t FuncAddr = EE->getFunctionAddress("add_i128");
    typedef __int128 (*AddFuncPtr)(__int128, __int128);
    AddFuncPtr AddI128 = reinterpret_cast<AddFuncPtr>(FuncAddr);

    __int128 ValA = 123456789012345678ULL;
    __int128 ValB = 987654321098765432ULL;

    __int128 SumResult = AddI128(ValA, ValB);

    APInt AVal(128, (uint64_t)ValA);
    APInt BVal(128, (uint64_t)ValB);
    APInt ResVal(128, (uint64_t)SumResult);

    llvm::outs() << "\n--- JIT execution result ---\n";
    llvm::outs() << "A = " << AVal << "\n";
    llvm::outs() << "B = " << BVal << "\n";
    llvm::outs() << "A + B = " << ResVal << "\n";
    return 0;
}
