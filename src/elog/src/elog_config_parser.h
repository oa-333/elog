#ifndef __ELOG_CONFIG_PARSER_H__
#define __ELOG_CONFIG_PARSER_H__

#include "elog_config.h"
#include "elog_source.h"
#include "elog_string_tokenizer.h"
#include "elog_target_spec.h"

namespace elog {

class ELogConfigParser {
public:
    static bool parseLogLevel(const char* logLevelStr, ELogLevel& logLevel,
                              ELogPropagateMode& propagateMode);

    static bool parseLogAffinityList(const char* affinityListStr, ELogTargetAffinityMask& mask);

    static ELogConfig* parseLogTargetConfig(const std::string& logTargetUrl);

    static bool parseHostPort(const std::string& server, std::string& host, int& port);

    static bool parseRateLimit(const std::string& rateLimitCfg, uint64_t& maxMsg, uint64_t& timeout,
                               ELogTimeUnits& units);

private:
    static void insertPropOverride(ELogPropertyMap& props, const std::string& key,
                                   const std::string& value);

    static bool parseLogTargetUrl(const std::string& logTargetUrl,
                                  ELogTargetUrlSpec& logTargetUrlSpec, size_t basePos = 0);

    static bool parseUrlPath(ELogTargetUrlSpec& logTargetUrlSpec, size_t basePos);
    static bool insertPropPosOverride(ELogPropertyPosMap& props, const std::string& key,
                                      const std::string& value, size_t keyPos, size_t valuePos);
    static ELogConfigMapNode* logTargetUrlToConfig(ELogTargetUrlSpec* urlSpec,
                                                   ELogConfigSourceContext* sourceContext);
    static bool addConfigProperty(ELogConfigMapNode* mapNode, const char* key,
                                  const ELogPropertyPos* prop);
};

}  // namespace elog

#endif  // __ELOG_CONFIG_PARSER_H__