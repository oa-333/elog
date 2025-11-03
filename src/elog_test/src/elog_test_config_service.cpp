#include "elog_test_common.h"

#ifdef ELOG_ENABLE_CONFIG_PUBLISH_REDIS
#include "cfg_srv/elog_config_service_redis_publisher.h"
#endif
#ifdef ELOG_ENABLE_CONFIG_PUBLISH_ETCD
#include "cfg_srv/elog_config_service_etcd_publisher.h"
#endif

#ifdef ELOG_ENABLE_CONFIG_SERVICE
static int testConfigService() {
    // baseline test - no filter used, direct life sign report
    fprintf(stderr, "Running basic config-service test\n");
    elog::ELogTarget* logTarget = initElog();
    if (logTarget == nullptr) {
        fprintf(stderr, "Failed to init config-service test, aborting\n");
        return 1;
    }
    fprintf(stderr, "initElog() OK\n");

    elog::ELogConfigServicePublisher* publisher = nullptr;
#ifdef ELOG_ENABLE_CONFIG_PUBLISH_REDIS
    elog::ELogConfigServiceRedisPublisher* redisPublisher =
        elog::ELogConfigServiceRedisPublisher::create();
    std::string redisServerList;
    getEnvVar("ELOG_REDIS_SERVERS", redisServerList);
    redisPublisher->setServerList(redisServerList);
    publisher = redisPublisher;
#endif
#ifdef ELOG_ENABLE_CONFIG_PUBLISH_ETCD
    elog::ELogConfigServiceEtcdPublisher* etcdPublisher =
        elog::ELogConfigServiceEtcdPublisher::create();
    std::string etcdServerList;
    getEnvVar("ELOG_ETCD_SERVERS", etcdServerList);
    fprintf(stderr, "etcd server at: %s", etcdServerList.c_str());
    etcdPublisher->setServerList(etcdServerList);
    std::string etcdApiVersion;
    getEnvVar("ELOG_ETCD_API_VERSION", etcdApiVersion);
    if (!etcdApiVersion.empty()) {
        elog::ELogEtcdApiVersion apiVersion;
        if (!elog::convertEtcdApiVersion(etcdApiVersion.c_str(), apiVersion)) {
            return 2;
        }
        etcdPublisher->setApiVersion(apiVersion);
    }
    publisher = etcdPublisher;
#endif
    if (publisher != nullptr) {
        if (!publisher->initialize()) {
            fprintf(stderr, "Failed to initialize redis publisher\n");
            return 2;
        }
        if (!elog::stopConfigService()) {
            fprintf(stderr, "Failed to stop configuration service\n");
            publisher->terminate();
            return 2;
        }
        elog::setConfigServiceDetails("subnet:192.168.1.0", 0);
        elog::setConfigServicePublisher(publisher);
        if (!elog::startConfigService()) {
            fprintf(stderr, "Failed to restart configuration service\n");
            elog::setConfigServicePublisher(nullptr);
            publisher->terminate();
            return 2;
        }
    }

    // just print every second with two loggers
    elog::ELogLogger* logger1 = elog::getPrivateLogger("test.logger1");
    elog::ELogLogger* logger2 = elog::getPrivateLogger("test.logger2");
    logger1->getLogSource()->setLogLevel(elog::ELEVEL_INFO, elog::ELogPropagateMode::PM_NONE);
    logger2->getLogSource()->setLogLevel(elog::ELEVEL_TRACE, elog::ELogPropagateMode::PM_NONE);

    volatile bool stopTest = false;
    std::thread t1 = std::thread([logger1, &stopTest]() {
        while (!stopTest) {
            ELOG_INFO_EX(logger1, "test message from logger 1");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    std::thread t2 = std::thread([logger2, &stopTest]() {
        while (!stopTest) {
            ELOG_TRACE_EX(logger2, "test message from logger 2");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    // TODO: how do we test this service?
    // we need to invoke the CLI and interrogate output
    // then change the log level of the test logger and verify level has changed (use test log
    // target for verifying)

    stopTest = true;
    t1.join();
    t2.join();

    termELog();
    // NOTE: we can remove publisher early, or let ELog destroy it during shutdown
    // we leave this comment  for future tests
    // must remove publisher before going out of scope
    /*if (publisher != nullptr) {
        if (!elog::setConfigServicePublisher(nullptr, true)) {
            fprintf(stderr, "Failed to remove publisher\n");
        }
        publisher->terminate();
        elog::destroyConfigServicePublisher(publisher);
    }*/
    return 0;
}

TEST(ELogCore, ConfigService) {
    int res = testConfigService();
    EXPECT_EQ(res, 0);
}
#endif