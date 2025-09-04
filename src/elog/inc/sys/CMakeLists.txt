
target_sources(
    elog
    PUBLIC
        FILE_SET publicheaders
        TYPE HEADERS
        FILES
            elog_syslog_target.h
            elog_win32_event_log_target.h)