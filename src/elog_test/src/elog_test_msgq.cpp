#include "elog_test_common.h"

#ifdef ELOG_ENABLE_KAFKA_MSGQ_CONNECTOR
bool testKafka() {
    ELOG_BEGIN_TEST();
    std::string serverAddr;
    getEnvVar("ELOG_KAFKA_SERVER", serverAddr);
    std::string cfg =
        std::string("msgq://kafka?kafka_bootstrap_servers=") + serverAddr +
        ":9092&"
        "msgq_topic=log_records&"
        "kafka_flush_timeout=5000millis&"
        "flush_policy=immediate&"
        "headers={rid=${rid}, time=${time}, level=${level}, host=${host}, user=${user}, "
        "prog=${prog}, pid = ${pid}, tid = ${tid}, tname = ${tname}, file = ${file}, "
        "line = ${line}, func = ${func}, mod = ${mod}, src = ${src}, msg = ${msg}}";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("Kafka", cfg.c_str(), msgPerf, ioPerf, TT_NORMAL, 10);
    ELOG_END_TEST();
}
TEST(ELogMsgQ, Kafka) {
    bool res = testKafka();
    EXPECT_EQ(res, true);
}
#endif