if(MSVC AND ELOG_USING_PROTOBUF)
    target_sources(
        elog
        PUBLIC
            FILE_SET grpcpublicheaders
            TYPE HEADERS
            FILES
                elog.grpc.pb.h
                elog.pb.h)

    target_sources(elog PRIVATE
        elog_proto.cpp)
endif()