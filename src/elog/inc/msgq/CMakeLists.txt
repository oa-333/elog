
target_sources(
    elog
    PUBLIC
        FILE_SET publicheaders
        TYPE HEADERS
        FILES
            elog_kafka_msgq_target.h
            elog_msgq_schema_handler.h
            elog_msgq_target.h
            elog_msgq_target_provider.h)