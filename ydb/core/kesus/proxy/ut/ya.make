UNITTEST_FOR(ydb/core/kesus/proxy)

OWNER(
    snaury
    g:kikimr
)

FORK_SUBTESTS()

SPLIT_FACTOR(20)

TIMEOUT(600)

SIZE(MEDIUM)

PEERDIR(
    ydb/core/testlib
)

SRCS(
    proxy_actor_ut.cpp
    ut_helpers.cpp
)

YQL_LAST_ABI_VERSION()
 
END()
