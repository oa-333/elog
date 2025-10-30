#include "elog_test_common.h"

#ifdef ELOG_ENABLE_GRAFANA_CONNECTOR
bool testGrafana() {
    ELOG_BEGIN_TEST();
    std::string serverAddr;
    getEnvVar("ELOG_GRAFANA_SERVER", serverAddr);
    fprintf(stderr, "ELOG_GRAFANA_SERVER=%s\n", serverAddr.c_str());
    std::string cfg =
        std::string("mon://grafana?mode=json&loki_address=http://") + serverAddr +
        ":3100&labels={app: "
        "test}&flush_policy=count&flush_count=10&connect_timeout=5000ms&read_timeout=5000ms";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    fprintf(stderr, "Grafana cfg: %s\n", cfg.c_str());
    runSingleThreadedTest("Grafana-Loki", cfg.c_str(), msgPerf, ioPerf, TT_NORMAL, 100);
    ELOG_END_TEST();
}
TEST(ELogMon, Grafana) {
    bool res = testGrafana();
    EXPECT_EQ(res, true);
}
#endif

#ifdef ELOG_ENABLE_SENTRY_CONNECTOR
bool testSentry() {
    ELOG_BEGIN_TEST();
    // test script sets this up
    std::string buildPath;
    getEnvVar("ELOG_BUILD_PATH", buildPath);

    std::string cfg = std::string(
                          "mon://sentry?"
                          "db_path=.sentry-native&"
                          "release=native@1.0&"
                          "env=staging&"
                          "handler_path=") +
                      buildPath +
                      "\\vcpkg_installed\\x64-windows\\tools\\sentry-native\\crashpad_handler.exe&"
                      "flush_policy=immediate&"
                      "debug=true&"
                      "logger_level=INFO&"
                      "tags={log_source=${src}, module=${mod}, file=${file}, line=${line}}&"
                      "stack_trace=yes&"
                      "context={app=${app}, os=${os_name}, ver=${os_ver}}&"
                      "context_title=Env Details";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("Sentry", cfg.c_str(), msgPerf, ioPerf, TT_NORMAL, 10);
    ELOG_END_TEST();
}
TEST(ELogMon, Sentry) {
    bool res = testSentry();
    EXPECT_EQ(res, true);
}
#endif

#ifdef ELOG_ENABLE_DATADOG_CONNECTOR
bool testDatadog() {
    // test currently disabled on linux due to crash in open ssl when calling SSL_CTX_new()
#ifdef ELOG_LINUX
    return true;
#else
    ELOG_BEGIN_TEST();
    char* datadogServer = getenv("ELOG_DATADOG_SERVER");
    if (datadogServer == nullptr) {
        fprintf(stderr, "Missing datadog Server\n");
        return false;
    }
    char* api_key = getenv("ELOG_DATADOG_API_KEY");
    if (api_key == nullptr) {
        fprintf(stderr, "Missing datadog API Key\n");
        return false;
    }
    fprintf(stderr, "ELOG_DATADOG_SERVER=%s\n", datadogServer);
    std::string cfg = "mon://datadog?address=" + std::string(datadogServer) + "&" +
                      "api_key=" + std::string(api_key) + "&" +
                      "source=elog&"
                      "service=elog_test&"
                      "flush_policy=count&"
                      "flush_count=5&"
                      "tags={log_source=${src}, module=${mod}, file=${file}, line=${line}}&"
                      "stack_trace=yes&"
                      "compress=yes&"
                      "connect_timeout=5000ms&"
                      "read_timeout=5000ms";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("Datadog", cfg.c_str(), msgPerf, ioPerf, TT_NORMAL, 10);
    ELOG_END_TEST();
#endif
}
TEST(ELogMon, Datadog) {
    bool res = testDatadog();
    EXPECT_EQ(res, true);
}
#endif

#ifdef ELOG_ENABLE_OTEL_CONNECTOR
bool testOtel() {
    ELOG_BEGIN_TEST();
    std::string serverAddr;
    getEnvVar("ELOG_OTEL_SERVER", serverAddr);
    std::string cfg =
        "mon://"
        "otel?method=http&endpoint=" +
        serverAddr +
        ":4318&debug=true&batching=yes&batch_export_"
        "size=25&"
        "log_format=msg:${rid}, ${time}, ${src}, ${mod}, ${tid}, ${pid}, ${file}, ${line}, "
        "${level}, ${msg}&"
        "flush_policy=count&flush_count=10";
    double msgPerf = 0.0f;
    double ioPerf = 0.0f;
    runSingleThreadedTest("Open-Telemetry", cfg.c_str(), msgPerf, ioPerf, TT_NORMAL, 10);
    ELOG_END_TEST();

    // NOTE: grpc method works, but it cannot be run after http (process stuck with some lock)
    // so for unit tests we must do two separate runs
    /*cfg =
        "mon://otel?method=grpc&endpoint=192.168.1.163:4317&debug=true&"
        "log_format=msg:${rid}, ${time}, ${src}, ${mod}, ${tid}, ${pid}, ${file}, ${line}, "
        "${level}, ${msg}";
    runSingleThreadedTest("Open-Telemetry", cfg.c_str(), msgPerf, ioPerf, statData, 10);*/

    // TODO: regression test will launch a local otel collector and have it write records to
    // file then we can parse the log file and verify all records and attributes are there
}
TEST(ELogMon, Otel) {
    bool res = testOtel();
    EXPECT_EQ(res, true);
}
#endif