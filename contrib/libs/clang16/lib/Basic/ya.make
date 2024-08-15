# Generated by devtools/yamaker.

LIBRARY()

VERSION(16.0.0)

LICENSE(Apache-2.0 WITH LLVM-exception)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

PEERDIR(
    contrib/libs/clang16
    contrib/libs/clang16/include
    contrib/libs/llvm16
    contrib/libs/llvm16/lib/Support
    contrib/libs/llvm16/lib/TargetParser
)

ADDINCL(
    contrib/libs/clang16/lib/Basic
)

NO_COMPILER_WARNINGS()

NO_UTIL()

SRCS(
    Attributes.cpp
    Builtins.cpp
    CLWarnings.cpp
    CharInfo.cpp
    CodeGenOptions.cpp
    Cuda.cpp
    DarwinSDKInfo.cpp
    Diagnostic.cpp
    DiagnosticIDs.cpp
    DiagnosticOptions.cpp
    ExpressionTraits.cpp
    FileEntry.cpp
    FileManager.cpp
    FileSystemStatCache.cpp
    IdentifierTable.cpp
    LangOptions.cpp
    LangStandards.cpp
    MakeSupport.cpp
    Module.cpp
    NoSanitizeList.cpp
    ObjCRuntime.cpp
    OpenCLOptions.cpp
    OpenMPKinds.cpp
    OperatorPrecedence.cpp
    ProfileList.cpp
    SanitizerSpecialCaseList.cpp
    Sanitizers.cpp
    Sarif.cpp
    SourceLocation.cpp
    SourceManager.cpp
    Stack.cpp
    TargetID.cpp
    TargetInfo.cpp
    Targets.cpp
    Targets/AArch64.cpp
    Targets/AMDGPU.cpp
    Targets/ARC.cpp
    Targets/ARM.cpp
    Targets/AVR.cpp
    Targets/BPF.cpp
    Targets/CSKY.cpp
    Targets/DirectX.cpp
    Targets/Hexagon.cpp
    Targets/Lanai.cpp
    Targets/Le64.cpp
    Targets/LoongArch.cpp
    Targets/M68k.cpp
    Targets/MSP430.cpp
    Targets/Mips.cpp
    Targets/NVPTX.cpp
    Targets/OSTargets.cpp
    Targets/PNaCl.cpp
    Targets/PPC.cpp
    Targets/RISCV.cpp
    Targets/SPIR.cpp
    Targets/Sparc.cpp
    Targets/SystemZ.cpp
    Targets/TCE.cpp
    Targets/VE.cpp
    Targets/WebAssembly.cpp
    Targets/X86.cpp
    Targets/XCore.cpp
    TokenKinds.cpp
    TypeTraits.cpp
    Version.cpp
    Warnings.cpp
    XRayInstr.cpp
    XRayLists.cpp
)

END()
