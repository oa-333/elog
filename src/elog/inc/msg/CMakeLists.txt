
target_sources(
    elog
    PUBLIC
        FILE_SET publicheaders
        TYPE HEADERS
        FILES
            elog_binary_format_provider.h
            elog_msg_formatter.h
            elog_msg_server.h
            elog_msg_stats.h
            elog_msg_target.h
            elog_msg.h
            elog_proto_receptor.h)