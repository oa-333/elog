
target_sources(
    elog
    PUBLIC
        FILE_SET publicheaders
        TYPE HEADERS
        FILES
            elog_buffered_file_target.h
            elog_buffered_file_writer.h
            elog_file_target.h
            elog_segmented_file_target.h)