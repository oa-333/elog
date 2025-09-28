
target_sources(
    elog
    PUBLIC
        FILE_SET publicheaders
        TYPE HEADERS
        FILES
            elog_net_schema_handler.h
            elog_net_target_provider.h)