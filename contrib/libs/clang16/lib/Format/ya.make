# Generated by devtools/yamaker.

LIBRARY()

VERSION(16.0.0)

LICENSE(
    Apache-2.0 WITH LLVM-exception AND
    NCSA
)

LICENSE_TEXTS(.yandex_meta/licenses.list.txt)

PEERDIR(
    contrib/libs/clang16
    contrib/libs/clang16/include
    contrib/libs/clang16/lib/Basic
    contrib/libs/clang16/lib/Lex
    contrib/libs/clang16/lib/Tooling/Core
    contrib/libs/clang16/lib/Tooling/Inclusions
    contrib/libs/llvm16
    contrib/libs/llvm16/lib/Support
)

ADDINCL(
    contrib/libs/clang16/lib/Format
)

NO_COMPILER_WARNINGS()

NO_UTIL()

SRCS(
    AffectedRangeManager.cpp
    BreakableToken.cpp
    ContinuationIndenter.cpp
    DefinitionBlockSeparator.cpp
    Format.cpp
    FormatToken.cpp
    FormatTokenLexer.cpp
    IntegerLiteralSeparatorFixer.cpp
    MacroCallReconstructor.cpp
    MacroExpander.cpp
    NamespaceEndCommentsFixer.cpp
    QualifierAlignmentFixer.cpp
    SortJavaScriptImports.cpp
    TokenAnalyzer.cpp
    TokenAnnotator.cpp
    UnwrappedLineFormatter.cpp
    UnwrappedLineParser.cpp
    UsingDeclarationsSorter.cpp
    WhitespaceManager.cpp
)

END()
