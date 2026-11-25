add_library(grabber STATIC grabber.c)

target_include_directories(grabber PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(grabber PUBLIC
    v4l2
    pthread
)