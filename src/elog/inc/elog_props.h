#ifndef __ELOG_PROPS_H__
#define __ELOG_PROPS_H__

#include <cstdint>
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
    size_t m_keyPos;
    size_t m_valuePos;

    ELogPropertyPos(ELogPropertyType type, size_t keyPos = 0, size_t valuePos = 0)
        : m_type(type), m_keyPos(keyPos), m_valuePos(valuePos) {}
    ELogPropertyPos(const ELogPropertyPos&) = delete;
    ELogPropertyPos(ELogPropertyPos&&) = delete;
    ELogPropertyPos& operator=(const ELogPropertyPos&) = delete;
    virtual ~ELogPropertyPos() {}
};

/** @struct A property value with source text position. */
struct ELOG_API ELogStringPropertyPos : public ELogPropertyPos {
    std::string m_value;

    ELogStringPropertyPos(const char* value = "", size_t keyPos = 0, size_t valuePos = 0)
        : ELogPropertyPos(ELogPropertyType::PT_STRING, keyPos, valuePos), m_value(value) {}
    ELogStringPropertyPos(const ELogStringPropertyPos&) = delete;
    ELogStringPropertyPos(ELogStringPropertyPos&&) = delete;
    ELogStringPropertyPos& operator=(const ELogStringPropertyPos&) = delete;
    ~ELogStringPropertyPos() final {}
};

/** @struct A property value with source text position. */
struct ELOG_API ELogIntPropertyPos : public ELogPropertyPos {
    int64_t m_value;

    ELogIntPropertyPos(int64_t value = 0, size_t keyPos = 0, size_t valuePos = 0)
        : ELogPropertyPos(ELogPropertyType::PT_INT, keyPos, valuePos), m_value(value) {}
    ELogIntPropertyPos(const ELogIntPropertyPos&) = delete;
    ELogIntPropertyPos(ELogIntPropertyPos&&) = delete;
    ELogIntPropertyPos& operator=(const ELogIntPropertyPos&) = delete;
    ~ELogIntPropertyPos() final {}
};

/** @struct A property value with source text position. */
struct ELOG_API ELogBoolPropertyPos : public ELogPropertyPos {
    bool m_value;

    ELogBoolPropertyPos(bool value = false, size_t keyPos = 0, size_t valuePos = 0)
        : ELogPropertyPos(ELogPropertyType::PT_BOOL, keyPos, valuePos), m_value(value) {}
    ELogBoolPropertyPos(const ELogBoolPropertyPos&) = delete;
    ELogBoolPropertyPos(ELogBoolPropertyPos&&) = delete;
    ELogBoolPropertyPos& operator=(const ELogBoolPropertyPos&) = delete;
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