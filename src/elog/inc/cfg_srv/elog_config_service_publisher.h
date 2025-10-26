#ifndef __ELOG_CONFIG_SERVICE_PUBLISHER_H__
#define __ELOG_CONFIG_SERVICE_PUBLISHER_H__

#ifdef ELOG_ENABLE_CONFIG_SERVICE

#include <condition_variable>
#include <mutex>
#include <thread>

#include "elog_config.h"
#include "elog_def.h"
#include "elog_props.h"

namespace elog {

/** @brief Parent class for all remote configuration service publishers. */
class ELOG_API ELogConfigServicePublisher {
public:
    /** @brief Loads configuration service publisher from configuration. */
    virtual bool load(const ELogConfigMapNode* cfg) = 0;

    /** @brief Loads configuration service publisher from properties. */
    virtual bool load(const ELogPropertySequence& props) = 0;

    /** @brief Initializes the configuration service publisher. */
    virtual bool initialize() = 0;

    /** @brief Terminates the configuration service publisher. */
    virtual bool terminate() = 0;

    /**
     * @brief Notifies the publisher that the remote configuration service connection details can be
     * published. Normally this means registering the configuration service at some global service
     * registry for discovery purposes.
     * @param host The interface on which the remote configuration service is listening for incoming
     * connections.
     * @param port The port number on which the remote configuration service is listening for
     * incoming connections.
     */
    virtual void onConfigServiceStart(const char* host, int port) = 0;

    /**
     * @brief Notifies the publisher that the remote configuration service is stopping. Normally
     * this means deregistering the configuration service from some global service registry for
     * discovery purposes.
     * @param host The interface on which the remote configuration service is listening for incoming
     * connections.
     * @param port The port number on which the remote configuration service is listening for
     * incoming connections.
     */
    virtual void onConfigServiceStop(const char* host, int port) = 0;

    /** @brief Retrieves the publisher's name. */
    inline const char* getName() const { return m_name.c_str(); }

protected:
    ELogConfigServicePublisher(const char* name)
        : m_name(name), m_requiresPublish(true), m_stopPublish(false) {}
    virtual ~ELogConfigServicePublisher() {}

    // utility helper functions
    // load string configuration item
    bool loadCfg(const ELogConfigMapNode* cfg, const char* propName, std::string& value,
                 bool isMandatory);

    // load integer configuration item
    bool loadIntCfg(const ELogConfigMapNode* cfg, const char* propName, uint32_t& value,
                    bool isMandatory);

    // load boolean configuration item
    bool loadBoolCfg(const ELogConfigMapNode* cfg, const char* propName, bool& value,
                     bool isMandatory);

    // load string property
    bool loadProp(const ELogPropertySequence& props, const char* propName, std::string& value,
                  bool isMandatory);

    // load integer property
    bool loadIntProp(const ELogPropertySequence& props, const char* propName, uint32_t& value,
                     bool isMandatory);

    // load boolean property
    bool loadBoolProp(const ELogPropertySequence& props, const char* propName, bool& value,
                      bool isMandatory);

    // load string configuration item, optional override from env var
    bool loadEnvCfg(const ELogConfigMapNode* cfg, const char* propName, std::string& value,
                    bool mandatory);

    // load integer configuration item, optional override from env var
    bool loadIntEnvCfg(const ELogConfigMapNode* cfg, const char* propName, uint32_t& value,
                       bool mandatory);

    // load boolean configuration item, optional override from env var
    bool loadBoolEnvCfg(const ELogConfigMapNode* cfg, const char* propName, bool& value,
                        bool mandatory);

    // load string property, optional override from env var
    bool loadEnvProp(const ELogPropertySequence& props, const char* propName, std::string& value,
                     bool mandatory);

    // load int property, optional override from env var
    bool loadIntEnvProp(const ELogPropertySequence& props, const char* propName, uint32_t& value,
                        bool mandatory);

    // load boolean property, optional override from env var
    bool loadBoolEnvProp(const ELogPropertySequence& props, const char* propName, bool& value,
                         bool mandatory);

    // starts the publish thread
    void startPublishThread(uint32_t renewExpiryTimeoutSeconds);

    // stops the publish thread
    void stopPublishThread();

    // raise requires-publish flag so publish thread calls publishConfigService() next round
    inline void setRequiresPublish() { m_requiresPublish = true; }

    // publish config service details key (first time after connect)
    virtual bool publishConfigService() = 0;

    // delete config service details key (before shutdown)
    virtual void unpublishConfigService() = 0;

    // renew expiry/ttl of config service details key
    virtual void renewExpiry() = 0;

    // query whether connected to service discovery server (key-value store)
    virtual bool isConnected() = 0;

    // connect to service discovery server (key-value store)
    virtual bool connect() = 0;

private:
    ELogConfigServicePublisher(ELogConfigServicePublisher&) = delete;
    ELogConfigServicePublisher(ELogConfigServicePublisher&&) = delete;
    ELogConfigServicePublisher& operator=(const ELogConfigServicePublisher&) = delete;

    std::string m_name;
    std::thread m_publishThread;
    std::mutex m_lock;
    std::condition_variable m_cv;
    bool m_requiresPublish;
    bool m_stopPublish;

    void publishThread(uint32_t renewExpiryTimeoutSeconds);
    void execPublishService();
};

// forward declaration
class ELOG_API ELogConfigServicePublisherConstructor;

/**
 * @brief Configuration service publisher constructor registration helper.
 * @param name The configuration service publisher identifier.
 * @param constructor The configuration service publisher constructor.
 */
extern ELOG_API void registerConfigServicePublisherConstructor(
    const char* name, ELogConfigServicePublisherConstructor* constructor);

/**
 * @brief Utility helper for constructing a configuration service publisher from type name
 * identifier.
 * @param name The configuration service publisher identifier.
 * @return ELogConfigServicePublisher* The resulting configuration service publisher, or null if
 * failed.
 */
extern ELOG_API ELogConfigServicePublisher* constructConfigServicePublisher(const char* name);

/** @brief Destroys a configuration service publisher object. */
extern ELOG_API void destroyConfigServicePublisher(ELogConfigServicePublisher* publisher);

/** @brief Utility helper class for configuration service publisher construction. */
class ELOG_API ELogConfigServicePublisherConstructor {
public:
    virtual ~ELogConfigServicePublisherConstructor() {}

    /**
     * @brief Constructs a configuration service publisher.
     * @return ELogConfigServicePublisher* The resulting configuration service publisher, or null if
     * failed.
     */
    virtual ELogConfigServicePublisher* constructConfigServicePublisher() = 0;

    /** @brief Destroys a configuration service publisher object. */
    virtual void destroyConfigServicePublisher(ELogConfigServicePublisher* publisher) = 0;

protected:
    /** @brief Constructor. */
    ELogConfigServicePublisherConstructor(const char* name) : m_publisherName(name) {
        registerConfigServicePublisherConstructor(name, this);
    }
    ELogConfigServicePublisherConstructor(const ELogConfigServicePublisherConstructor&) = delete;
    ELogConfigServicePublisherConstructor(ELogConfigServicePublisherConstructor&&) = delete;
    ELogConfigServicePublisherConstructor& operator=(const ELogConfigServicePublisherConstructor&) =
        delete;

    inline const char* getPublisherName() const { return m_publisherName.c_str(); }

private:
    std::string m_publisherName;
};

/**
 * @def Utility macro for declaring configuration service publisher factory method registration.
 * @param ConfigServicePublisherType Type name of publisher.
 * @param Name Configuration name of publisher (for dynamic loading from configuration).
 * @param ImportExportSpec Window import/export specification. If exporting from a library then
 * specify a macro that will expand correctly within the library and from outside as well. If not
 * relevant then pass ELOG_NO_EXPORT.
 */
#define ELOG_DECLARE_CONFIG_SERVICE_PUBLISHER(ConfigServicePublisherType, Name, ImportExportSpec) \
    ~ConfigServicePublisherType() final {}                                                        \
    friend class ImportExportSpec ConfigServicePublisherType##Constructor;                        \
    class ImportExportSpec ConfigServicePublisherType##Constructor final                          \
        : public elog::ELogConfigServicePublisherConstructor {                                    \
    public:                                                                                       \
        ConfigServicePublisherType##Constructor()                                                 \
            : elog::ELogConfigServicePublisherConstructor(#Name) {}                               \
        elog::ELogConfigServicePublisher* constructConfigServicePublisher() final;                \
        void destroyConfigServicePublisher(elog::ELogConfigServicePublisher* publisher) final;    \
        ~ConfigServicePublisherType##Constructor() final {}                                       \
        ConfigServicePublisherType##Constructor(const ConfigServicePublisherType##Constructor&) = \
            delete;                                                                               \
        ConfigServicePublisherType##Constructor(ConfigServicePublisherType##Constructor&&) =      \
            delete;                                                                               \
        ConfigServicePublisherType##Constructor& operator=(                                       \
            const ConfigServicePublisherType##Constructor&) = delete;                             \
    };                                                                                            \
    static ConfigServicePublisherType##Constructor sConstructor;

/**
 * @def Utility macro for implementing configuration service publisher factory method registration.
 */
#define ELOG_IMPLEMENT_CONFIG_SERVICE_PUBLISHER(ConfigServicePublisherType)          \
    ConfigServicePublisherType::ConfigServicePublisherType##Constructor              \
        ConfigServicePublisherType::sConstructor;                                    \
    elog::ELogConfigServicePublisher* ConfigServicePublisherType::                   \
        ConfigServicePublisherType##Constructor::constructConfigServicePublisher() { \
        return new (std::nothrow) ConfigServicePublisherType();                      \
    }                                                                                \
    void ConfigServicePublisherType::ConfigServicePublisherType##Constructor::       \
        destroyConfigServicePublisher(elog::ELogConfigServicePublisher* publisher) { \
        if (publisher != nullptr) {                                                  \
            delete (ConfigServicePublisherType*)publisher;                           \
        }                                                                            \
    }

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_SERVICE

#endif  // __ELOG_CONFIG_SERVICE_PUBLISHER_H__