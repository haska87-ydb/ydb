UNITTEST_FOR(ydb/core/sys_view)

OWNER(
    monster
    g:kikimr
)

FORK_SUBTESTS()

IF (WITH_VALGRIND)
    TIMEOUT(3600)
    SIZE(LARGE)
    TAG(ya:fat)
ELSE()
    TIMEOUT(600)
    SIZE(MEDIUM)
ENDIF()

PEERDIR(
    library/cpp/testing/unittest
    library/cpp/yson/node
    ydb/core/kqp/ut/common
    ydb/core/testlib
    ydb/public/sdk/cpp/client/draft
)

YQL_LAST_ABI_VERSION()
 
SRCS(
    ut_kqp.cpp
    ut_common.cpp
    ut_counters.cpp
)

END()
