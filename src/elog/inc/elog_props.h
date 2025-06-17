#ifndef __ELOG_PROPS_H__
#define __ELOG_PROPS_H__

#include <string>
#include <unordered_map>
#include <vector>

#include "elog_def.h"

namespace elog {

/** @brief A single property (key-value pair). */
typedef std::pair<std::string, std::string> ELogProperty;

/** @typedef Property sequence (order matters). */
typedef std::vector<ELogProperty> ELogPropertySequence;

/** @typedef Property map. */
typedef std::unordered_map<std::string, std::string> ELogPropertyMap;

/** @brief Property type constants. */
enum class ELogPropertyType : uint32_t {
    /** @brief Stirng property type. */
    PT_STRING,

    /** @brief Integer property type. */
    PT_INT,

    /** @brief Boolean property type. */
    PT_BOOL
};

/** @struct A property value with source text position. */
struct ELOG_API ELogPropertyPos {
    ELogPropertyType m_type;
    uint32_t m_keyPos;
    uint32_t m_valuePos;

    ELogPropertyPos(ELogPropertyType type, uint32_t keyPos = 0, uint32_t valuePos = 0)
        : m_type(type), m_keyPos(keyPos), m_valuePos(valuePos) {}
    virtual ~ELogPropertyPos() {}
};

/** @struct A property value with source text position. */
struct ELOG_API ELogStringPropertyPos : public ELogPropertyPos {
    std::string m_value;

    ELogStringPropertyPos(const char* value = "", uint32_t keyPos = 0, uint32_t valuePos = 0)
        : ELogPropertyPos(ELogPropertyType::PT_STRING, keyPos, valuePos), m_value(value) {}
    ~ELogStringPropertyPos() final {}
};

/** @struct A property value with source text position. */
struct ELOG_API ELogIntPropertyPos : public ELogPropertyPos {
    int m_value;

    ELogIntPropertyPos(int value = 0, uint32_t keyPos = 0, uint32_t valuePos = 0)
        : ELogPropertyPos(ELogPropertyType::PT_INT, keyPos, valuePos), m_value(value) {}
    ~ELogIntPropertyPos() final {}
};

/** @struct A property value with source text position. */
struct ELOG_API ELogBoolPropertyPos : public ELogPropertyPos {
    bool m_value;

    ELogBoolPropertyPos(bool value = false, uint32_t keyPos = 0, uint32_t valuePos = 0)
        : ELogPropertyPos(ELogPropertyType::PT_BOOL, keyPos, valuePos), m_value(value) {}
    ~ELogBoolPropertyPos() final {}
};

struct ELogPropertyPosSequence {
    ~ELogPropertyPosSequence() {
        for (auto& entry : m_sequence) {
            delete entry.second;
        }
        m_sequence.clear();
    }
    std::vector<std::pair<std::string, ELogPropertyPos*>> m_sequence;
};

struct ELogPropertyPosMap {
    ~ELogPropertyPosMap() {
        for (auto& entry : m_map) {
            delete entry.second;
        }
        m_map.clear();
    }

    typedef std::unordered_map<std::string, ELogPropertyPos*> MapType;
    MapType m_map;
};

/** @typedef Property map with source text position information. */
// typedef std::vector<std::pair<std::string, ELogPropertyPos*>> ELogPropertyPosSequence;

/** @typedef Property map with source text position information. */
// typedef std::unordered_map<std::string, ELogPropertyPos*> ELogPropertyPosMap;

}  // namespace elog

#endif  // __ELOG_PROPS_H__