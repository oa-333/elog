
target_sources(
    elog
    PUBLIC
        FILE_SET publicheaders
        TYPE HEADERS
        FILES
            elog_grpc_target.h
            elog_rpc_formatter.h
            elog_rpc_schema_handler.h
            elog_rpc_target_provider.h
            elog_rpc_target.h)