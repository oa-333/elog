#ifndef __ELOG_FONT_COLOR_H__
#define __ELOG_FONT_COLOR_H__

#include <sstream>
#include <string>

// ANSI C escape code (used for terminal text manipulation)
#define ELOG_ESCAPE_CODE "\x1B"

// ANSI C Control Sequence Introducer
// see https://en.wikipedia.org/wiki/ANSI_escape_code#CSIsection for more details
#define ELOG_CSI ELOG_ESCAPE_CODE "["

// SGR codes, e.g. ELOG_SGR(31) = "ESC[31m"
#define ELOG_SGR_SUFFIX "m"
#define ELOG_SGR(CODE) ELOG_CSI #CODE ELOG_SGR_SUFFIX
#define ELOG_SGR_RAW(STR) ELOG_CSI STR ELOG_SGR_SUFFIX

// Terminal Text utility macros (predefined SGR codes)
// see https://en.wikipedia.org/wiki/ANSI_escape_code#SGR for more details

// reset all previous settings to default
#define ELOG_TT_DEFAULT ELOG_SGR(0)

// font settings
#define ELOG_TT_BOLD ELOG_SGR(1)
#define ELOG_TT_FAINT ELOG_SGR(2)
#define ELOG_TT_ITALIC ELOG_SGR(3)
#define ELOG_TT_UNDERLINE ELOG_SGR(4)
#define ELOG_TT_SLOW_BLINK ELOG_SGR(5)
#define ELOG_TT_RAPID_BLINK ELOG_SGR(6)
#define ELOG_TT_CROSS_OUT ELOG_SGR(7)
// no bold no faint
#define ELOG_TT_NORMAL ELOG_SGR(22)
#define ELOG_TT_NO_ITALIC ELOG_SGR(23)
#define ELOG_TT_NO_UNDERLINE ELOG_SGR(24)
#define ELOG_TT_NO_BLINK ELOG_SGR(25)
#define ELOG_TT_NO_CROSS_OUT ELOG_SGR(29)

// Terminal Text ForeGround colors as SGR codes
#define ELOG_TT_FG_BLACK ELOG_SGR(30)
#define ELOG_TT_FG_RED ELOG_SGR(31)
#define ELOG_TT_FG_GREEN ELOG_SGR(32)
#define ELOG_TT_FG_YELLOW ELOG_SGR(33)
#define ELOG_TT_FG_BLUE ELOG_SGR(34)
#define ELOG_TT_FG_MAGENTA ELOG_SGR(35)
#define ELOG_TT_FG_CYAN ELOG_SGR(36)
#define ELOG_TT_FG_WHITE ELOG_SGR(37)

// The following codes are for VGA 256 color pallette

// custom foreground colors from 216 color pallette
// first 8 are predefined colors, next 8 are bright colors, then followed by 216 custom colors
// so for custom colors, numbers in the range 16-231 should be specified
// see https://en.wikipedia.org/wiki/ANSI_escape_code#8-bit for more details
#define ELOG_TT_FG_VGA(ColorIndex) ELOG_SGR_RAW("38;5;" #ColorIndex)

// VGA color pallette start index
#define ELOG_VGA_BASE 16

// VGA color pallette dimension size
#define ELOG_VGA_DIM 6

// multiplication factor for red component in VGA color translation function
#define ELOG_VGA_RED_FACTOR ((ELOG_VGA_DIM) * (ELOG_VGA_DIM))

// multiplication factor for green component in VGA color translation function
#define ELOG_VGA_GREEN_FACTOR ELOG_VGA_DIM

// multiplication factor for blue component in VGA color translation function
#define ELOG_VGA_BLUE_FACTOR 1

// first VGA grayscale color index
#define ELOG_VGA_GREY_BASE 232

// convert rgb to VGA color pallette index
inline int rgb2vga(uint8_t red, uint8_t green, uint8_t blue) {
    return ELOG_VGA_BASE + ELOG_VGA_RED_FACTOR * red + ELOG_VGA_GREEN_FACTOR * green +
           ELOG_VGA_BLUE_FACTOR * blue;
}

// convert grayscale value to VGA color pallette index
inline int grey2vga(uint8_t greyScale) { return ELOG_VGA_GREY_BASE + greyScale; }

// format foreground RGB escape code from dynamic values
inline std::string formatForegroundRgbVga(uint8_t red, uint8_t green, uint8_t blue) {
    std::stringstream s;
    s << ELOG_CSI << "38;5;" << std::to_string(rgb2vga(red, green, blue)) << ELOG_SGR_SUFFIX;
    return s.str();
}

// grayscale foreground colors, from 232 to 255, where 232 is almost black and 255 is almost white
#define ELOG_TT_FG_GRAY24(ColorIndex) ELOG_SGR_RAW("38;5;" #ColorIndex)

// format foreground RGB escape code from dynamic values
inline std::string formatForegroundGreyVga(uint8_t greyScale) {
    std::stringstream s;
    s << ELOG_CSI << "38;5;" << std::to_string(grey2vga(greyScale)) << ELOG_SGR_SUFFIX;
    return s.str();
}

// 24 bit color pallette background color codes
// see https://en.wikipedia.org/wiki/ANSI_escape_code#24-bit for more details
#define ELOG_TT_FG_RGB(R, G, B) ELOG_SGR_RAW("38;2;" #R ";" #G ";" #B)

// format foreground RGB escape code from dynamic values
inline std::string formatForegroundRgb(uint8_t red, uint8_t green, uint8_t blue) {
    std::stringstream s;
    s << ELOG_CSI << "38;2;" << std::to_string(red) << ";" << std::to_string(green) << ";"
      << std::to_string(blue) << ELOG_SGR_SUFFIX;
    return s.str();
}

// reset text foreground color to default value
#define ELOG_TT_FG_DEFAULT ELOG_SGR(39)

// bright color set (foreground)
#define ELOG_TT_FG_BRIGHT_BLACK ELOG_SGR(90)
#define ELOG_TT_FG_BRIGHT_RED ELOG_SGR(91)
#define ELOG_TT_FG_BRIGHT_GREEN ELOG_SGR(92)
#define ELOG_TT_FG_BRIGHT_YELLOW ELOG_SGR(93)
#define ELOG_TT_FG_BRIGHT_BLUE ELOG_SGR(94)
#define ELOG_TT_FG_BRIGHT_MAGENTA ELOG_SGR(95)
#define ELOG_TT_FG_BRIGHT_CYAN ELOG_SGR(96)
#define ELOG_TT_FG_BRIGHT_WHITE ELOG_SGR(97)

// Terminal Text BackGround colors as SGR codes
#define ELOG_TT_BG_BLACK ELOG_SGR(40)
#define ELOG_TT_BG_RED ELOG_SGR(41)
#define ELOG_TT_BG_GREEN ELOG_SGR(42)
#define ELOG_TT_BG_YELLOW ELOG_SGR(43)
#define ELOG_TT_BG_BLUE ELOG_SGR(44)
#define ELOG_TT_BG_MAGENTA ELOG_SGR(45)
#define ELOG_TT_BG_CYAN ELOG_SGR(46)
#define ELOG_TT_BG_WHITE ELOG_SGR(47)

// The following codes are for VGA 256 color pallette

// custom background colors from 216 color pallette
// first 8 are predefined colors, next 8 are bright colors, then followed by 216 custom colors
// so for custom colors, numbers in the range 16-231 should be specified
// see https://en.wikipedia.org/wiki/ANSI_escape_code#8-bit for more details
#define ELOG_TT_BG_COLOR216(ColorIndex) ELOG_SGR_RAW("48;5;" #ColorIndex)

// format background RGB escape code from dynamic values
inline std::string formatBackgroundRgbVga(uint8_t red, uint8_t green, uint8_t blue) {
    std::stringstream s;
    s << ELOG_CSI << "48;5;" << std::to_string(rgb2vga(red, green, blue)) << ELOG_SGR_SUFFIX;
    return s.str();
}

// grayscale background colors, from 232 to 255, where 232 is almost black and 255 is almost white
#define ELOG_TT_BG_GRAY24(ColorIndex) ELOG_SGR_RAW("48;5;" #ColorIndex)

// format background RGB escape code from dynamic values
inline std::string formatBackgroundGreyVga(uint8_t greyScale) {
    std::stringstream s;
    s << ELOG_CSI << "48;5;" << std::to_string(grey2vga(greyScale)) << ELOG_SGR_SUFFIX;
    return s.str();
}

// 24 bit color pallette background color codes
// see https://en.wikipedia.org/wiki/ANSI_escape_code#24-bit for more details
#define ELOG_TT_BG_RGB(R, G, B) ELOG_SGR_RAW("48;2;" #R ";" #G ";" #B)

// format background RGB escape code from dynamic values
inline std::string formatBackgroundRgb(uint8_t red, uint8_t green, uint8_t blue) {
    std::stringstream s;
    s << ELOG_CSI << "48;2;" << std::to_string(red) << ";" << std::to_string(green) << ";"
      << std::to_string(blue) << ELOG_SGR_SUFFIX;
    return s.str();
}

// reset text background color to default value
#define ELOG_TT_BG_DEFAULT ELOG_SGR(49)

// bright color set (background)
#define ELOG_TT_BG_BRIGHT_BLACK ELOG_SGR(100)
#define ELOG_TT_BG_BRIGHT_RED ELOG_SGR(101)
#define ELOG_TT_BG_BRIGHT_GREEN ELOG_SGR(102)
#define ELOG_TT_BG_BRIGHT_YELLOW ELOG_SGR(103)
#define ELOG_TT_BG_BRIGHT_BLUE ELOG_SGR(104)
#define ELOG_TT_BG_BRIGHT_MAGENTA ELOG_SGR(105)
#define ELOG_TT_BG_BRIGHT_CYAN ELOG_SGR(106)
#define ELOG_TT_BG_BRIGHT_WHITE ELOG_SGR(107)

namespace elog {}  // namespace elog

#endif  // __ELOG_COLOR_H__