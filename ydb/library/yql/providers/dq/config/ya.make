PROTO_LIBRARY() 
 
OWNER(g:yql) 
 
SRCS( 
    config.proto 
) 
 
PEERDIR( 
) 
 
IF (NOT PY_PROTOS_FOR) 
    EXCLUDE_TAGS(GO_PROTO) 
ENDIF() 
 
END() 
