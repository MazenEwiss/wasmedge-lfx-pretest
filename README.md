# WasmEdge LFX Mentorship Pre-test - Wide Arithmetic Proposal

## Basic Information

- **Name:** Mazen Hatem Hassan
- **Email:** mazenrory@gmail.com
- **GitHub:** [MazenEwiss](https://github.com/MazenEwiss)
- **GitLab:** [MazenHassan](https://code.videolan.org/Mazen_Hassan)
- **Project applying to:** WasmEdge Runtime - Support for the Wide Arithmetic Proposal

## Checklist

- [x] I have read the [WasmEdge Mentorship Program Policy](https://github.com/WasmEdge/WasmEdge/blob/master/docs/mentorship-policy.md)
- [x] I have read the [LFX Mentorship Standards of Excellence](https://lf-projects.atlassian.net/wiki/spaces/PMO/pages/71538903/LFX+Mentorship+Standards+of+Excellence)

---

## Pre-test 1 - C++ Trace

**Task:** Show the output of the given `filterSum` C++ snippet.

**Trace** for `filterSum({4, 1, 5, 6, 2, 8, 3}, 5)`:

| n | condition (`n < limit`) | action | total | limit |
|---|---|---|---|---|
| 4 | 4 < 5 → true | total += 12 | 12 | 5 |
| 1 | 1 < 5 → true | total += 3 | 15 | 5 |
| 5 | 5 < 5 → false | limit += 1 | 15 | 6 |
| 6 | 6 < 6 → false | limit += 1 | 15 | 7 |
| 2 | 2 < 7 → true | total += 6 | 21 | 7 |
| 8 | 8 < 7 → false | limit += 1 | 21 | 8 |
| 3 | 3 < 8 → true | total += 9 | 30 | 8 |

**Output:** `30`

---

## Pre-test 2 - SIMD Instrumentation

**Task:** Build WasmEdge from source, create a `.wasm` file using a SIMD instruction, modify WasmEdge's execution engine to print extra information when executing that instruction, and provide the patch, the WAT source, and the captured output.

### Build

Built from the `master` branch on Ubuntu (WSL2), following the [official build docs](https://wasmedge.org/docs/contribute/source/os/linux):

```bash
sudo apt install -y software-properties-common cmake llvm-14-dev liblld-14-dev clang-14
git clone https://github.com/WasmEdge/WasmEdge.git
cd WasmEdge
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DWASMEDGE_BUILD_TESTS=ON ..
make -j$(nproc)
```

Build completed successfully - `[100%] Built target ...` with no errors.

### WASM file (WAT source)

The instruction targeted is `i32x4.add` (128-bit SIMD, four lane-wise 32-bit integer additions).

Operands are passed in as eight scalar `i32` function parameters (not `v128.const` literals) for two reasons, both discovered while working through this task:

1. **Constant folding:** an earlier version used `v128.const` literals directly. WasmEdge's loader precomputes the result of `i32x4.add` at load time when both operands are compile-time constants, so the interpreter's runtime dispatch code - the code we're trying to instrument - never actually executes. Using runtime function parameters instead forces genuine runtime execution.
2. **CLI limitation:** the `wasmedge` CLI's reactor-mode argument parser only understands scalar types (`i32`, `i64`, `f32`, `f64`) when invoking an exported function directly - it has no syntax for passing a `v128` value from the command line. A version of this function taking `(param v128) (param v128)` directly is valid WAT and would work fine if called through WasmEdge's embedding API, but cannot be invoked via the CLI at all (confirmed: it fails with a function signature mismatch when passed scalar arguments).

The four scalar params for each side are reassembled into a `v128` inside the function using `i32x4.splat` + `i32x4.replace_lane`.

`add_simd.wat`:

```wat
(module
  (func $add_simd
    (param $a0 i32) (param $a1 i32) (param $a2 i32) (param $a3 i32)
    (param $b0 i32) (param $b1 i32) (param $b2 i32) (param $b3 i32)
    (result v128)

    (local $lhs v128)
    (local $rhs v128)

    local.get $a0
    i32x4.splat
    local.set $lhs

    local.get $lhs
    local.get $a1
    i32x4.replace_lane 1
    local.set $lhs

    local.get $lhs
    local.get $a2
    i32x4.replace_lane 2
    local.set $lhs

    local.get $lhs
    local.get $a3
    i32x4.replace_lane 3
    local.set $lhs

    local.get $b0
    i32x4.splat
    local.set $rhs

    local.get $rhs
    local.get $b1
    i32x4.replace_lane 1
    local.set $rhs

    local.get $rhs
    local.get $b2
    i32x4.replace_lane 2
    local.set $rhs

    local.get $rhs
    local.get $b3
    i32x4.replace_lane 3
    local.set $rhs

    local.get $lhs
    local.get $rhs
    i32x4.add
  )
  (export "add_simd" (func $add_simd))
)
```

Compiled with:

```bash
wat2wasm add_simd.wat -o add_simd.wasm
```

### Modification to WasmEdge's execution engine

The interpreter's opcode dispatch lives in `lib/executor/engine/engine.cpp`, inside `Executor::execute`'s big `switch (OpCode)`. The `I32x4__add` case was modified to print both operands (before the operation) and the result (after), broken out lane-by-lane:

```diff
diff --git a/lib/executor/engine/engine.cpp b/lib/executor/engine/engine.cpp
index 53ffdd56..6fcddb68 100644
--- a/lib/executor/engine/engine.cpp
+++ b/lib/executor/engine/engine.cpp
@@ -9,6 +9,7 @@
 #include <array>
 #include <cstdint>
 #include <cstring>
+#include <iostream>

 using namespace std::literals;

@@ -24,6 +25,7 @@ Expect<void>
 Executor::runFunction(Runtime::StackManager &StackMgr,
                       const Runtime::Instance::FunctionInstance &Func,
                       Span<const ValVariant> Params) {
+  fprintf(stderr, "ENTERED runFunction\n");
   // Set start time.
   if (Stat && Conf.getStatisticsConfigure().isTimeMeasuring()) {
     Stat->startRecordWasm();
@@ -1526,7 +1528,22 @@ Expect<void> Executor::execute(Runtime::StackManager &StackMgr,
     }
     case OpCode::I32x4__add: {
       ValVariant Rhs = StackMgr.pop();
-      return runVectorAddOp<uint32_t>(StackMgr.getTop(), Rhs);
+      ValVariant &Lhs = StackMgr.getTop();
+
+      auto LhsVec = Lhs.get<uint32x4_t>();
+      auto RhsVec = Rhs.get<uint32x4_t>();
+
+      std::cout << "[SIMD-TRACE] I32x4.add lhs=("
+            << LhsVec[0] << "," << LhsVec[1] << ","
+            << LhsVec[2] << "," << LhsVec[3] << ") rhs=("
+            << RhsVec[0] << "," << RhsVec[1] << ","
+            << RhsVec[2] << "," << RhsVec[3] << ")\n";
+      auto Res = runVectorAddOp<uint32_t>(Lhs, Rhs);
+      auto ResVec = Lhs.get<uint32x4_t>();
+      std::cout << "[SIMD-TRACE] I32x4.add result=("
+            << ResVec[0] << "," << ResVec[1] << ","
+            << ResVec[2] << "," << ResVec[3] << ")\n";
+      return Res;
     }
     case OpCode::I32x4__sub: {
       ValVariant Rhs = StackMgr.pop();
```

Full patch file: [`simd_trace.patch`](./simd_trace.patch)

### Output

Ran the modified, freshly-built `wasmedge` binary against `add_simd.wasm`, invoking `add_simd` with operands `(7, 21, 26, 93)` and `(5, 69, 54, 10)`:

```bash
LD_LIBRARY_PATH=~/WasmEdge/build/lib/api:$LD_LIBRARY_PATH \
  ~/WasmEdge/build/tools/wasmedge/wasmedge --reactor add_simd.wasm add_simd 7 21 26 93 5 69 54 10
```

```
ENTERED runFunction
8160500740444966298418338070540
[SIMD-TRACE] I32x4.add lhs=(7,21,26,93) rhs=(5,69,54,10)
[SIMD-TRACE] I32x4.add result=(12,90,80,103)
```

The per-lane result `(12, 90, 80, 103)` matches the expected values (`7+5, 21+69, 26+54, 93+10`). The final line is the CLI's own generic printout of the returned `v128`, packed into a single large integer - the `[SIMD-TRACE]` lines are the ones added by this patch, showing the operation broken down lane-by-lane as it actually executes inside the interpreter.

---
 
## Pre-test 3 - LLVM API
 
**Task:** Write a small C++ program that uses the LLVM API to create two i128 integers and compute `a + b`, and provide the patch and the output.
 
### Program
 
Built using LLVM 14's C++ API (`LLVMContext`, `Module`, `IRBuilder`, `Function`). The program does two things: (1) programmatically constructs LLVM IR for a function `add_i128(i128, i128) -> i128`, and (2) JIT-compiles that IR to real machine code and actually calls it with concrete values, to verify the generated IR is not just well-formed but computes the correct result.
 
Two things worth noting, both discovered while writing this:
 
1. **Constructing the type:** LLVM supports arbitrary-width integers directly via `Type::getInt128Ty(Context)` - no need to hand-roll 128-bit arithmetic; `IRBuilder::CreateAdd` handles the 128-bit add natively once given two i128 values.
2. **JIT execution of i128 specifically:** LLVM 14's simpler `ExecutionEngine::runFunction` / `GenericValue` interface does not support "full-featured argument passing" for a type like i128 (confirmed by hitting `LLVM ERROR: MCJIT::runFunction does not support full-featured argument passing`). The fix was to fetch the compiled function's raw address with `getFunctionAddress`, cast it to a real C++ function pointer using GCC/Clang's built-in `__int128` type (which matches LLVM's `i128` layout), and call it directly like any normal function pointer.
`i128_add.cpp`:
 
```cpp
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/ExecutionEngine/GenericValue.h"
 
using namespace llvm;
 
int main() {
    LLVMContext Context;
    auto ModPtr = std::make_unique<Module>("i128_add", Context);
    Module *Mod = ModPtr.get();
 
    IRBuilder<> Builder(Context);
    Type *Int128Ty = Type::getInt128Ty(Context);
 
    std::vector<Type*> ParamTypes = { Int128Ty, Int128Ty };
    FunctionType *FuncType = FunctionType::get(Int128Ty, ParamTypes, false);
 
    Function *AddFunc = Function::Create(
        FuncType, Function::ExternalLinkage, "add_i128", Mod);
 
    BasicBlock *EntryBlock = BasicBlock::Create(Context, "entry", AddFunc);
    Builder.SetInsertPoint(EntryBlock);
    Argument *A = AddFunc->getArg(0);
    Argument *B = AddFunc->getArg(1);
    Value *Sum = Builder.CreateAdd(A, B, "sum");
    Builder.CreateRet(Sum);
 
    if (verifyFunction(*AddFunc, &llvm::errs())) {
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
    if (!EE) {
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
```
 
Compiled and run with:
 
```bash
clang++ -g i128_add.cpp `llvm-config-14 --cxxflags --ldflags --libs core mcjit native --system-libs` -o i128_add
./i128_add
```
 
Patch file (new-file diff): [`i128_add.patch`](./i128_add.patch)
 
### Output
 
```
Function verified successfully.
 
; ModuleID = 'i128_add'
source_filename = "i128_add"
 
define i128 @add_i128(i128 %0, i128 %1) {
entry:
  %sum = add i128 %0, %1
  ret i128 %sum
}
 
--- JIT execution result ---
A = 123456789012345678
B = 987654321098765432
A + B = 1111111110111111110
```
 
The printed IR confirms `add_i128` was correctly generated (verified with LLVM's own `verifyFunction`) as a genuine `i128` add. The JIT execution result confirms the generated IR is not just well-formed but computes the correct answer when actually run: `123456789012345678 + 987654321098765432 = 1111111110111111110`, independently checkable by hand.
 
---
 
## Proposal
 
 
*In progress.*