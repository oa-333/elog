#include "elog_test_common.h"

class TestSelector : public elog::ELogFieldSelector {
public:
    TestSelector(const elog::ELogFieldSpec& fieldSpec)
        : elog::ELogFieldSelector(elog::ELogFieldType::FT_TEXT, fieldSpec) {}
    TestSelector(const TestSelector&) = delete;
    TestSelector(TestSelector&&) = delete;
    TestSelector& operator=(const TestSelector&) = delete;

    void selectField(const elog::ELogRecord& record, elog::ELogFieldReceptor* receptor) final {
        std::string fieldStr = "test-field";
        receptor->receiveStringField(getTypeId(), fieldStr.c_str(), getFieldSpec(),
                                     fieldStr.length());
    }

private:
    ELOG_DECLARE_FIELD_SELECTOR(TestSelector, test, ELOG_NO_EXPORT)
};

ELOG_IMPLEMENT_FIELD_SELECTOR(TestSelector)

// TODO: define a string log target so we can examine the resulting text

int testSelector() {
    const char* cfg = "sys://stderr?log_format=${time} ${level:6} [${tid}] <${test}> ${src} ${msg}";
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        return 1;
    }
    elog::ELogLogger* logger = elog::getPrivateLogger("elog_test_logger");
    ELOG_INFO_EX(logger, "This is a test message");
    termELog();
    return 0;
}

TEST(ELogExtend, ELogSelector) {
    int res = testSelector();
    EXPECT_EQ(res, 0);
}

class TestFilter final : public elog::ELogFilter {
public:
    TestFilter() {}
    TestFilter(const TestFilter&) = delete;
    TestFilter(TestFilter&&) = delete;
    TestFilter& operator=(const TestFilter&) = delete;

    /** @brief Loads filter from configuration. */
    bool load(const elog::ELogConfigMapNode* filterCfg) final { return true; }

    /** @brief Loads filter from a free-style predicate-like parsed expression. */
    bool loadExpr(const elog::ELogExpression* expr) final { return true; }

    /**
     * @brief Filters a log record.
     * @param logRecord The log record to filter.
     * @return true If the log record is to be logged.
     * @return false If the log record is to be discarded.
     */
    bool filterLogRecord(const elog::ELogRecord& logRecord) final {
        return logRecord.m_logRecordId % 2 == 0;
    }

private:
    ELOG_DECLARE_FILTER(TestFilter, test_filter, ELOG_NO_EXPORT)
};

ELOG_IMPLEMENT_FILTER(TestFilter)

int testFilter() {
    const char* cfg =
        "sys://stderr?log_format=${time} ${level:6} [${tid}] <${test}> ${src} ${msg}&"
        "filter=test_filter";
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        return 1;
    }
    elog::ELogLogger* logger = elog::getPrivateLogger("elog_test_logger");
    for (int i = 0; i < 10; ++i) {
        ELOG_INFO_EX(logger, "This is a test message %d", i);
    }
    termELog();
    return 0;
}

TEST(ELogExtend, ELogFilter) {
    int res = testFilter();
    EXPECT_EQ(res, 0);
}

/**
 * @class A flush policy that enforces log target flush whenever the number of un-flushed log
 * messages exceeds a configured limit.
 */
class TestFlushPolicy final : public elog::ELogFlushPolicy {
public:
    TestFlushPolicy() : m_counter(0) {}
    TestFlushPolicy(const TestFlushPolicy&) = delete;
    TestFlushPolicy(TestFlushPolicy&&) = delete;
    TestFlushPolicy& operator=(const TestFlushPolicy&) = delete;

    /** @brief Loads flush policy from configuration. */
    bool load(const elog::ELogConfigMapNode* flushPolicyCfg) final { return true; }

    /** @brief Loads flush policy from a free-style predicate-like parsed expression. */
    bool loadExpr(const elog::ELogExpression* expr) final { return true; }

    bool shouldFlush(uint64_t msgSizeBytes) final {
        if ((++m_counter) % 2 == 0) {
            fprintf(stderr, "Test flush PASS\n");
            return true;
        } else {
            fprintf(stderr, "Test flush NO-PASS\n");
            return false;
        }
    }

private:
    uint64_t m_counter;

    ELOG_DECLARE_FLUSH_POLICY(TestFlushPolicy, test_policy, ELOG_NO_EXPORT)
};

ELOG_IMPLEMENT_FLUSH_POLICY(TestFlushPolicy)

int testFlushPolicy() {
    const char* cfg =
        "sys://stderr?log_format=${time} ${level:6} [${tid}] <${test}> ${src} ${msg}&"
        "flush_policy=test_policy";
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        return 1;
    }
    elog::ELogLogger* logger = elog::getPrivateLogger("elog_test_logger");
    for (int i = 0; i < 10; ++i) {
        ELOG_INFO_EX(logger, "This is a test message %d", i);
    }
    termELog();
    return 0;
}

TEST(ELogExtend, ELogFlushPolicy) {
    int res = testFlushPolicy();
    EXPECT_EQ(res, 0);
}

// test formatter - prepends the message with "***"", and surrounds each field with "[]"
class TestFormatter : public elog::ELogFormatter {
public:
    TestFormatter() : ELogFormatter(TYPE_NAME), m_firstField(true) {}
    TestFormatter(const TestFormatter&) = delete;
    TestFormatter(TestFormatter&&) = delete;
    TestFormatter& operator=(const TestFormatter&) = delete;

    static constexpr const char* TYPE_NAME = "test";

protected:
    bool handleText(const std::string& text) override {
        if (m_firstField) {
            m_fieldSelectors.push_back(new (std::nothrow) elog::ELogStaticTextSelector("*** "));
            m_firstField = false;
        }
        m_fieldSelectors.push_back(new (std::nothrow) elog::ELogStaticTextSelector(text.c_str()));
        return true;
    }

    bool handleField(const elog::ELogFieldSpec& fieldSpec) override {
        if (m_firstField) {
            m_fieldSelectors.push_back(new (std::nothrow) elog::ELogStaticTextSelector("*** "));
            m_firstField = false;
        }
        m_fieldSelectors.push_back(new (std::nothrow) elog::ELogStaticTextSelector("["));
        bool res = ELogFormatter::handleField(fieldSpec);
        if (res == true) {
            m_fieldSelectors.push_back(new (std::nothrow) elog::ELogStaticTextSelector("]"));
        }
        return res;
    }

private:
    bool m_firstField;

    ELOG_DECLARE_LOG_FORMATTER(TestFormatter, test, ELOG_NO_EXPORT)
};

ELOG_IMPLEMENT_LOG_FORMATTER(TestFormatter)

int testLogFormatter() {
    const char* cfg = "sys://stderr?log_format=test:${time} ${level:6} ${tid} ${src} ${msg}";
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        return 1;
    }
    elog::ELogLogger* logger = elog::getPrivateLogger("elog_test_logger");
    ELOG_INFO_EX(logger, "This is a test message");
    termELog();
    return 0;
}

TEST(ELogExtend, ELogFormatter) {
    int res = testLogFormatter();
    EXPECT_EQ(res, 0);
}