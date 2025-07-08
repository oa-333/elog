if(LINUX AND ELOG_ENABLE_GRPC_CONNECTOR)
    target_sources(
        elog
        PUBLIC
            FILE_SET grpcpublicheaders
            TYPE HEADERS
            FILES
                elog.grpc.pb.h
                elog.pb.h)

    target_sources(elog PRIVATE
        ./elog.grpc.pb.cc
        ./elog.pb.cc)
endif()