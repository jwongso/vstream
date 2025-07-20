cmake_minimum_required(VERSION 3.20)
project(vstream_qt_client VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Qt6 REQUIRED COMPONENTS Core Widgets WebSockets)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PORTAUDIO REQUIRED portaudio-2.0)
find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)  # Add this for proper thread support

# Qt6 automatic handling
qt6_standard_project_setup()

# Source files
set(SOURCES
    main.cpp
    mainwindow.cpp
    audio_capture.cpp
    websocket_client.cpp
)

set(HEADERS
    mainwindow.h
    audio_capture.h
    websocket_client.h
)

# Create executable
qt6_add_executable(vstream_qt_client ${SOURCES} ${HEADERS})

# Qt6 resources (if needed)
# qt6_add_resources(vstream_qt_client "resources" FILES ...)

target_include_directories(vstream_qt_client PRIVATE
    ${PORTAUDIO_INCLUDE_DIRS}
)

target_link_libraries(vstream_qt_client PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::WebSockets
    ${PORTAUDIO_LIBRARIES}
    nlohmann_json::nlohmann_json
    Threads::Threads  # Add proper thread support
)

# Fix: Use compile options instead of definitions for flags
target_compile_options(vstream_qt_client PRIVATE
    ${PORTAUDIO_CFLAGS_OTHER}
)

# If you need pthread specifically, add it properly
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_options(vstream_qt_client PRIVATE -pthread)
    target_link_options(vstream_qt_client PRIVATE -pthread)
endif()

# Installation
install(TARGETS vstream_qt_client
    RUNTIME DESTINATION bin
)

message(STATUS "Qt6 vstream client example will be built")
