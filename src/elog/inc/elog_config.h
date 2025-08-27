#ifndef __ELOG_CONFIG_H__
#define __ELOG_CONFIG_H__

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "elog_def.h"
#include "elog_props.h"

/**
 * @file Configuration tree API.
 * Although this may seem like a poor man's JSON alternative, the purpose of defining this API is to
 * avoid forcing users to depend on external JSON packages.
 */

namespace elog {

/** @enum Configuration node type constants. */
enum class ELogConfigNodeType : uint32_t {
    ELOG_CONFIG_SIMPLE_NODE,  // key = value
    ELOG_CONFIG_ARRAY_NODE,   // [ ... ]
    ELOG_CONFIG_MAP_NODE      // { ... }
};

/** @brief Converts node type to string. */
extern ELOG_API const char* configNodeTypeToString(ELogConfigNodeType nodeType);

/** @enum Configuration value type constants. */
enum class ELogConfigValueType : uint32_t {
    ELOG_CONFIG_NULL_VALUE,
    ELOG_CONFIG_BOOL_VALUE,
    ELOG_CONFIG_INT_VALUE,
    ELOG_CONFIG_STRING_VALUE,
    ELOG_CONFIG_ARRAY_VALUE,
    ELOG_CONFIG_MAP_VALUE
};

/** @def Invalid parse position constant. */
#define ELOG_CONFIG_INVALID_PARSE_POS ((uint32_t)0xFFFFFFFF)

/** @brief Converts value type to string. */
extern ELOG_API const char* configValueTypeToString(ELogConfigValueType valueType);

/** @brief Common source data context used by all configuration entities. */
class ELOG_API ELogConfigSourceContext {
public:
    ELogConfigSourceContext(const char* sourceFilePath = "<input-string>")
        : m_sourceFilePath(sourceFilePath) {}
    ELogConfigSourceContext(const ELogConfigSourceContext&) = delete;
    ELogConfigSourceContext(ELogConfigSourceContext&&) = delete;
    ELogConfigSourceContext& operator=(const ELogConfigSourceContext&) = delete;
    ~ELogConfigSourceContext() {}

    /** @brief Adds input line to context. */
    inline void addLineData(uint32_t lineNumber, const char* line) {
        m_lines.push_back({lineNumber, line});
    }

    /**
     * @brief Retrieves context information by position.
     * @param pos The node's position in the source input text.
     * @param pathContext The path context of the node.
     * @return The formatted context string.
     */
    std::string getPosContext(size_t pos, const char* pathContext) const;

private:
    /** @brief Source lines as read from file. In case of a string input there is a single line. */
    std::vector<std::pair<uint32_t, std::string>> m_lines;
    std::string m_sourceFilePath;
};

/** @brief Context class for each specific configuration entity. */
class ELOG_API ELogConfigContext {
public:
    ELogConfigContext(ELogConfigSourceContext* sourceContext, size_t parsePos,
                      const char* pathContext)
        : m_sourceContext(sourceContext), m_parsePos(parsePos), m_pathContext(pathContext) {}
    ELogConfigContext(const ELogConfigContext&) = delete;
    ELogConfigContext(ELogConfigContext&&) = delete;
    ELogConfigContext& operator=(const ELogConfigContext&) = delete;
    ~ELogConfigContext() {}

    /** @brief Retrieves source intput string context data. */
    inline ELogConfigSourceContext* getSourceContext() { return m_sourceContext; }

    /** @brief Retrieves configuration tree path context data. */
    inline const char* getPathContext() { return m_pathContext.c_str(); }

    /** @brief Sets the configuration tree path context data. */
    inline void setPathContext(const char* pathContext) { m_pathContext = pathContext; }

    /** @brief Retrieves the parse position of the configuration entity. */
    inline size_t getParsePos() const { return m_parsePos; }

    /** @brief Retrieves full context information for the configuration entity. */
    const char* getFullContext() const;

private:
    ELogConfigSourceContext* m_sourceContext;
    size_t m_parsePos;
    std::string m_pathContext;
    mutable std::string m_fullContext;
};

/** @brief Parent class for all configuration entities. */
class ELOG_API ELogConfigEntity {
public:
    virtual ~ELogConfigEntity() {
        if (m_context != nullptr) {
            delete m_context;
            m_context = nullptr;
        }
    }

    /**
     * @brief Retrieves full context for the configuration entity, including source input
     * string/file context (line, column and source text), and configuration tree path context.
     */
    inline const char* getFullContext() const { return m_context->getFullContext(); }

    /** @brief Retrieves configuration tree path context. */
    inline const char* getPathContext() const { return m_context->getPathContext(); }

    /**
     * @brief Retrieves the parse position within the source file/string of this configuration
     * entity.
     */
    inline size_t getParsePos() const { return m_context->getParsePos(); }

    /** @brief Sets the configuration tree path context for this entity. */
    inline void setPathContext(const char* pathContext) {
        m_context->setPathContext(pathContext);
        onSetPathContext(pathContext);
    }

    ELogConfigContext* makeConfigContext(size_t parsePos = ELOG_CONFIG_INVALID_PARSE_POS);

protected:
    ELogConfigEntity(ELogConfigContext* context) : m_context(context) {}
    ELogConfigEntity(const ELogConfigEntity&) = delete;
    ELogConfigEntity(ELogConfigEntity&&) = delete;
    ELogConfigEntity operator=(const ELogConfigEntity&) = delete;

    /** @brief Reacts to path context changing event. Should propagate to sub-entities. */
    virtual void onSetPathContext(const char* pathContext) { (void)pathContext; }

    inline ELogConfigSourceContext* getSourceContext() { return m_context->getSourceContext(); }

private:
    ELogConfigContext* m_context;
};

/** @brief Parent class for all configuration nodes. */
class ELOG_API ELogConfigNode : public ELogConfigEntity {
public:
    ~ELogConfigNode() override {}
    inline ELogConfigNodeType getNodeType() const { return m_nodeType; }

protected:
    ELogConfigNode(ELogConfigContext* context, ELogConfigNodeType nodeType)
        : ELogConfigEntity(context), m_nodeType(nodeType) {}
    ELogConfigNode(const ELogConfigNode&) = delete;
    ELogConfigNode(ELogConfigNode&&) = delete;
    ELogConfigNode& operator=(const ELogConfigNode&) = delete;

private:
    ELogConfigNodeType m_nodeType;
};

/** @brief Parent class for all configuration values. */
class ELOG_API ELogConfigValue : public ELogConfigEntity {
public:
    ~ELogConfigValue() override {}
    inline ELogConfigValueType getValueType() const { return m_valueType; }

protected:
    ELogConfigValue(ELogConfigContext* context, ELogConfigValueType valueType)
        : ELogConfigEntity(context), m_valueType(valueType) {}
    ELogConfigValue(const ELogConfigValue&) = delete;
    ELogConfigValue(ELogConfigValue&&) = delete;
    ELogConfigValue& operator=(const ELogConfigValue&) = delete;

private:
    ELogConfigValueType m_valueType;
};

/** @brief Configuration node for simple key-value mapping, though value may be complex. */
class ELOG_API ELogConfigSimpleNode : public ELogConfigNode {
public:
    ELogConfigSimpleNode(ELogConfigContext* context, const char* key, ELogConfigValue* value)
        : ELogConfigNode(context, ELogConfigNodeType::ELOG_CONFIG_SIMPLE_NODE),
          m_key(key),
          m_value(value) {
        // propagate the path context to the mapped value
        onSetPathContext(context->getPathContext());
    }
    ELogConfigSimpleNode(const ELogConfigSimpleNode&) = delete;
    ELogConfigSimpleNode(ELogConfigSimpleNode&&) = delete;
    ELogConfigSimpleNode& operator=(const ELogConfigSimpleNode&) = delete;
    ~ELogConfigSimpleNode() override {
        if (m_value != nullptr) {
            delete m_value;
            m_value = nullptr;
        }
    }

    inline const char* getKey() const { return m_key.c_str(); }
    inline const ELogConfigValue* getValue() const { return m_value; }

protected:
    void onSetPathContext(const char* pathContext) final {
        m_value->setPathContext((std::string(pathContext) + "::" + m_key + "::<value>").c_str());
    }

private:
    std::string m_key;
    ELogConfigValue* m_value;
};

/** @brief Configuration node for array of nodes. */
class ELOG_API ELogConfigArrayNode : public ELogConfigNode {
public:
    ELogConfigArrayNode(ELogConfigContext* context)
        : ELogConfigNode(context, ELogConfigNodeType::ELOG_CONFIG_ARRAY_NODE) {}
    ELogConfigArrayNode(const ELogConfigArrayNode&) = delete;
    ELogConfigArrayNode(ELogConfigArrayNode&&) = delete;
    ELogConfigArrayNode& operator=(const ELogConfigArrayNode&) = delete;
    ~ELogConfigArrayNode() override {
        for (ELogConfigValue* value : m_values) {
            delete value;
        }
        m_values.clear();
    }

    void addValue(ELogConfigValue* value);

    inline size_t getValueCount() const { return m_values.size(); }
    inline const ELogConfigValue* getValueAt(size_t index) const { return m_values[index]; }

protected:
    void onSetPathContext(const char* pathContext) final;

private:
    std::vector<ELogConfigValue*> m_values;
    void setValuePathContext(ELogConfigValue* value, size_t index);
};

/** @brief Configuration node for mapping between keys and value, while preserving order. */
class ELOG_API ELogConfigMapNode : public ELogConfigNode {
public:
    typedef std::pair<std::string, ELogConfigValue*> EntryType;

    ELogConfigMapNode(ELogConfigContext* context)
        : ELogConfigNode(context, ELogConfigNodeType::ELOG_CONFIG_MAP_NODE) {}
    ELogConfigMapNode(const ELogConfigMapNode&) = delete;
    ELogConfigMapNode(ELogConfigMapNode&&) = delete;
    ELogConfigMapNode& operator=(const ELogConfigMapNode&) = delete;
    ~ELogConfigMapNode() override {
        for (EntryType& entry : m_entries) {
            delete entry.second;
        }
        m_entries.clear();
        m_entryMap.clear();
    }

    bool addEntry(const char* key, ELogConfigValue* value);

    bool mergeStringEntry(const char* key, const char* value);

    bool mergeIntEntry(const char* key, int64_t value);

    inline size_t getEntryCount() const { return m_entries.size(); }

    inline const EntryType& getEntryAt(size_t index) const {
        if (index >= m_entries.size()) {
            return sNullEntry;
        }
        return m_entries[index];
    }

    inline const ELogConfigValue* getValue(const char* key) const {
        const ELogConfigValue* res = nullptr;
        EntryMap::const_iterator itr = m_entryMap.find(key);
        if (itr != m_entryMap.end()) {
            res = m_entries[itr->second].second;
        }
        return res;
    }

    bool getStringValue(const char* key, bool& found, std::string& value) const;
    bool getIntValue(const char* key, bool& found, int64_t& value) const;
    bool getBoolValue(const char* key, bool& found, bool& value) const;

protected:
    void onSetPathContext(const char* pathContext) final;

private:
    std::vector<EntryType> m_entries;

    typedef std::unordered_map<std::string, size_t> EntryMap;
    EntryMap m_entryMap;
    static EntryType sNullEntry;

    void setValuePathContext(const char* key, ELogConfigValue* value);
};

/** @brief Null configuration value. */
class ELOG_API ELogConfigNullValue final : public ELogConfigValue {
public:
    ELogConfigNullValue(ELogConfigContext* context)
        : ELogConfigValue(context, ELogConfigValueType::ELOG_CONFIG_NULL_VALUE) {}
    ELogConfigNullValue(const ELogConfigNullValue&) = delete;
    ELogConfigNullValue(ELogConfigNullValue&&) = delete;
    ELogConfigNullValue& operator=(const ELogConfigNullValue&) = delete;
    ~ELogConfigNullValue() final {}
};

/** @brief Integer (signed) configuration value. */
class ELOG_API ELogConfigIntValue final : public ELogConfigValue {
public:
    ELogConfigIntValue(ELogConfigContext* context, int64_t value = 0)
        : ELogConfigValue(context, ELogConfigValueType::ELOG_CONFIG_INT_VALUE), m_value(value) {}
    ELogConfigIntValue(const ELogConfigIntValue&) = delete;
    ELogConfigIntValue(ELogConfigIntValue&&) = delete;
    ELogConfigIntValue operator=(const ELogConfigIntValue&) = delete;
    ~ELogConfigIntValue() final {}

    inline int64_t getIntValue() const { return m_value; }
    inline void setIntValue(int64_t value) { m_value = value; }

private:
    int64_t m_value;
};

/** @brief Boolean configuration value. */
class ELOG_API ELogConfigBoolValue final : public ELogConfigValue {
public:
    ELogConfigBoolValue(ELogConfigContext* context, bool value = false)
        : ELogConfigValue(context, ELogConfigValueType::ELOG_CONFIG_BOOL_VALUE), m_value(value) {}
    ELogConfigBoolValue(const ELogConfigBoolValue&) = delete;
    ELogConfigBoolValue(ELogConfigBoolValue&&) = delete;
    ELogConfigBoolValue& operator=(const ELogConfigBoolValue&) = delete;
    ~ELogConfigBoolValue() final {}

    inline bool getBoolValue() const { return m_value; }
    inline void setBoolValue(bool value) { m_value = value; }

private:
    bool m_value;
};

/** @brief String configuration value. */
class ELOG_API ELogConfigStringValue final : public ELogConfigValue {
public:
    ELogConfigStringValue(ELogConfigContext* context, const char* value = "")
        : ELogConfigValue(context, ELogConfigValueType::ELOG_CONFIG_STRING_VALUE), m_value(value) {}
    ELogConfigStringValue(const ELogConfigStringValue&) = delete;
    ELogConfigStringValue(ELogConfigStringValue&&) = delete;
    ELogConfigStringValue& operator=(const ELogConfigStringValue&) = delete;
    ~ELogConfigStringValue() final {}

    inline const char* getStringValue() const { return m_value.c_str(); }
    inline void setStringValue(const char* value) { m_value = value; }

private:
    std::string m_value;
};

/** @brief Array configuration value. */
class ELOG_API ELogConfigArrayValue final : public ELogConfigValue {
public:
    ELogConfigArrayValue(ELogConfigContext* context, ELogConfigArrayNode* value = nullptr)
        : ELogConfigValue(context, ELogConfigValueType::ELOG_CONFIG_ARRAY_VALUE), m_value(value) {}
    ELogConfigArrayValue(const ELogConfigArrayValue&) = delete;
    ELogConfigArrayValue(ELogConfigArrayValue&&) = delete;
    ELogConfigArrayValue& operator=(const ELogConfigArrayValue&) = delete;
    ~ELogConfigArrayValue() final {
        if (m_value != nullptr) {
            delete m_value;
            m_value = nullptr;
        }
    }

    inline const ELogConfigArrayNode* getArrayNode() const { return m_value; }

protected:
    void onSetPathContext(const char* context) final { m_value->setPathContext(context); }

private:
    ELogConfigArrayNode* m_value;
};

/** @brief Map configuration value. */
class ELOG_API ELogConfigMapValue final : public ELogConfigValue {
public:
    ELogConfigMapValue(ELogConfigContext* context, ELogConfigMapNode* value = nullptr)
        : ELogConfigValue(context, ELogConfigValueType::ELOG_CONFIG_MAP_VALUE), m_value(value) {}
    ELogConfigMapValue(const ELogConfigMapValue&) = delete;
    ELogConfigMapValue(ELogConfigMapValue&&) = delete;
    ELogConfigMapValue& operator=(const ELogConfigMapValue&) = delete;
    ~ELogConfigMapValue() final {
        if (m_value != nullptr) {
            delete m_value;
            m_value = nullptr;
        }
    }

    inline const ELogConfigMapNode* getMapNode() const { return m_value; }

protected:
    void onSetPathContext(const char* context) final { m_value->setPathContext(context); }

private:
    ELogConfigMapNode* m_value;
};

/** @brief Main configuration object. */
class ELOG_API ELogConfig {
public:
    ELogConfig(ELogConfigNode* root = nullptr) : m_root(root), m_sourceContext(nullptr) {}
    ELogConfig(const ELogConfig&) = delete;
    ELogConfig(ELogConfig&&) = delete;
    ELogConfig& operator=(const ELogConfig&) = delete;
    ~ELogConfig() {
        if (m_root != nullptr) {
            delete m_root;
            m_root = nullptr;
        }
        if (m_sourceContext != nullptr) {
            delete m_sourceContext;
            m_sourceContext = nullptr;
        }
    }

    static ELogConfig* loadFromFile(const char* path);
    static ELogConfig* loadFromPropFile(const char* path);
    static ELogConfig* loadFromString(const char* str);
    static ELogConfig* loadFromProps(const ELogPropertyPosSequence& props);
    static ELogConfigValue* loadValueFromProp(ELogConfigContext* context, const char* key,
                                              const ELogPropertyPos* prop);

    inline const ELogConfigNode* getRootNode() const { return m_root; }
    inline void setRootNode(ELogConfigNode* root) {
        if (m_root != nullptr) {
            delete m_root;
        }
        m_root = root;
    }

    inline std::string getContext(size_t pos, const char* pathContext) const {
        return m_sourceContext->getPosContext(pos, pathContext);
    }

    bool setSingleLineSourceContext(const char* line);
    ELogConfigSourceContext* getSourceContext() { return m_sourceContext; }

private:
    ELogConfigNode* m_root;
    ELogConfigSourceContext* m_sourceContext;

    static ELogConfig* load(const char* str, ELogConfigSourceContext* sourceContext);
};

}  // namespace elog

#endif  // __ELOG_CONFIG_H__