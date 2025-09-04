
target_sources(
    elog
    PUBLIC
        FILE_SET publicheaders
        TYPE HEADERS
        FILES
            elog_async_schema_handler.h
            elog_async_target.h
            elog_async_target_provider.h
            elog_deferred_target.h
            elog_multi_quantum_target.h
            elog_quantum_target.h
            elog_queued_target.h)