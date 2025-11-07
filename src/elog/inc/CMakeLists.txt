
target_sources(
    elog
    PUBLIC
        FILE_SET publicheaders
        TYPE HEADERS
        FILES
            elog_aligned_alloc.h
            elog_api.h
            elog_atomic.h
            elog_buffer_receptor.h
            elog_buffer.h
            elog_common_def.h
            elog_concurrent_ring_buffer.h
            elog_config.h
            elog_def.h
            elog_expression.h
            elog_field_receptor.h
            elog_field_selector.h
            elog_field_spec.h
            elog_filter.h
            elog_flush_policy.h
            elog_fmt_lib.h
            elog_formatter.h
            elog_gc.h
            elog_gzip.h
            elog_http_client.h
            elog_json_formatter.h
            elog_json_receptor.h
            elog_level.h
            elog_life_sign_filter.h
            elog_life_sign_params.h
            elog_logger.h
            elog_managed_object.h
            elog_params.h
            elog_private_logger.h
            elog_props_formatter.h
            elog_props.h
            elog_proto.h
            elog_rate_limiter.h
            elog_read_buffer.h
            elog_record.h
            elog_redis_client.h
            elog_report_handler.h
            elog_rolling_bitset.h
            elog_record_builder.h
            elog_schema_handler.h
            elog_shared_logger.h
            elog_source.h
            elog_stats.h
            elog_string_stream_receptor.h
            elog_string_receptor.h
            elog_target_provider.h
            elog_target_spec.h
            elog_target.h
            elog_time.h
            elog_tls.h
            elog_type_codec.h)
            
# core sub-directories
add_subdirectory(async)
add_subdirectory(file)
add_subdirectory(sys)

# optional sub-directories
if (ELOG_ENABLE_DB)
    add_subdirectory(db)
endif()
if (ELOG_ENABLE_MSGQ)
    add_subdirectory(msgq)
endif()
if (ELOG_ENABLE_RPC)
    add_subdirectory(rpc)
endif()
if (ELOG_ENABLE_MON)
    add_subdirectory(mon)
endif()
if (ELOG_ENABLE_MSG)
    add_subdirectory(msg)
endif()
if (ELOG_ENABLE_NET)
    add_subdirectory(net)
endif()
if (ELOG_ENABLE_IPC)
    add_subdirectory(ipc)
endif()

# other extensions
if (ELOG_ENABLE_CONFIG_SERVICE)
    add_subdirectory(cfg_srv)
endif()