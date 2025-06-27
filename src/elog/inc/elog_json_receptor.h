#ifndef __ELOG_JSON_RECEPTOR_H__
#define __ELOG_JSON_RECEPTOR_H__

#ifdef ELOG_ENABLE_JSON

#include <nlohmann/json.hpp>
#include <vector>

#include "elog_def.h"
#include "elog_field_receptor.h"

namespace elog {

class ELogJsonReceptor : public ELogFieldReceptor {
public:
    ELogJsonReceptor() {}
    ~ELogJsonReceptor() final {}

    /** @brief Receives a string log record field. */
    void receiveStringField(uint32_t typeId, const char* field, const ELogFieldSpec& fieldSpec,
                            size_t length) override {
        m_propValues.push_back(field);
    }

    /** @brief Receives an integer log record field. */
    void receiveIntField(uint32_t typeId, uint64_t field, const ELogFieldSpec& fieldSpec) override {
        m_propValues.push_back(std::to_string(field));
    }

    /** @brief Receives a time log record field. */
    void receiveTimeField(uint32_t typeId, const ELogTime& logTime, const char* timeStr,
                          const ELogFieldSpec& fieldSpec, size_t length) override {
        m_propValues.push_back(timeStr);
    }

    /** @brief Receives a log level log record field. */
    void receiveLogLevelField(uint32_t typeId, ELogLevel logLevel,
                              const ELogFieldSpec& fieldSpec) override {
        const char* logLevelStr = elogLevelToStr(logLevel);
        m_propValues.push_back(logLevelStr);
    }

    /**
     * @brief Composes the resulting JSON map object.
     * @param logAttributes The resulting log attributes as a JSON map.
     * @param propNames The property names used to compose the map. These normally come from the
     * JSON formatter.
     * @return The operation result.
     */
    bool prepareJsonMap(nlohmann::json& logAttributes, const std::vector<std::string>& propNames);

    /** @brief Retrieves the received property values. */
    inline const std::vector<std::string>& getPropValues() const { return m_propValues; }

private:
    std::vector<std::string> m_propValues;
};

}  // namespace elog

#endif  // ELOG_ENABLE_JSON

#endif  // __ELOG_JSON_RECEPTOR_H__