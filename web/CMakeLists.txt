# CMakeLists.txt for the web module
#   Library (`web`) with header "web/web_server.h", plus a demo program.

add_library(web STATIC
    src/web_server.cpp
    src/WebServer.cpp
)
target_include_directories(web PUBLIC include)
target_include_directories(web SYSTEM PRIVATE third_party)
target_compile_features(web PRIVATE cxx_std_17)

# Demo with a simulated booth
add_executable(web_demo demo/web_demo.c)
target_link_libraries(web_demo LINK_PRIVATE web)

# Website files next to the programs, and on the NFS share for the board
add_custom_target(web_files ALL
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/www" "${CMAKE_CURRENT_BINARY_DIR}/www"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/demo/samples" "${CMAKE_CURRENT_BINARY_DIR}/samples"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/www" "~/cmpt433/public/myApps/www"
    COMMENT "Copying website files")
