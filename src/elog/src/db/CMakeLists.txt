target_sources(elog PRIVATE
    elog_db_formatter.cpp
    elog_db_schema_handler.cpp
    elog_db_target.cpp
    elog_mysql_db_target.cpp
    elog_mysql_db_target_provider.cpp
    elog_pgsql_db_target_provider.cpp
    elog_pgsql_db_target.cpp
    elog_redis_db_target.cpp
    elog_redis_db_target_provider.cpp
    elog_sqlite_db_target.cpp
    elog_sqlite_db_target_provider.cpp)