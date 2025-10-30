#include "elog_test_common.h"

#ifdef ELOG_ENABLE_FMT_LIB
struct Coord {
    int x;
    int y;
};

template <>
struct fmt::formatter<Coord> : formatter<std::string_view> {
    // parse is inherited from formatter<string_view>.

    auto format(Coord c, format_context& ctx) const -> format_context::iterator {
        std::string s = "{";
        s += std::to_string(c.x);
        s += ",";
        s += std::to_string(c.y);
        s += "}";
        return formatter<string_view>::format(s, ctx);
    }
};

#define COORD_CODE_ID ELOG_UDT_CODE_BASE

ELOG_DECLARE_TYPE_ENCODE_DECODE_EX(Coord, COORD_CODE_ID)

ELOG_BEGIN_IMPLEMENT_TYPE_ENCODE_EX(Coord) {
    if (!buffer.appendData(value.x)) {
        return false;
    }
    if (!buffer.appendData(value.y)) {
        return false;
    }
    return true;
}
ELOG_END_IMPLEMENT_TYPE_ENCODE_EX()

ELOG_IMPLEMENT_TYPE_DECODE_EX(Coord) {
    Coord c = {};
    if (!readBuffer.read(c.x)) {
        return false;
    }
    if (!readBuffer.read(c.y)) {
        return false;
    }
    store.push_back(c);
    return true;
}
#endif

#ifdef ELOG_ENABLE_FMT_LIB
TEST(ELogMisc, FmtLib) {
    // use string log target with format line containing only ${msg} so we can inspect
    // output and compare all will be printed to default log target (stderr)
    TestLogTarget* logTarget = new (std::nothrow) TestLogTarget();
    logTarget->setLogFormat("${msg}");
    elog::addLogTarget(logTarget);

    const auto& logMessages = logTarget->getLogMessages();

    int someInt = 5;
    ELOG_FMT_INFO("This is a test message for fmtlib: {}", someInt);
    EXPECT_EQ(logMessages.empty(), false);
    EXPECT_EQ(logMessages.back().compare("This is a test message for fmtlib: 5"), 0);

    ++someInt;
    ELOG_BIN_INFO("This is a test binary message, with int {}, bool {} and string {}", someInt,
                  true, "test string param");
    EXPECT_EQ(
        logMessages.back().compare(
            "This is a test binary message, with int 6, bool true and string test string param"),
        0);

    ++someInt;
    ELOG_CACHE_INFO("This is a test binary auto-cached message, with int {}, bool {} and string {}",
                    someInt, true, "test string param");
    EXPECT_EQ(logMessages.back().compare("This is a test binary auto-cached message, with int 7, "
                                         "bool true and string test string param"),
              0);

    ++someInt;
    elog::ELogCacheEntryId msgId = elog::getOrCacheFormatMsg(
        "This is a test binary pre-cached message, with int {}, bool {} and string {}");
    ELOG_ID_INFO(msgId, someInt, true, "test string param");
    EXPECT_EQ(logMessages.back().compare("This is a test binary pre-cached message, with int 8, "
                                         "bool true and string test string param"),
              0);

    // UDT test
    Coord c = {5, 7};
    ELOG_BIN_INFO("This is a test binary message, with UDT coord {}", c);
    EXPECT_EQ(logMessages.back().compare("This is a test binary message, with UDT coord {5,7}"), 0);

    elog::removeLogTarget(logTarget);
}
#endif