#include "elog_common.h"

#include <cassert>

#include "elog_report.h"

namespace elog {

bool parseIntProp(const char* propName, const std::string& logTargetCfg, const std::string& prop,
                  int32_t& value, bool issueError /* = true */) {
    std::size_t pos = 0;
    try {
        value = std::stol(prop, &pos);
    } catch (std::exception& e) {
        if (issueError) {
            ELOG_REPORT_ERROR("Invalid %s value %s: %s (%s)", propName, prop.c_str(),
                              logTargetCfg.c_str(), e.what());
        }
        return false;
    }
    if (pos != prop.length()) {
        if (issueError) {
            ELOG_REPORT_ERROR("Excess characters at %s value %s: %s", propName, prop.c_str(),
                              logTargetCfg.c_str());
        }
        return false;
    }
    return true;
}

bool parseIntProp(const char* propName, const std::string& logTargetCfg, const std::string& prop,
                  uint32_t& value, bool issueError /* = true */) {
    std::size_t pos = 0;
    try {
        value = std::stoul(prop, &pos);
    } catch (std::exception& e) {
        if (issueError) {
            ELOG_REPORT_ERROR("Invalid %s value %s: %s (%s)", propName, prop.c_str(),
                              logTargetCfg.c_str(), e.what());
        }
        return false;
    }
    if (pos != prop.length()) {
        if (issueError) {
            ELOG_REPORT_ERROR("Excess characters at %s value %s: %s", propName, prop.c_str(),
                              logTargetCfg.c_str());
        }
        return false;
    }
    return true;
}

bool parseIntProp(const char* propName, const std::string& logTargetCfg, const std::string& prop,
                  int64_t& value, bool issueError /* = true */) {
    std::size_t pos = 0;
    try {
        value = std::stoll(prop, &pos);
    } catch (std::exception& e) {
        if (issueError) {
            ELOG_REPORT_ERROR("Invalid %s value %s: %s (%s)", propName, prop.c_str(),
                              logTargetCfg.c_str(), e.what());
        }
        return false;
    }
    if (pos != prop.length()) {
        if (issueError) {
            ELOG_REPORT_ERROR("Excess characters at %s value %s: %s", propName, prop.c_str(),
                              logTargetCfg.c_str());
        }
        return false;
    }
    return true;
}

bool parseIntProp(const char* propName, const std::string& logTargetCfg, const std::string& prop,
                  uint64_t& value, bool issueError /* = true */) {
    std::size_t pos = 0;
    try {
        value = std::stoull(prop, &pos);
    } catch (std::exception& e) {
        if (issueError) {
            ELOG_REPORT_ERROR("Invalid %s value %s: %s (%s)", propName, prop.c_str(),
                              logTargetCfg.c_str(), e.what());
        }
        return false;
    }
    if (pos != prop.length()) {
        if (issueError) {
            ELOG_REPORT_ERROR("Excess characters at %s value %s: %s", propName, prop.c_str(),
                              logTargetCfg.c_str());
        }
        return false;
    }
    return true;
}

bool parseBoolProp(const char* propName, const std::string& logTargetCfg, const std::string& prop,
                   bool& value, bool issueError /* = true */) {
    if (prop.compare("true") == 0 || prop.compare("yes") == 0) {
        value = true;
    } else if (prop.compare("false") == 0 || prop.compare("no") == 0) {
        value = false;
    } else {
        if (issueError) {
            ELOG_REPORT_ERROR("Invalid boolean property %s value %s: %s", propName, prop.c_str(),
                              logTargetCfg.c_str());
        }
        return false;
    }
    return true;
}

// TODO: should be parseTimeValueProp
bool parseTimeoutProp(const char* propName, const std::string& logTargetCfg,
                      const std::string& prop, uint64_t& timeout, ELogTimeoutUnits targetUnits,
                      bool issueError /* = true */) {
    std::size_t pos = 0;
    try {
        timeout = std::stoull(prop, &pos);
    } catch (std::exception& e) {
        if (issueError) {
            ELOG_REPORT_ERROR("Invalid %s timeout value %s: %s (%s)", propName, prop.c_str(),
                              logTargetCfg.c_str(), e.what());
        }
        return false;
    }
    if (pos == prop.length()) {
        if (issueError) {
            ELOG_REPORT_ERROR("Missing timeout unit specification at %s value %s: %s", propName,
                              prop.c_str(), logTargetCfg.c_str());
        }
        return false;
    }

    // first comput nanos, then move to target units
    std::string units = toLower(trim(prop.substr(pos)));
    if (units.compare("d") == 0 || units.compare("day") == 0 || units.compare("days") == 0) {
        // days
        timeout *= (24 * 60 * 60 * 1000000000ull);
    } else if (units.compare("h") == 0 || units.compare("hour") == 0 ||
               units.compare("hours") == 0) {
        // hours
        timeout *= (60 * 60 * 1000000000ull);
    } else if (units.compare("m") == 0 || units.compare("minute") == 0 ||
               units.compare("minutes") == 0) {
        // minutes
        timeout *= (60 * 1000000000ull);
    } else if (units.compare("s") == 0 || units.compare("second") == 0 ||
               units.compare("seconds") == 0) {
        // seconds
        timeout *= 1000000000ull;
    } else if (units.compare("ms") == 0 || units.compare("milli") == 0 ||
               units.compare("millis") == 0 || units.compare("milliseconds") == 0) {
        // milliseconds
        timeout *= 1000000ull;
    } else if (units.compare("us") == 0 || units.compare("micro") == 0 ||
               units.compare("micros") == 0 || units.compare("microseconds") == 0) {
        // microseconds
        timeout *= 1000ull;
    } else if (units.compare("ns") == 0 || units.compare("nano") == 0 ||
               units.compare("nanos") == 0 || units.compare("nanoseconds") == 0) {
        // nanoseconds
    } else {
        if (issueError) {
            ELOG_REPORT_ERROR(
                "Invalid timeout units specification '%s', at %s value %s: %s, expecting ms, us or "
                "ns",
                units.c_str(), propName, prop.c_str(), logTargetCfg.c_str());
        }
        return false;
    }

    // now convert to target units
    switch (targetUnits) {
        case ELogTimeoutUnits::TU_SECONDS:
            timeout /= 1000000000ull;
            break;

        case ELogTimeoutUnits::TU_MILLI_SECONDS:
            timeout /= 1000000ull;
            break;

        case ELogTimeoutUnits::TU_MICRO_SECONDS:
            timeout /= 1000ull;
            break;

        case ELogTimeoutUnits::TU_NANO_SECONDS:
            break;

        default:
            if (issueError) {
                ELOG_REPORT_ERROR("Invalid target timeout unit %u at %s value %s: %s",
                                  (uint32_t)targetUnits, propName, prop.c_str(),
                                  logTargetCfg.c_str());
            }
            break;
    }
    return true;
}

bool parseSizeProp(const char* propName, const std::string& logTargetCfg, const std::string& prop,
                   uint64_t& size, ELogSizeUnits targetUnits, bool issueError /* = true */) {
    std::size_t pos = 0;
    try {
        size = std::stoull(prop, &pos);
    } catch (std::exception& e) {
        if (issueError) {
            ELOG_REPORT_ERROR("Invalid %s size value %s: %s (%s)", propName, prop.c_str(),
                              logTargetCfg.c_str(), e.what());
        }
        return false;
    }
    if (pos == prop.length()) {
        if (issueError) {
            ELOG_REPORT_ERROR("Missing size unit specification at %s value %s: %s", propName,
                              prop.c_str(), logTargetCfg.c_str());
        }
        return false;
    }

    // first comput bytes, then move to target units
    std::string units = toLower(trim(prop.substr(pos)));
    if (units.compare("b") == 0 || units.compare("byte") == 0 || units.compare("bytes") == 0) {
        // bytes
    } else if (units.compare("k") == 0 || units.compare("kb") == 0 ||
               units.compare("kilobyte") == 0 || units.compare("kilobytes") == 0) {
        // kilobytes
        size *= 1024ull;
    } else if (units.compare("m") == 0 || units.compare("mb") == 0 ||
               units.compare("megabyte") == 0 || units.compare("megabytes") == 0) {
        // megabytes
        size *= 1024ull * 1024ull;
    } else if (units.compare("g") == 0 || units.compare("gb") == 0 ||
               units.compare("gigabyte") == 0 || units.compare("gigabytes") == 0) {
        // gigabytes
        size *= 1024ull * 1024ull * 1024ull;
    } else {
        if (issueError) {
            ELOG_REPORT_ERROR(
                "Invalid size units specification '%s', at %s value %s: %s, expecting b, kb, mb or "
                "gb",
                units.c_str(), propName, prop.c_str(), logTargetCfg.c_str());
        }
        return false;
    }

    // now convert to target units
    switch (targetUnits) {
        case ELogSizeUnits::SU_BYTES:
            break;

        case ELogSizeUnits::SU_KILO_BYTES:
            size /= 1024ull;
            break;

        case ELogSizeUnits::SU_MEGA_BYTES:
            size /= (1024ull * 1024ull);
            break;

        case ELogSizeUnits::SU_GIGA_BYTES:
            size /= (1024ull * 1024ull * 1024ull);
            break;

        default:
            if (issueError) {
                ELOG_REPORT_ERROR("Invalid target size unit %u at %s value %s: %s",
                                  (uint32_t)targetUnits, propName, prop.c_str(),
                                  logTargetCfg.c_str());
            }
            break;
    }
    return true;
}

size_t elog_strncpy(char* dest, const char* src, size_t destLen, size_t srcLen /* = 0 */) {
    assert(destLen > 0);
    if (srcLen == 0) {
        srcLen = strlen(src);
    }
    if (srcLen + 1 < destLen) {
        // copy terminating null as well (use faster memcpy())
        memcpy(dest, src, srcLen + 1);
        return srcLen;
    }
    // reserve one char for terminating null
    size_t copyLen = destLen - 1;
    memcpy(dest, src, copyLen);

    // add terminating null
    dest[copyLen] = 0;

    // return number of bytes copied, excluding terminating null
    return copyLen;
}

bool elog_getenv(const char* envVarName, std::string& envVarValue) {
#ifdef ELOG_SECURE
#ifdef ELOG_WINDOWS
    const size_t ENV_BUF_SIZE = 256;
    char envBuf[ENV_BUF_SIZE];
    size_t retSize = 0;
    errno_t res = getenv_s(&retSize, envBuf, ENV_BUF_SIZE, envVarName);
    if (res != 0) {
        ELOG_REPORT_ERROR("Failed to get environment variable %s, security error %d", envVarName,
                          res);
        return false;
    }
    if (retSize == 0) {
        return false;
    }
    envVarValue = envBuf;
    return true;
#else
    char* envVarValueLocal = secure_getenv(envVarName);
    if (envVarValueLocal == nullptr) {
        return false;
    }
    envVarValue = envVarValueLocal;
    return true;
#endif
#else
    char* envVarValueLocal = getenv(envVarName);
    if (envVarValueLocal == nullptr) {
        return false;
    }
    envVarValue = envVarValueLocal;
    return true;
#endif
}

FILE* elog_fopen(const char* path, const char* mode) {
    FILE* handle = nullptr;
#if defined(ELOG_SECURE) && defined(ELOG_WINDOWS)
    errno_t res = fopen_s(&handle, path, mode);
    if (res != 0) {
        ELOG_REPORT_SYS_ERROR(fopen_s, "Failed to open log file %s", path);
    }
#else
    handle = fopen(path, mode);
    if (handle == nullptr) {
        ELOG_REPORT_SYS_ERROR(fopen, "Failed to open log file %s", path);
    }
#endif
    return handle;
}

}  // namespace elog
