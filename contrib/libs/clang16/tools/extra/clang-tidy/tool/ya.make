# Generated by devtools/yamaker.

PROGRAM(clang-tidy)

VERSION(16.0.0)

LICENSE(Apache-2.0 WITH LLVM-exception)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

PEERDIR(
    contrib/libs/clang16/lib
    contrib/libs/clang16/lib/AST
    contrib/libs/clang16/lib/ASTMatchers
    contrib/libs/clang16/lib/Analysis
    contrib/libs/clang16/lib/Analysis/FlowSensitive
    contrib/libs/clang16/lib/Analysis/FlowSensitive/Models
    contrib/libs/clang16/lib/Basic
    contrib/libs/clang16/lib/CrossTU
    contrib/libs/clang16/lib/Driver
    contrib/libs/clang16/lib/Edit
    contrib/libs/clang16/lib/Format
    contrib/libs/clang16/lib/Frontend
    contrib/libs/clang16/lib/Index
    contrib/libs/clang16/lib/Lex
    contrib/libs/clang16/lib/Parse
    contrib/libs/clang16/lib/Rewrite
    contrib/libs/clang16/lib/Sema
    contrib/libs/clang16/lib/Serialization
    contrib/libs/clang16/lib/StaticAnalyzer/Checkers
    contrib/libs/clang16/lib/StaticAnalyzer/Core
    contrib/libs/clang16/lib/StaticAnalyzer/Frontend
    contrib/libs/clang16/lib/Support
    contrib/libs/clang16/lib/Tooling
    contrib/libs/clang16/lib/Tooling/Core
    contrib/libs/clang16/lib/Tooling/Inclusions
    contrib/libs/clang16/lib/Tooling/Refactoring
    contrib/libs/clang16/lib/Tooling/Transformer
    contrib/libs/clang16/tools/extra/clang-tidy
    contrib/libs/clang16/tools/extra/clang-tidy/abseil
    contrib/libs/clang16/tools/extra/clang-tidy/altera
    contrib/libs/clang16/tools/extra/clang-tidy/android
    contrib/libs/clang16/tools/extra/clang-tidy/boost
    contrib/libs/clang16/tools/extra/clang-tidy/bugprone
    contrib/libs/clang16/tools/extra/clang-tidy/cert
    contrib/libs/clang16/tools/extra/clang-tidy/concurrency
    contrib/libs/clang16/tools/extra/clang-tidy/cppcoreguidelines
    contrib/libs/clang16/tools/extra/clang-tidy/darwin
    contrib/libs/clang16/tools/extra/clang-tidy/fuchsia
    contrib/libs/clang16/tools/extra/clang-tidy/google
    contrib/libs/clang16/tools/extra/clang-tidy/hicpp
    contrib/libs/clang16/tools/extra/clang-tidy/linuxkernel
    contrib/libs/clang16/tools/extra/clang-tidy/llvm
    contrib/libs/clang16/tools/extra/clang-tidy/llvmlibc
    contrib/libs/clang16/tools/extra/clang-tidy/misc
    contrib/libs/clang16/tools/extra/clang-tidy/modernize
    contrib/libs/clang16/tools/extra/clang-tidy/mpi
    contrib/libs/clang16/tools/extra/clang-tidy/objc
    contrib/libs/clang16/tools/extra/clang-tidy/openmp
    contrib/libs/clang16/tools/extra/clang-tidy/performance
    contrib/libs/clang16/tools/extra/clang-tidy/portability
    contrib/libs/clang16/tools/extra/clang-tidy/readability
    contrib/libs/clang16/tools/extra/clang-tidy/utils
    contrib/libs/clang16/tools/extra/clang-tidy/zircon
    library/cpp/clang_tidy/arcadia_checks
)

ADDINCL(
    contrib/libs/clang16/tools/extra/clang-tidy/tool
)

NO_COMPILER_WARNINGS()

NO_UTIL()

SRCS(
    ClangTidyToolMain.cpp
)

END()
