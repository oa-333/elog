#ifndef __ELOG_CONFIG_PARSER_H__
#define __ELOG_CONFIG_PARSER_H__

#include "elog_config.h"
#include "elog_source.h"
#include "elog_spec_tokenizer.h"
#include "elog_target_spec.h"

namespace elog {

class ELogConfigParser {
public:
    static bool parseLogLevel(const char* logLevelStr, ELogLevel& logLevel,
                              ELogPropagateMode& propagateMode);

    static bool parseLogAffinityList(const char* affinityListStr, ELogTargetAffinityMask& mask);

    static bool parseLogTargetSpec(const std::string& logTargetCfg,
                                   ELogTargetNestedSpec& logTargetNestedSpec,
                                   ELogTargetSpecStyle& specStyle);

    static ELogConfig* parseLogTargetConfig(const std::string& logTargetUrl);

    static bool parseHostPort(const std::string& server, std::string& host, int& port);

private:
    static bool parseLogTargetSpec(const std::string& logTargetCfg, ELogTargetSpec& logTargetSpec);
    static bool parseLogTargetNestedSpec(const std::string& logTargetCfg,
                                         ELogTargetNestedSpec& logTargetNestedSpec);

    static bool parseLogTargetNestedSpec(const std::string& logTargetCfg,
                                         ELogTargetNestedSpec& logTargetNestedSpec,
                                         ELogStringTokenizer& tok);
    static void insertPropOverride(ELogPropertyMap& props, const std::string& key,
                                   const std::string& value);
    static void tryParsePathAsHostPort(const std::string& logTargetCfg,
                                       ELogTargetSpec& logTargetSpec);

    static bool parseLogTargetUrl(const std::string& logTargetUrl,
                                  ELogTargetUrlSpec& logTargetUrlSpec, uint32_t basePos = 0);

    static bool parseUrlPath(ELogTargetUrlSpec& logTargetUrlSpec, uint32_t basePos);
    static void insertPropPosOverride(ELogPropertyPosMap& props, const std::string& key,
                                      const std::string& value, uint32_t keyPos, uint32_t valuePos);
    static ELogConfigMapNode* logTargetUrlToConfig(ELogTargetUrlSpec* urlSpec,
                                                   ELogConfigSourceContext* sourceContext);
    static bool addConfigProperty(ELogConfigMapNode* mapNode, const char* key,
                                  const ELogPropertyPos& prop);
    static bool addConfigIntProperty(ELogConfigMapNode* mapNode, const char* key,
                                     const ELogIntPropertyPos& prop);
};

}  // namespace elog

#endif  // __ELOG_CONFIG_PARSER_H__