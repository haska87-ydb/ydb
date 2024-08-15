# Generated by devtools/yamaker.

LIBRARY()

VERSION(16.0.0)

LICENSE(Apache-2.0 WITH LLVM-exception)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

PEERDIR(
    contrib/libs/clang16
    contrib/libs/clang16/include
    contrib/libs/clang16/lib/ARCMigrate
    contrib/libs/clang16/lib/Basic
    contrib/libs/clang16/lib/CodeGen
    contrib/libs/clang16/lib/Driver
    contrib/libs/clang16/lib/ExtractAPI
    contrib/libs/clang16/lib/Frontend
    contrib/libs/clang16/lib/Frontend/Rewrite
    contrib/libs/clang16/lib/StaticAnalyzer/Frontend
    contrib/libs/llvm16
    contrib/libs/llvm16/lib/Option
    contrib/libs/llvm16/lib/Support
)

ADDINCL(
    contrib/libs/clang16/lib/FrontendTool
)

NO_COMPILER_WARNINGS()

NO_UTIL()

SRCS(
    ExecuteCompilerInvocation.cpp
)

END()
