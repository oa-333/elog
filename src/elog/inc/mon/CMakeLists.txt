
target_sources(
    elog
    PUBLIC
        FILE_SET publicheaders
        TYPE HEADERS
        FILES
            elog_datadog_target.h
            elog_grafana_json_target.h
            elog_grafana_target.h
            elog_mon_schema_handler.h
            elog_mon_target_provider.h
            elog_mon_target.h
            elog_otel_target.h
            elog_sentry_target.h)