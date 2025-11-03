#include "elog_api_time_source.h"

#include "elog_api.h"
#include "elog_internal.h"
#include "elog_time_source.h"

namespace elog {

static ELogTimeSource sTimeSource;

void initTimeSource() {
    if (isTimeSourceEnabled()) {
        sTimeSource.start();
    }
}

void termTimeSource() {
    if (isTimeSourceEnabled()) {
        sTimeSource.stop();
    }
}

void enableLazyTimeSource() {
    if (!isTimeSourceEnabled()) {
        modifyParams().m_enableTimeSource.m_atomicValue.store(true, std::memory_order_release);
        sTimeSource.start();
    }
}

void disableLazyTimeSource() {
    if (isTimeSourceEnabled()) {
        sTimeSource.stop();
        modifyParams().m_enableTimeSource.m_atomicValue.store(false, std::memory_order_release);
    }
}

void configureLazyTimeSource(uint64_t resolution, ELogTimeUnits resolutionUnits) {
    if (isTimeSourceEnabled()) {
        sTimeSource.stop();
    }
    modifyParams().m_timeSourceResolution = resolution;
    modifyParams().m_timeSourceUnits = resolutionUnits;
    if (isTimeSourceEnabled()) {
        sTimeSource.start();
    }
}

bool configTimeSourceProps(const ELogPropertySequence& props) {
    // configure time source (allow override from env)
    bool enableTimeSource = false;
    bool found = false;
    if (!getBoolEnv(ELOG_CONFIG_ENABLE_TIME_SOURCE_NAME, enableTimeSource, true, &found)) {
        return false;
    }
    if (!found &&
        !getBoolProp(props, ELOG_CONFIG_ENABLE_TIME_SOURCE_NAME, enableTimeSource, &found)) {
        return false;
    }

    if (found) {
        if (enableTimeSource && !isTimeSourceEnabled()) {
            // get resolution (allow override from env)
            std::string timeSourceResolution;
            if (getStringEnv(ELOG_CONFIG_TIME_SOURCE_RESOLUTION_NAME, timeSourceResolution) ||
                getProp(props, ELOG_CONFIG_TIME_SOURCE_RESOLUTION_NAME, timeSourceResolution)) {
                if (!parseTimeValueProp(ELOG_CONFIG_TIME_SOURCE_RESOLUTION_NAME, "",
                                        timeSourceResolution, modifyParams().m_timeSourceResolution,
                                        modifyParams().m_timeSourceUnits)) {
                    return false;
                }
            }
            sTimeSource.initialize(getParams().m_timeSourceResolution,
                                   getParams().m_timeSourceUnits);
            sTimeSource.start();
            modifyParams().m_enableTimeSource.m_atomicValue.store(true, std::memory_order_release);
        } else if (!enableTimeSource && isTimeSourceEnabled()) {
            sTimeSource.stop();
            modifyParams().m_enableTimeSource.m_atomicValue.store(false, std::memory_order_release);
        }
    }

    return true;
}

bool configTimeSource(const ELogConfigMapNode* cfgMap) {
    // configure time source (allow override from env)
    bool enableTimeSource = false;
    bool found = false;
    if (!getBoolEnv(ELOG_CONFIG_ENABLE_TIME_SOURCE_NAME, enableTimeSource, true, &found)) {
        return false;
    }
    if (!found &&
        !cfgMap->getBoolValue(ELOG_CONFIG_ENABLE_TIME_SOURCE_NAME, found, enableTimeSource)) {
        return false;
    }

    if (found) {
        if (enableTimeSource && !isTimeSourceEnabled()) {
            // get resolution (allow override from env)
            std::string timeSourceResolution;
            found = getStringEnv(ELOG_CONFIG_TIME_SOURCE_RESOLUTION_NAME, timeSourceResolution);
            if (!found && !cfgMap->getStringValue(ELOG_CONFIG_TIME_SOURCE_RESOLUTION_NAME, found,
                                                  timeSourceResolution)) {
                return false;
            }
            if (found &&
                !parseTimeValueProp(ELOG_CONFIG_TIME_SOURCE_RESOLUTION_NAME, "",
                                    timeSourceResolution, modifyParams().m_timeSourceResolution,
                                    modifyParams().m_timeSourceUnits)) {
                return false;
            }
            sTimeSource.initialize(getParams().m_timeSourceResolution,
                                   getParams().m_timeSourceUnits);
            sTimeSource.start();
            modifyParams().m_enableTimeSource.m_atomicValue.store(true, std::memory_order_release);

        } else if (!enableTimeSource && isTimeSourceEnabled()) {
            sTimeSource.stop();
            modifyParams().m_enableTimeSource.m_atomicValue.store(false, std::memory_order_release);
        }
    }

    return true;
}

void getCurrentTimeFromSource(ELogTime& currentTime) { sTimeSource.getCurrentTime(currentTime); }

}  // namespace elog