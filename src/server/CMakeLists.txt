cmake_minimum_required(VERSION 3.10)
project(video_server CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find mbedtls
find_package(PkgConfig REQUIRED)
pkg_check_modules(MBEDTLS REQUIRED mbedtls mbedcrypto)

# Source files
set(SOURCES
    main.cpp
    tcp_server.cpp
    websocket.cpp
    nal_parser.cpp
    connection.cpp
    timer.cpp
)

# Executable
add_executable(video_server ${SOURCES})

# Include directories
target_include_directories(video_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${MBEDTLS_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(video_server
    ${MBEDTLS_LIBRARIES}
    pthread
)

# Output to bin directory
set_target_properties(video_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/../../bin
)
