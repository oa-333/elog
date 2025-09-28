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
        elog.grpc.pb.cc
        elog.pb.cc)

    # on Windows/MSVC we would like to disable some annoying warnings coming from generated proto files
    # NOTE: this is not working, warnings are still being issues, we leave this as is until a solution is found
    set_source_files_properties(
        elog.grpc.pb.cc
        elog.pb.cc
        PROPERTIES
        COMPILE_OPTIONS "/wd4625;/wd4626;/wd4868")
endif()