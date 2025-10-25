target_sources(elog PRIVATE
    elog_api_config_service.cpp
    elog_config_server_details.cpp
    elog_config_service_client.cpp
    elog_config_service_etcd_publisher.cpp
    elog_config_service_etcd_reader.cpp
    elog_config_service_etcd_user.cpp
    elog_config_service_publisher.cpp
    elog_config_service_redis_publisher.cpp
    elog_config_service_redis_reader.cpp
    elog_config_service_redis_user.cpp
    elog_config_service_user.cpp
    elog_config_service.cpp)