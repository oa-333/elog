
target_sources(
    elog
    PUBLIC
        FILE_SET publicheaders
        TYPE HEADERS
        FILES
            elog_db_formatter.h
            elog_db_schema_handler.h
            elog_db_target.h
            elog_db_target_provider.h
            elog_mysql_db_target.h
            elog_pgsql_db_target.h
            elog_sqlite_db_target.h)