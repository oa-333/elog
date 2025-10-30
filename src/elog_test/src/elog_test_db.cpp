#include "elog_test_common.h"

#ifdef ELOG_ENABLE_MYSQL_DB_CONNECTOR
bool testMySQL() {
    ELOG_BEGIN_TEST();
#if 0
    const char* cfg =
        "db://mysql?conn_string=tcp://127.0.0.1&db=test&user=root&passwd=root&"
        "insert_query=INSERT INTO log_records VALUES(${rid}, ${time}, ${level}, ${host}, "
        "${user},"
        "${prog}, ${pid}, ${tid}, ${mod}, ${src}, ${msg})&"
        "db_thread_model=conn-per-thread";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("MySQL", cfg, msgPerf, ioPerf, TT_NORMAL, 10);
#endif
    ELOG_END_TEST();
}
TEST(ELogDb, MySQL) {
    bool res = testMySQL();
    EXPECT_EQ(res, true);
}
#endif

#ifdef ELOG_ENABLE_SQLITE_DB_CONNECTOR
bool testSQLite() {
    ELOG_BEGIN_TEST();
    const char* cfg =
        "db://sqlite?conn_string=test.db&"
        "insert_query=INSERT INTO log_records VALUES(${rid}, ${time}, ${level}, ${host}, "
        "${user},"
        "${prog}, ${pid}, ${tid}, ${mod}, ${src}, ${msg})&"
        "db_thread_model=conn-per-thread";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("PostgreSQL", cfg, msgPerf, ioPerf, TT_NORMAL, 10);
    ELOG_END_TEST();
}
TEST(ELogDb, SQLite) {
    bool res = testSQLite();
    EXPECT_EQ(res, true);
}
#endif

#ifdef ELOG_ENABLE_PGSQL_DB_CONNECTOR
bool testPostgreSQL() {
    ELOG_BEGIN_TEST();
    std::string serverAddr;
    getEnvVar("ELOG_PGSQL_SERVER", serverAddr);
    std::string cfg = std::string("db://postgresql?conn_string=") + serverAddr +
                      "&port=5432&db=mydb&user=oren&passwd=\"1234\"&"
                      "insert_query=INSERT INTO log_records VALUES(${rid}, ${time}, ${level}, "
                      "${host}, ${user},"
                      "${prog}, ${pid}, ${tid}, ${mod}, ${src}, ${msg})&"
                      "db_thread_model=conn-per-thread";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("PostgreSQL", cfg.c_str(), msgPerf, ioPerf, TT_NORMAL, 10);
    ELOG_END_TEST();
}
TEST(ELogDb, PostgreSQL) {
    bool res = testPostgreSQL();
    EXPECT_EQ(res, true);
}
#endif

#ifdef ELOG_ENABLE_REDIS_DB_CONNECTOR
bool testRedis() {
    ELOG_BEGIN_TEST();
    std::string serverAddr;
    getEnvVar("ELOG_REDIS_SERVER", serverAddr);
    std::string cfg =
        std::string("db://redis?conn_string=") + serverAddr +
        ":6379&passwd=\"1234\"&"
        "insert_query=HSET log_records:${rid} time \"${time}\" level \"${level}\" "
        "host \"${host}\" user \"${user}\" prog \"${prog}\" pid \"${pid}\" tid \"${tid}\" "
        "mod \"${mod}\" src \"${src}\" msg \"${msg}\"&"
        "index_insert=SADD log_records_all ${rid};ZADD log_records_by_time ${time_epoch} "
        "${rid}&"
        "db_thread_model=conn-per-thread";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("Redis", cfg.c_str(), msgPerf, ioPerf, TT_NORMAL, 10);
    ELOG_END_TEST();
}
TEST(ELogDb, Redis) {
    bool res = testRedis();
    EXPECT_EQ(res, true);
}
#endif
