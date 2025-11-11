#include "elog_test_common.h"

// TODO: how can this test be automated?
int testColors() {
    const char* cfg =
        "sys://stderr?log_format=${time:font=faint} ${level:6:fg-color=green:bg-color=blue} "
        "[${tid:font=italic}] ${src:font=underline:fg-color=bright-red} "
        "${msg:font=cross-out,blink-rapid:fg-color=#993983}"
        "${fmt:default}";
    elog::ELogTarget* logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        return 1;
    }
    elog::ELogLogger* logger = elog::getPrivateLogger("elog_test_logger");
    ELOG_INFO_EX(logger, "This is a test message");
    termELog();

    cfg =
        "sys://stderr?log_format=${time:font=faint} "
        "${if: (log_level == INFO): ${fmt:begin-fg-color=green}: ${fmt:begin-fg-color=red}}"
        "${level:6}${fmt:default} "
        "[${tid:font=italic}] ${src:font=underline:fg-color=bright-red} "
        "${msg:font=cross-out,blink-rapid:fg-color=#993983}"
        "${fmt:default}";
    logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        return 2;
    }
    logger = elog::getPrivateLogger("elog_test_logger");
    ELOG_INFO_EX(logger, "This is a test message");
    ELOG_WARN_EX(logger, "This is a test message");
    termELog();

    cfg =
        "sys://stderr?log_format=${time:font=faint} "
        "${switch: ${level}:"
        "   ${case: ${const-level: INFO}: ${fmt:begin-fg-color=green}} :"
        "   ${case: ${const-level: WARN}: ${fmt:begin-fg-color=red}} :"
        "   ${case: ${const-level: ERROR}: ${fmt:begin-fg-color=magenta}} :"
        "   ${default: ${fmt:begin-fg-color=yellow}}}"
        "${level:6}${fmt:default} "
        "[${tid:font=italic}] ${src:font=underline:fg-color=bright-red} "
        "${msg:font=cross-out,blink-rapid:fg-color=#993983}"
        "${fmt:default}";
    logTarget = initElog(cfg);
    if (logTarget == nullptr) {
        return 3;
    }
    logger = elog::getPrivateLogger("elog_test_logger");
    ELOG_INFO_EX(logger, "This is a test message");
    ELOG_WARN_EX(logger, "This is a test message");
    ELOG_ERROR_EX(logger, "This is a test message");
    ELOG_NOTICE_EX(logger, "This is a test message");
    termELog();

    cfg =
        "sys://stderr?log_format=${time:font=faint} "
        "${expr-switch: "
        "   ${case: (log_level == INFO): ${fmt:begin-fg-color=green}} :"
        "   ${case: (log_level == WARN): ${fmt:begin-fg-color=red}} :"
        "   ${case: (log_level == ERROR): ${fmt:begin-fg-color=magenta}} :"
        "   ${default: ${fmt:begin-fg-color=yellow}}}"
        "${level:6}${fmt:default} "
        "[${tid:font=italic}] ${src:font=underline:fg-color=bright-red} "
        "${msg:font=cross-out,blink-rapid:fg-color=#993983}"
        "${fmt:default}";
    logTarget = initElog(cfg);
    logger = elog::getPrivateLogger("elog_test_logger");
    ELOG_INFO_EX(logger, "This is a test message");
    ELOG_WARN_EX(logger, "This is a test message");
    ELOG_ERROR_EX(logger, "This is a test message");
    ELOG_NOTICE_EX(logger, "This is a test message");
    termELog();
    return 0;
}

TEST(ELogCore, TestColors) {
    int res = testColors();
    EXPECT_EQ(res, 0);
}