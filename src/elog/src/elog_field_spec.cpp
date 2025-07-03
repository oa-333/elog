#include "elog_field_spec.h"

#include "elog_common.h"
#include "elog_error.h"
#include "elog_font_color.h"

namespace elog {

struct NameColorEntry {
    const char* m_colorName;
    ELogColor m_color;
};

static NameColorEntry sColorTable[] = {{"black", ELOG_COLOR_BLACK}, {"red", ELOG_COLOR_RED},
                                       {"green", ELOG_COLOR_GREEN}, {"yellow", ELOG_COLOR_YELLOW},
                                       {"blue", ELOG_COLOR_BLUE},   {"magenta", ELOG_COLOR_MAGENTA},
                                       {"cyan", ELOG_COLOR_CYAN},   {"white", ELOG_COLOR_WHITE},
                                       {"reset", ELOG_COLOR_RESET}, {"default", ELOG_COLOR_RESET}};

static const size_t sColorCount = sizeof(sColorTable) / sizeof(sColorTable[0]);

struct ColorSpecEntry {
    ELogColor m_color;
    const char* m_spec;
};

#define FG_COLOR_ENTRY(COLOR) {ELOG_COLOR_##COLOR, ELOG_TT_FG_##COLOR}
#define FG_BRIGHT_COLOR_ENTRY(COLOR) {ELOG_COLOR_##COLOR, ELOG_TT_FG_BRIGHT_##COLOR}

#define BG_COLOR_ENTRY(COLOR) {ELOG_COLOR_##COLOR, ELOG_TT_BG_##COLOR}
#define BG_BRIGHT_COLOR_ENTRY(COLOR) {ELOG_COLOR_##COLOR, ELOG_TT_BG_BRIGHT_##COLOR}

static const ColorSpecEntry sFgColorTable[] = {
    FG_COLOR_ENTRY(BLACK), FG_COLOR_ENTRY(RED),     FG_COLOR_ENTRY(GREEN), FG_COLOR_ENTRY(YELLOW),
    FG_COLOR_ENTRY(BLUE),  FG_COLOR_ENTRY(MAGENTA), FG_COLOR_ENTRY(CYAN),  FG_COLOR_ENTRY(WHITE)};

static const size_t sFgColorCount = sizeof(sFgColorTable) / sizeof(sFgColorTable[0]);

static const ColorSpecEntry sFgBrightColorTable[] = {
    FG_BRIGHT_COLOR_ENTRY(BLACK),  FG_BRIGHT_COLOR_ENTRY(RED),  FG_BRIGHT_COLOR_ENTRY(GREEN),
    FG_BRIGHT_COLOR_ENTRY(YELLOW), FG_BRIGHT_COLOR_ENTRY(BLUE), FG_BRIGHT_COLOR_ENTRY(MAGENTA),
    FG_BRIGHT_COLOR_ENTRY(CYAN),   FG_BRIGHT_COLOR_ENTRY(WHITE)};

static const size_t sFgBrightColorCount =
    sizeof(sFgBrightColorTable) / sizeof(sFgBrightColorTable[0]);

static const ColorSpecEntry sBgColorTable[] = {
    BG_COLOR_ENTRY(BLACK), BG_COLOR_ENTRY(RED),     BG_COLOR_ENTRY(GREEN), BG_COLOR_ENTRY(YELLOW),
    BG_COLOR_ENTRY(BLUE),  BG_COLOR_ENTRY(MAGENTA), BG_COLOR_ENTRY(CYAN),  BG_COLOR_ENTRY(WHITE)};

static const size_t sBgColorCount = sizeof(sBgColorTable) / sizeof(sBgColorTable[0]);

static const ColorSpecEntry sBgBrightColorTable[] = {
    BG_BRIGHT_COLOR_ENTRY(BLACK),  BG_BRIGHT_COLOR_ENTRY(RED),  BG_BRIGHT_COLOR_ENTRY(GREEN),
    BG_BRIGHT_COLOR_ENTRY(YELLOW), BG_BRIGHT_COLOR_ENTRY(BLUE), BG_BRIGHT_COLOR_ENTRY(MAGENTA),
    BG_BRIGHT_COLOR_ENTRY(CYAN),   BG_BRIGHT_COLOR_ENTRY(WHITE)};

static const size_t sBgBrightColorCount =
    sizeof(sBgBrightColorTable) / sizeof(sBgBrightColorTable[0]);

static void applyColorFromTable(std::string& spec, ELogColor color,
                                const ColorSpecEntry* colorEntryTable, size_t colorCount) {
    for (size_t i = 0; i < colorCount; ++i) {
        if (color == colorEntryTable[i].m_color) {
            spec.append(colorEntryTable[i].m_spec);
            break;
        }
    }
}

inline void applyFgBrightColor(std::string& spec, ELogColor color) {
    applyColorFromTable(spec, color, sFgBrightColorTable, sFgBrightColorCount);
}

inline void applyFgColor(std::string& spec, ELogColor color) {
    applyColorFromTable(spec, color, sFgColorTable, sFgColorCount);
}

inline void applyBgBrightColor(std::string& spec, ELogColor color) {
    applyColorFromTable(spec, color, sBgBrightColorTable, sBgBrightColorCount);
}

inline void applyBgColor(std::string& spec, ELogColor color) {
    applyColorFromTable(spec, color, sBgColorTable, sBgColorCount);
}

static bool allocTextFormat(ELogFieldSpec& fieldSpec, uint8_t autoReset);
static bool parseTokenJustify(const char* propName, const std::string& specToken, int32_t& justify);
static bool parsePropValue(const std::string& prop, const char* propName, std::string& propValue);
static bool parseTokenColor(const char* propName, const std::string& specToken,
                            ELogColorSpec& colorSpec);
static bool simpleColorFromString(const char* colorName, ELogColor& color);
static bool parseVGAColor(const std::string& colorValue, ELogRGBColorSpec& colorSpec);
static bool parseGreyColor(const std::string& colorValue, uint8_t& greyScale);
static bool parseHexaColor(const std::string& colorValue, ELogRGBColorSpec& colorSpec);
static bool parseHexaDigit(char c, uint32_t& value);
static bool parseColorComponent(const char* color, uint8_t& value, const char* name);
static bool parseTokenTextAttribute(const std::string& specToken, ELogFontSpec& fontSpec);

const char* ELogTextSpec::m_resetSpec = ELOG_TT_RESET;

void ELogTextSpec::resolve() {
    // NOTE: if reset was specified we stop immediately
    if (m_resetTextSpec) {
        m_resolvedSpec = ELOG_TT_RESET;
        return;
    }

    // font specification

    // apply bold specification
    switch (m_fontSpec.m_boldSpec) {
        case ELogFontSpec::BoldSpec::ELOG_FONT_BOLD:
            m_resolvedSpec.append(ELOG_TT_BOLD);
            break;

        case ELogFontSpec::BoldSpec::ELOG_FONT_FAINT:
            m_resolvedSpec.append(ELOG_TT_FAINT);
            break;

        case ELogFontSpec::BoldSpec::ELOG_FONT_NORMAL:
            m_resolvedSpec.append(ELOG_TT_NORMAL);
            break;
    }

    // apply italic specification
    switch (m_fontSpec.m_italicSpec) {
        case ELogFontSpec::ItalicSpec::ELOG_ITALIC_SET:
            m_resolvedSpec.append(ELOG_TT_ITALIC);
            break;

        case ELogFontSpec::ItalicSpec::ELOG_ITALIC_RESET:
            m_resolvedSpec.append(ELOG_TT_NO_ITALIC);
            break;
    }

    // apply underline specification
    switch (m_fontSpec.m_underline) {
        case ELogFontSpec::UnderlineSpec::ELOG_UNDERLINE_SET:
            m_resolvedSpec.append(ELOG_TT_UNDERLINE);
            break;

        case ELogFontSpec::UnderlineSpec::ELOG_UNDERLINE_RESET:
            m_resolvedSpec.append(ELOG_TT_NO_UNDERLINE);
            break;
    }

    // apply cross-out (strike-through) specification
    switch (m_fontSpec.m_crossOut) {
        case ELogFontSpec::CrossOutSpec::ELOG_CROSSOUT_SET:
            m_resolvedSpec.append(ELOG_TT_CROSS_OUT);
            break;

        case ELogFontSpec::CrossOutSpec::ELOG_CROSSOUT_RESET:
            m_resolvedSpec.append(ELOG_TT_NO_CROSS_OUT);
            break;
    }

    // apply blink specification
    switch (m_fontSpec.m_blinkSpec) {
        case ELogFontSpec::BlinkSpec::ELOG_BLINK_SET_RAPID:
            m_resolvedSpec.append(ELOG_TT_RAPID_BLINK);
            break;

        case ELogFontSpec::BlinkSpec::ELOG_BLINK_SET_SLOW:
            m_resolvedSpec.append(ELOG_TT_SLOW_BLINK);
            break;

        case ELogFontSpec::BlinkSpec::ELOG_BLINK_RESET:
            m_resolvedSpec.append(ELOG_TT_NO_BLINK);
            break;
    }

    // apply foreground color
    if (m_fgColorSpec.m_colorSpecType == ELogColorSpec::SpecType::COLOR_SPEC_SIMPLE) {
        if (m_fgColorSpec.m_simpleSpec.m_color == ELOG_COLOR_DEFAULT) {
            m_resolvedSpec.append(ELOG_TT_FG_DEFAULT);
        } else if (m_fgColorSpec.m_simpleSpec.m_color != ELOG_COLOR_NONE) {
            if (m_fgColorSpec.m_simpleSpec.m_bright) {
                applyFgBrightColor(m_resolvedSpec, m_fgColorSpec.m_simpleSpec.m_color);
            } else {
                applyFgColor(m_resolvedSpec, m_fgColorSpec.m_simpleSpec.m_color);
            }
        }
    } else if (m_fgColorSpec.m_colorSpecType == ELogColorSpec::SpecType::COLOR_SPEC_RGB) {
        // SVGA color
        const ELogRGBColorSpec& rgbSpec = m_fgColorSpec.m_rgbSpec;
        m_resolvedSpec.append(formatForegroundRgb(rgbSpec.m_red, rgbSpec.m_green, rgbSpec.m_blue));
    } else if (m_fgColorSpec.m_colorSpecType == ELogColorSpec::SpecType::COLOR_SPEC_RGB_VGA) {
        // VGA color
        const ELogRGBColorSpec& rgbSpec = m_fgColorSpec.m_rgbSpec;
        m_resolvedSpec.append(
            formatForegroundRgbVga(rgbSpec.m_red, rgbSpec.m_green, rgbSpec.m_blue));
    } else if (m_fgColorSpec.m_colorSpecType == ELogColorSpec::SpecType::COLOR_SPEC_GREY) {
        // VGA grayscale
        m_resolvedSpec.append(formatForegroundGreyVga(m_fgColorSpec.m_greyScale));
    }

    // background color
    if (m_bgColorSpec.m_colorSpecType == ELogColorSpec::SpecType::COLOR_SPEC_SIMPLE) {
        if (m_bgColorSpec.m_simpleSpec.m_color == ELOG_COLOR_DEFAULT) {
            m_resolvedSpec.append(ELOG_TT_BG_DEFAULT);
        } else if (m_bgColorSpec.m_simpleSpec.m_color != ELOG_COLOR_NONE) {
            if (m_bgColorSpec.m_simpleSpec.m_bright) {
                applyBgBrightColor(m_resolvedSpec, m_bgColorSpec.m_simpleSpec.m_color);
            } else {
                applyBgColor(m_resolvedSpec, m_bgColorSpec.m_simpleSpec.m_color);
            }
        }
    } else if (m_bgColorSpec.m_colorSpecType == ELogColorSpec::SpecType::COLOR_SPEC_RGB) {
        // SVGA color
        const ELogRGBColorSpec& rgbSpec = m_bgColorSpec.m_rgbSpec;
        m_resolvedSpec.append(formatBackgroundRgb(rgbSpec.m_red, rgbSpec.m_green, rgbSpec.m_blue));
    } else if (m_fgColorSpec.m_colorSpecType == ELogColorSpec::SpecType::COLOR_SPEC_RGB_VGA) {
        // VGA color
        const ELogRGBColorSpec& rgbSpec = m_fgColorSpec.m_rgbSpec;
        m_resolvedSpec.append(
            formatForegroundRgbVga(rgbSpec.m_red, rgbSpec.m_green, rgbSpec.m_blue));
    } else if (m_fgColorSpec.m_colorSpecType == ELogColorSpec::SpecType::COLOR_SPEC_GREY) {
        // VGA grayscale
        m_resolvedSpec.append(formatForegroundGreyVga(m_fgColorSpec.m_greyScale));
    }
}

bool ELogFieldSpec::parse(const std::string& fieldSpecStr) {
    // field specification is expected to follow the following syntax:
    // ${token:justify-number}
    // additional optional font/color may be specified as follows, as many times as desired,
    // overriding previously seen specification:
    // ${token:justify:fg/bg-color=red/green/...}
    // ${token:justify:fg/bg-color=#RGB-hexa-spec}
    // predefined color list is: black, red, green, yellow, blue, magenta, cyan, white
    // both fg and bg color receive special value: reset or default which means return to normal
    // terminal color
    // all simple colors may be preceded by "bright-"
    // hexa color spec is expected to have 6 hexa digits (only lower case is accepted)
    // also text specification is supported as follows:
    // ${token:justify:text=bold/faint/normal/italic/no-italic/underline/no-underline/cross-out/
    // no-cross-out/strike-through/no-strike-through/slow-blink/rapid-blink/no-blink}
    // text specification may be given in a comma separated list so several attributes can be
    // specified in one go.
    // extended justify syntax is also accepted:
    // ${token:justify-left=5} or ${token:justify-right=12} or ${token:justify-none}
    // this can be specified several times, overriding previous specification
    // specifying once default/reset is irrecoverable (i.e. all subsequent specification is ignored)
    // TODO: add this info to README after implementing
    //
    // in order to span several fields, the begin syntax is also allowed:
    // ${text:begin-fg-color=yellow:begin-text=faint}
    int32_t justify = 0;
    std::string::size_type colonPos = fieldSpecStr.find(':');
    m_name = fieldSpecStr.substr(0, colonPos);
    while (colonPos != std::string::npos) {
        std::string::size_type nextColonPos = fieldSpecStr.find(':', colonPos + 1);
        std::string specToken =
            nextColonPos == std::string::npos
                ? fieldSpecStr.substr(colonPos + 1)
                : fieldSpecStr.substr(colonPos + 1, nextColonPos - colonPos - 1);
        specToken = trim(specToken);
        // special case: "begin-" prefix
        uint8_t autoReset = 1;
        if (specToken.starts_with("begin-")) {
            autoReset = 0;
            specToken = specToken.substr(strlen("begin-"));
        }
        if (specToken.starts_with("justify-left")) {
            if (!parseTokenJustify("justify-left", specToken, justify)) {
                return false;
            }
            m_justifySpec.m_justify = justify;
            m_justifySpec.m_mode = ELogJustifyMode::JM_LEFT;
        } else if (specToken.starts_with("justify-right")) {
            if (!parseTokenJustify("justify-right", specToken, justify)) {
                return false;
            }
            m_justifySpec.m_justify = justify;
            m_justifySpec.m_mode = ELogJustifyMode::JM_RIGHT;
        } else if (specToken.starts_with("fg-color")) {
            if (!allocTextFormat(*this, autoReset)) {
                return false;
            }
            if (!parseTokenColor("fg-color", specToken, m_textSpec->m_fgColorSpec)) {
                return false;
            }
        } else if (specToken.starts_with("bg-color")) {
            if (!allocTextFormat(*this, autoReset)) {
                return false;
            }
            if (!parseTokenColor("bg-color", specToken, m_textSpec->m_bgColorSpec)) {
                return false;
            }
        } else if (specToken.starts_with("text")) {
            if (!allocTextFormat(*this, autoReset)) {
                return false;
            }
            if (!parseTokenTextAttribute(specToken, m_textSpec->m_fontSpec)) {
                return false;
            }
        } else if (specToken.compare("default") == 0 || specToken.compare("reset") == 0) {
            if (!allocTextFormat(*this, autoReset)) {
                return false;
            }
            m_textSpec->m_resetTextSpec = 1;
        } else {
            // finally tru simple integer justification value
            if (!parseIntProp("", "", specToken, justify, false)) {
                ELOG_REPORT_ERROR("Invalid field specification: %s", specToken.c_str());
                return false;
            }
            if (justify > 0) {
                m_justifySpec.m_justify = justify;
                m_justifySpec.m_mode = ELogJustifyMode::JM_LEFT;
            } else if (justify < 0) {
                m_justifySpec.m_justify = -justify;
                m_justifySpec.m_mode = ELogJustifyMode::JM_RIGHT;
            }
        }
        colonPos = nextColonPos;
    }

    // pre-calculate the resolved formatting escape code, to avoid doing that repeated during field
    // selection/formatting
    if (m_textSpec != nullptr) {
        m_textSpec->resolve();
    }
    return true;
}

bool allocTextFormat(ELogFieldSpec& fieldSpec, uint8_t autoReset) {
    if (fieldSpec.m_textSpec == nullptr) {
        fieldSpec.m_textSpec = new (std::nothrow) ELogTextSpec();
        if (fieldSpec.m_textSpec == nullptr) {
            ELOG_REPORT_ERROR("Failed to allocate text specification object, out of memory");
            return false;
        }
    }
    fieldSpec.m_textSpec->m_autoReset = autoReset;
    return true;
}

bool parseTokenJustify(const char* propName, const std::string& specToken, int32_t& justify) {
    std::string propValue;
    if (!parsePropValue(specToken, propName, propValue)) {
        ELOG_REPORT_ERROR("Failed to parse justify specification, invalid syntax: %s",
                          specToken.c_str());
        return false;
    }
    if (!parseIntProp(propName, "", propValue, justify, true)) {
        ELOG_REPORT_ERROR("Failed to parse property %s value %s as integer", propName,
                          propValue.c_str());
        return false;
    }
    if (justify < 0) {
        ELOG_REPORT_ERROR("Invalid negative value specified for %s: %d", propName, justify);
        return false;
    }
    return true;
}

bool parsePropValue(const std::string& prop, const char* propName, std::string& propValue) {
    std::string::size_type equalPos = prop.find('=');
    if (equalPos == std::string::npos) {
        ELOG_REPORT_ERROR("Invalid property specification, missing '=': %s", prop.c_str());
        return false;
    }
    propValue = trim(prop.substr(equalPos + 1));
    return true;
}

bool parseTokenColor(const char* propName, const std::string& specToken, ELogColorSpec& colorSpec) {
    std::string propValue;
    if (!parsePropValue(specToken, propName, propValue)) {
        ELOG_REPORT_ERROR("Failed to parse color specification, invalid syntax: %s",
                          specToken.c_str());
        return false;
    }

    // check for simple colors, first with bright prefix
    if (propValue.starts_with("bright-")) {
        colorSpec.m_colorSpecType = ELogColorSpec::SpecType::COLOR_SPEC_SIMPLE;
        colorSpec.m_simpleSpec.m_bright = 1;
        propValue = propValue.substr(strlen("bright-"));
        // with bright prefix we must have a simple color
        if (!simpleColorFromString(propValue.c_str(), colorSpec.m_simpleSpec.m_color)) {
            ELOG_REPORT_ERROR(
                "Invalid color specification, simple color name must follow 'bright-' prefix: %s",
                specToken.c_str());
            return false;
        }
        colorSpec.m_colorSpecType = ELogColorSpec::SpecType::COLOR_SPEC_SIMPLE;
        return true;
    }

    // check for simple color
    if (simpleColorFromString(propValue.c_str(), colorSpec.m_simpleSpec.m_color)) {
        colorSpec.m_colorSpecType = ELogColorSpec::SpecType::COLOR_SPEC_SIMPLE;
        colorSpec.m_simpleSpec.m_bright = 0;
        return true;
    }

    // check for vga color spec
    if (propValue.starts_with("vga#")) {
        if (!parseVGAColor(propValue, colorSpec.m_rgbSpec)) {
            return false;
        }
        colorSpec.m_colorSpecType = ELogColorSpec::SpecType::COLOR_SPEC_RGB_VGA;
        return true;
    }

    // check for grey scale color
    if (propValue.starts_with("grey#") || propValue.starts_with("gray#")) {
        if (!parseGreyColor(propValue, colorSpec.m_greyScale)) {
            return false;
        }
        colorSpec.m_colorSpecType = ELogColorSpec::SpecType::COLOR_SPEC_GREY;
        return true;
    }

    // check for hexa spec
    if (!propValue.starts_with("#")) {
        ELOG_REPORT_ERROR(
            "Invalid color specification, expecting either simple color or hexadecimal "
            "specification preceded by hash sign '#': %s",
            specToken.c_str());
        return false;
    }

    if (!parseHexaColor(propValue, colorSpec.m_rgbSpec)) {
        ELOG_REPORT_ERROR("Invalid hexadecimal color specification: %s", specToken.c_str());
        return false;
    }
    colorSpec.m_colorSpecType = ELogColorSpec::SpecType::COLOR_SPEC_RGB;
    return true;
}

bool simpleColorFromString(const char* colorName, ELogColor& color) {
    for (uint32_t i = 0; i < sColorCount; ++i) {
        if (strcmp(sColorTable[i].m_colorName, colorName) == 0) {
            color = sColorTable[i].m_color;
            return true;
        }
    }
    return false;
}

bool parseVGAColor(const std::string& colorValue, ELogRGBColorSpec& colorSpec) {
    // we skip initial vga, then parse as hexa, finally verify components range
    // must start with hash sign
    if (!colorValue.starts_with("vga#")) {
        ELOG_REPORT_ERROR("Invalid hexadecimal VGA color specification, must start with 'vga#': %s",
                          colorValue.c_str());
        return false;
    }
    if (!parseHexaColor(colorValue.c_str() + 3, colorSpec)) {
        return false;
    }
    if (colorSpec.m_red >= 32) {
        ELOG_REPORT_ERROR(
            "Invalid hexadecimal VGA color specification, red component too large (cannot exceed "
            "0x1F): %s",
            colorValue.c_str());
        return false;
    }
    if (colorSpec.m_green >= 32) {
        ELOG_REPORT_ERROR(
            "Invalid hexadecimal VGA color specification, green component too large (cannot exceed "
            "0x1F): %s",
            colorValue.c_str());
        return false;
    }
    if (colorSpec.m_blue >= 32) {
        ELOG_REPORT_ERROR(
            "Invalid hexadecimal VGA color specification, blue component too large (cannot exceed "
            "0x1F): %s",
            colorValue.c_str());
        return false;
    }
    return true;
}

bool parseGreyColor(const std::string& colorValue, uint8_t& greyScale) {
    // we skip initial grey/gray, then parse as decimal
    if (!colorValue.starts_with("grey#") && !colorValue.starts_with("gray")) {
        ELOG_REPORT_ERROR(
            "Invalid VGA grey color specification, must start with 'grey#' or 'gray#': %s",
            colorValue.c_str());
        return false;
    }

    uint64_t value = 0;
    if (!parseIntProp("grey", "", colorValue.c_str() + 5, value)) {
        ELOG_REPORT_ERROR("Failed to parse grayscale as integer: %s", colorValue.c_str());
        return false;
    }

    if (value >= 24) {
        ELOG_REPORT_ERROR(
            "Invalid grayscale color specification, value exceeds allowed range [0-23]: %s",
            colorValue.c_str());
        return false;
    }

    return true;
}

bool parseHexaColor(const std::string& colorValue, ELogRGBColorSpec& colorSpec) {
    // must start with hash sign
    if (!colorValue.starts_with("#")) {
        ELOG_REPORT_ERROR(
            "Invalid hexadecimal color specification, must be preceded by hash sign '#': %s",
            colorValue.c_str());
        return false;
    }

    // now we should see 3 hexa pairs
    if (colorValue.length() != 7) {
        ELOG_REPORT_ERROR(
            "Invalid hexadecimal color specification, hash sign must be follow by exactly 6 "
            "hexadecimal digits: %s",
            colorValue.c_str());
        return false;
    }

    // red
    if (!parseColorComponent(colorValue.c_str() + 1, colorSpec.m_red, "red")) {
        return false;
    }

    // green
    if (!parseColorComponent(colorValue.c_str() + 3, colorSpec.m_green, "green")) {
        return false;
    }

    // blue
    if (!parseColorComponent(colorValue.c_str() + 5, colorSpec.m_blue, "blue")) {
        return false;
    }
    return true;
}

bool parseHexaDigit(char c, uint32_t& value) {
    if (c >= '0' && c <= '9') {
        value = c - '0';
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        value = c - 'a' + 10;
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        value = c - 'A' + 10;
        return true;
    }

    ELOG_REPORT_ERROR("Invalid hexadecimal digit: %c", c);
    return false;
}

bool parseColorComponent(const char* color, uint8_t& value, const char* name) {
    uint32_t digit1 = 0;
    uint32_t digit2 = 0;
    if (!parseHexaDigit(color[0], digit1) || !parseHexaDigit(color[1], digit2)) {
        ELOG_REPORT_ERROR("Invalid hexadecimal specification for %s component: %s", name, color);
        return false;
    }
    value = digit1 * 16 + digit2;
    return true;
}

bool parseTokenTextAttribute(const std::string& specToken, ELogFontSpec& fontSpec) {
    std::string propValue;
    if (!parsePropValue(specToken, "text", propValue)) {
        ELOG_REPORT_ERROR("Failed to parse text specification, invalid syntax: %s",
                          specToken.c_str());
        return false;
    }

    // parse comma separated list
    std::string::size_type prevPos = 0;  // always one past previous comma
    std::string::size_type commaPos = propValue.find(',');
    while (prevPos != std::string::npos) {
        // get next token in list
        std::string token = commaPos == std::string::npos
                                ? propValue.substr(prevPos)
                                : propValue.substr(prevPos, commaPos - prevPos);
        prevPos = commaPos;
        if (prevPos != std::string::npos) {
            ++prevPos;
            commaPos = propValue.find(',', prevPos);
        }

        // parse token
        if (token.compare("bold") == 0) {
            fontSpec.m_boldSpec = ELogFontSpec::BoldSpec::ELOG_FONT_BOLD;
        } else if (token.compare("faint") == 0) {
            fontSpec.m_boldSpec = ELogFontSpec::BoldSpec::ELOG_FONT_FAINT;
        } else if (token.compare("normal") == 0) {
            fontSpec.m_boldSpec = ELogFontSpec::BoldSpec::ELOG_FONT_NORMAL;
        } else if (token.compare("italic") == 0) {
            fontSpec.m_italicSpec = ELogFontSpec::ItalicSpec::ELOG_ITALIC_SET;
        } else if (token.compare("no-italic") == 0) {
            fontSpec.m_italicSpec = ELogFontSpec::ItalicSpec::ELOG_ITALIC_RESET;
        } else if (token.compare("underline") == 0) {
            fontSpec.m_underline = ELogFontSpec::UnderlineSpec::ELOG_UNDERLINE_SET;
        } else if (token.compare("no-underline") == 0) {
            fontSpec.m_underline = ELogFontSpec::UnderlineSpec::ELOG_UNDERLINE_RESET;
        } else if (token.compare("cross-out") == 0) {
            fontSpec.m_crossOut = ELogFontSpec::CrossOutSpec::ELOG_CROSSOUT_SET;
        } else if (token.compare("no-cross-out") == 0) {
            fontSpec.m_crossOut = ELogFontSpec::CrossOutSpec::ELOG_CROSSOUT_RESET;
        } else if (token.compare("strike-through") == 0) {
            fontSpec.m_crossOut = ELogFontSpec::CrossOutSpec::ELOG_CROSSOUT_SET;
        } else if (token.compare("no-strike-through") == 0) {
            fontSpec.m_crossOut = ELogFontSpec::CrossOutSpec::ELOG_CROSSOUT_RESET;
        } else if (token.compare("blink-slow") == 0) {
            fontSpec.m_blinkSpec = ELogFontSpec::BlinkSpec::ELOG_BLINK_SET_RAPID;
        } else if (token.compare("blink-rapid") == 0) {
            fontSpec.m_blinkSpec = ELogFontSpec::BlinkSpec::ELOG_BLINK_SET_SLOW;
        } else if (token.compare("no-blink") == 0) {
            fontSpec.m_blinkSpec = ELogFontSpec::BlinkSpec::ELOG_BLINK_RESET;
        } else {
            ELOG_REPORT_ERROR("Invalid font specification, unrecognized property: %s",
                              token.c_str());
            return false;
        }
    }
    return true;
}

}  // namespace  elog