target_sources(elog PRIVATE
    elog_api_config.cpp
    elog_api_life_sign.cpp
    elog_api_log_source.cpp
    elog_api_log_target.cpp
    elog_api_reload_config.cpp
    elog_api_time_source.cpp
    elog_buffer_receptor.cpp
    elog_buffer.cpp
    elog_cache.cpp
    elog_comm_util_log_handler.cpp
    elog_common.cpp
    elog_config_loader.cpp
    elog_config_parser.cpp
    elog_config.cpp
    elog_dbg_util_log_handler.cpp
    elog_expression_parser.cpp
    elog_expression_tokenizer.cpp
    elog_field_selector.cpp
    elog_field_spec.cpp
    elog_filter.cpp
    elog_flush_policy.cpp
    elog_formatter.cpp
    elog_gc.cpp
    elog_http_client.cpp
    elog_http_config_loader.cpp
    elog_json_formatter.cpp
    elog_json_receptor.cpp
    elog_level.cpp
    elog_life_sign_filter.cpp
    elog_logger.cpp
    elog_pre_init_logger.cpp
    elog_private_logger.cpp
    elog_props_formatter.cpp
    elog_rate_limiter.cpp
    elog_record.cpp
    elog_redis_client.cpp
    elog_report.cpp
    elog_report.h
    elog_rolling_bitset.cpp
    elog_schema_manager.cpp
    elog_shared_logger.cpp
    elog_source.cpp
    elog_string_tokenizer.cpp
    elog_stack_trace.cpp
    elog_stats.cpp
    elog_string_stream_receptor.cpp
    elog_string_receptor.cpp
    elog_target.cpp
    elog_time_source.cpp
    elog_time.cpp
    elog_tls.cpp
    elog_type_codec.cpp
    elog_win32_dll_event.cpp
    elog.cpp)
            
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