
target_sources(
    elog
    PUBLIC
        FILE_SET publicheaders
        TYPE HEADERS
        FILES
            elog_config_service_client.h
            elog_config_service_params.h
            elog_config_service_publisher.h
            elog_config_service_redis_params.h
            elog_config_service_redis_publisher.h
            elog_config_service_redis_reader.h)