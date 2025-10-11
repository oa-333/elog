#ifndef __ELOG_CONFIG_SERVICE_PUBLISHER_H__
#define __ELOG_CONFIG_SERVICE_PUBLISHER_H__

#ifdef ELOG_ENABLE_CONFIG_SERVICE

#include "elog_def.h"

namespace elog {

/** @brief Helper class for publishing the remote configuration service. */
class ELOG_API ELogConfigServicePublisher {
public:
    virtual ~ELogConfigServicePublisher() {}

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

protected:
    ELogConfigServicePublisher() {}

private:
    ELogConfigServicePublisher(ELogConfigServicePublisher&) = delete;
    ELogConfigServicePublisher(ELogConfigServicePublisher&&) = delete;
    ELogConfigServicePublisher& operator=(const ELogConfigServicePublisher&) = delete;
};

}  // namespace elog

#endif  // ELOG_ENABLE_CONFIG_SERVICE

#endif  // __ELOG_CONFIG_SERVICE_PUBLISHER_H__