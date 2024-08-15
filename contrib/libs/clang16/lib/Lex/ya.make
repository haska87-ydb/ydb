# Generated by devtools/yamaker.

LIBRARY()

VERSION(16.0.0)

LICENSE(Apache-2.0 WITH LLVM-exception)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

PEERDIR(
    contrib/libs/clang16
    contrib/libs/clang16/include
    contrib/libs/clang16/lib/Basic
    contrib/libs/llvm16
    contrib/libs/llvm16/lib/Support
    contrib/libs/llvm16/lib/TargetParser
)

ADDINCL(
    contrib/libs/clang16/lib/Lex
)

NO_COMPILER_WARNINGS()

NO_UTIL()

SRCS(
    DependencyDirectivesScanner.cpp
    HeaderMap.cpp
    HeaderSearch.cpp
    InitHeaderSearch.cpp
    Lexer.cpp
    LiteralSupport.cpp
    MacroArgs.cpp
    MacroInfo.cpp
    ModuleMap.cpp
    PPCaching.cpp
    PPCallbacks.cpp
    PPConditionalDirectiveRecord.cpp
    PPDirectives.cpp
    PPExpressions.cpp
    PPLexerChange.cpp
    PPMacroExpansion.cpp
    Pragma.cpp
    PreprocessingRecord.cpp
    Preprocessor.cpp
    PreprocessorLexer.cpp
    ScratchBuffer.cpp
    TokenConcatenation.cpp
    TokenLexer.cpp
)

END()
