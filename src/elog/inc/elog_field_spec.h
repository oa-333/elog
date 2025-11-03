#ifndef __ELOG_FIELD_SPEC_H__
#define __ELOG_FIELD_SPEC_H__

#include <cinttypes>
#include <string>

#include "elog_common_def.h"
#include "elog_def.h"

namespace elog {

/** @brief Justify mode constants. */
enum class ELogJustifyMode : uint32_t {
    /** @var No justification used. */
    JM_NONE,

    /** @var Justify to the left, padding on the right. */
    JM_LEFT,

    /** @var Justify to the right, padding on the left. */
    JM_RIGHT
};

/** @struct Text justification specification. */
struct ELOG_API ELogJustifySpec {
    /** @brief Justify mode. */
    ELogJustifyMode m_mode;

    /** @brief Absolute justify value. */
    uint32_t m_justify;
};

/** @brief Font specification type. */
// typedef uint8_t ELogFontSpec;
struct ELOG_API ELogFontSpec {
    enum class BoldSpec : uint8_t {
        ELOG_BOLD_NONE,
        ELOG_FONT_BOLD,
        ELOG_FONT_FAINT,
        ELOG_FONT_NORMAL
    } m_boldSpec;

    enum class ItalicSpec : uint8_t {
        ELOG_ITALIC_NONE,
        ELOG_ITALIC_SET,
        ELOG_ITALIC_RESET
    } m_italicSpec;

    enum class UnderlineSpec : uint8_t {
        ELOG_UNDERLINE_NONE,
        ELOG_UNDERLINE_SET,
        ELOG_UNDERLINE_RESET
    } m_underline;

    enum class CrossOutSpec : uint8_t {
        ELOG_CROSSOUT_NONE,
        ELOG_CROSSOUT_SET,
        ELOG_CROSSOUT_RESET
    } m_crossOut;  // strike-through

    enum class BlinkSpec : uint8_t {
        ELOG_BLINK_NONE,
        ELOG_BLINK_SET_SLOW,
        ELOG_BLINK_SET_RAPID,
        ELOG_BLINK_RESET
    } m_blinkSpec;

    uint8_t m_padding[3];

    ELogFontSpec()
        : m_boldSpec(BoldSpec::ELOG_BOLD_NONE),
          m_italicSpec(ItalicSpec::ELOG_ITALIC_NONE),
          m_underline(UnderlineSpec::ELOG_UNDERLINE_NONE),
          m_crossOut(CrossOutSpec::ELOG_CROSSOUT_NONE),
          m_blinkSpec(BlinkSpec::ELOG_BLINK_NONE) {}
};

// predefined simple colors
enum ELogColor : uint8_t {
    ELOG_COLOR_NONE,
    ELOG_COLOR_BLACK,
    ELOG_COLOR_RED,
    ELOG_COLOR_GREEN,
    ELOG_COLOR_YELLOW,
    ELOG_COLOR_BLUE,
    ELOG_COLOR_MAGENTA,
    ELOG_COLOR_CYAN,
    ELOG_COLOR_WHITE,
    ELOG_COLOR_RESET,
    ELOG_COLOR_DEFAULT = ELOG_COLOR_RESET
};

/** @struct Simple color specification. */
struct ELOG_API ELogSimpleColorSpec {
    ELogColor m_color;
    // set this to non-zero to specify bright color
    uint8_t m_bright;

    ELogSimpleColorSpec() : m_color(ELOG_COLOR_NONE), m_bright(0) {}
};

/** @brief RGB Color specification. */
struct ELOG_API ELogRGBColorSpec {
    uint8_t m_red;
    uint8_t m_green;
    uint8_t m_blue;
    /** @brief Specifies whether restricted 216 color pallette is used. */
    uint8_t m_isVGAColor;

    ELogRGBColorSpec() : m_red(0), m_green(0), m_blue(0), m_isVGAColor(0) {}
};

struct ELOG_API ELogColorSpec {
    // specify which of the members is in use
    enum SpecType : uint32_t {
        COLOR_SPEC_NONE,
        COLOR_SPEC_SIMPLE,
        COLOR_SPEC_RGB,
        COLOR_SPEC_RGB_VGA,
        COLOR_SPEC_GREY
    } m_colorSpecType;

    ELogSimpleColorSpec m_simpleSpec;
    ELogRGBColorSpec m_rgbSpec;
    uint8_t m_greyScale;

    ELogColorSpec() : m_colorSpecType(COLOR_SPEC_NONE) {}
};

/** @struct Terminal Text specification. */
struct ELOG_API ELogTextSpec {
    // set this flag to non-zero in order to reset all previous formatting
    /** @brief Foreground text color specification. */
    ELogColorSpec m_fgColorSpec;  // 8 words

    /** @brief Background text color specification. */
    ELogColorSpec m_bgColorSpec;  // 8 words

    /** @brief Font specification. */
    ELogFontSpec m_fontSpec;  // 1 word

    /** @brief Flag for resetting all previous color and font settings. */
    uint8_t m_resetTextSpec;

    /**
     * @brief Specifies whether to return to normal specification after applying field text
     * formatting (by default: true).
     */
    uint8_t m_autoReset;

    /** @brief align next member to 8 bytes. */
    uint8_t m_padding[6];

    /** @brief The actual ANSI C resolved specification escape codes. */
    std::string m_resolvedSpec;

    /** @brief Reset all test formatting specification. */
    static const char* m_resetSpec;

    ELogTextSpec() : m_resetTextSpec(0), m_autoReset(1) {}
    ELogTextSpec(const ELogTextSpec&) = default;
    ELogTextSpec(ELogTextSpec&&) = default;
    ELogTextSpec& operator=(const ELogTextSpec&) = default;
    ~ELogTextSpec() {}

    /** @brief Resolves once the specification escape codes. */
    void resolve();
};

/** @brief Time clock types. */
enum class ELogTimeClock {
    /** @brief Realtime clock. */
    TC_REALTIME_CLOCK,

    /** @brief Monotonic clock. */
    TC_MONOTONIC_CLOCK
};

/** @brief Time provider types. */
enum class ELogTimeProvider {
    /** @brief std::chrono time provider. */
    TP_STD_CHRONO,

    /** @brief Direct operating system time provider. */
    TP_OS
};

/** @struct Time source and format specification. */
struct ELOG_API ELogTimeSpec {
    /** @brief The clock being used to retrieve the current time (currently not in use). */
    ELogTimeClock m_timeClock;

    /** @brief The time provider being used to retrieve the current time (currently not in use). */
    ELogTimeProvider m_timeProvider;

    /** @brief Specifies whether to display local time. */
    bool m_useLocalTime;

    /** @brief Specifies whether to display in seconds, milli, micro or nano-seconds. */
    ELogTimeUnits m_timeUnits;

    /** @brief Specifies whether to display also time zone. */
    bool m_useTimeZone;

    /** @brief A time format string, overrides zone settings. */
    std::string m_timeFormat;

    ELogTimeSpec()
        : m_timeClock(ELogTimeClock::TC_REALTIME_CLOCK),
          m_timeProvider(ELogTimeProvider::TP_OS),
          m_useLocalTime(true),
          m_timeUnits(ELogTimeUnits::TU_MILLI_SECONDS),
          m_useTimeZone(false) {}
    ELogTimeSpec(const ELogTimeSpec&) = default;
    ELogTimeSpec(ELogTimeSpec&&) = default;
    ELogTimeSpec& operator=(const ELogTimeSpec&) = default;
    ~ELogTimeSpec() {}
};

/** @brief Log record field reference specification. */
struct ELOG_API ELogFieldSpec {
    /** @var The special field name (reference token). */
    std::string m_name;

    /** @var Justification specification. */
    ELogJustifySpec m_justifySpec;

    /** @var Text (font/color) specification. */
    ELogTextSpec* m_textSpec;

    /** @var Time (source/format) specification. */
    ELogTimeSpec* m_timeSpec;

    /** @brief Simple/default constructor */
    ELogFieldSpec(const char* name = "", ELogJustifyMode justifyMode = ELogJustifyMode::JM_NONE,
                  uint32_t justify = 0)
        : m_name(name),
          m_justifySpec({justifyMode, justify}),
          m_textSpec(nullptr),
          m_timeSpec(nullptr) {}

    ELogFieldSpec(const ELogFieldSpec& fieldSpec)
        : m_name(fieldSpec.m_name),
          m_justifySpec(fieldSpec.m_justifySpec),
          m_textSpec(nullptr),
          m_timeSpec(nullptr) {
        if (fieldSpec.m_textSpec != nullptr) {
            m_textSpec = new (std::nothrow) ELogTextSpec(*fieldSpec.m_textSpec);
        }
        if (fieldSpec.m_timeSpec != nullptr) {
            m_timeSpec = new (std::nothrow) ELogTimeSpec(*fieldSpec.m_timeSpec);
        }
    }

    ELogFieldSpec(ELogFieldSpec&& fieldSpec)
        : m_name(fieldSpec.m_name),
          m_justifySpec(fieldSpec.m_justifySpec),
          m_textSpec(fieldSpec.m_textSpec),
          m_timeSpec(fieldSpec.m_timeSpec) {
        fieldSpec.m_textSpec = nullptr;
        fieldSpec.m_timeSpec = nullptr;
    }

    ~ELogFieldSpec() {
        if (m_textSpec != nullptr) {
            delete m_textSpec;
            m_textSpec = nullptr;
        }
        if (m_timeSpec != nullptr) {
            delete m_timeSpec;
            m_timeSpec = nullptr;
        }
    }

    ELogFieldSpec& operator=(const ELogFieldSpec&) = delete;

    bool parse(const std::string& fieldSpecStr);
};

}  // namespace elog

#endif  // __ELOG_FIELD_SPEC_H__