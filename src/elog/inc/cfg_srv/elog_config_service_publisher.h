#ifndef __ELOG_CONFIG_SERVICE_PUBLISHER_H__
#define __ELOG_CONFIG_SERVICE_PUBLISHER_H__

#ifdef ELOG_ENABLE_CONFIG_SERVICE

#include "elog_config.h"
#include "elog_def.h"
#include "elog_props.h"

namespace elog {

/** @brief Helper class for publishing the remote configuration service. */
class ELOG_API ELogConfigServicePublisher {
public:
    virtual ~ELogConfigServicePublisher() {}

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

    inline const char* getName() const { return m_name.c_str(); }

protected:
    ELogConfigServicePublisher(const char* name) : m_name(name) {}

private:
    ELogConfigServicePublisher(ELogConfigServicePublisher&) = delete;
    ELogConfigServicePublisher(ELogConfigServicePublisher&&) = delete;
    ELogConfigServicePublisher& operator=(const ELogConfigServicePublisher&) = delete;

    std::string m_name;
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

protected:
    /** @brief Constructor. */
    ELogConfigServicePublisherConstructor(const char* name) {
        registerConfigServicePublisherConstructor(name, this);
    }
    ELogConfigServicePublisherConstructor(const ELogConfigServicePublisherConstructor&) = delete;
    ELogConfigServicePublisherConstructor(ELogConfigServicePublisherConstructor&&) = delete;
    ELogConfigServicePublisherConstructor& operator=(const ELogConfigServicePublisherConstructor&) =
        delete;
};

// TODO: for sake of being able to externally extend elog, the ELOG_API should be replaced with
// macro parameter, so it can be set to dll export, or to nothing

/** @def Utility macro for declaring configuration service publisher factory method registration. */
#define ELOG_DECLARE_CONFIG_SERVICE_PUBLISHER(ConfigServicePublisherType, Name)                   \
    class ELOG_API ConfigServicePublisherType##Constructor final                                  \
        : public elog::ELogConfigServicePublisherConstructor {                                    \
    public:                                                                                       \
        ConfigServicePublisherType##Constructor()                                                 \
            : elog::ELogConfigServicePublisherConstructor(#Name) {}                               \
        elog::ELogConfigServicePublisher* constructConfigServicePublisher() final {               \
            return new (std::nothrow) ConfigServicePublisherType();                               \
        }                                                                                         \
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
#define ELOG_IMPLEMENT_CONFIG_SERVICE_PUBLISHER(ConfigServicePublisherType) \
    ConfigServicePublisherType::ConfigServicePublisherType##Constructor     \
        ConfigServicePublisherType::sConstructor;

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_SERVICE

#endif  // __ELOG_CONFIG_SERVICE_PUBLISHER_H__