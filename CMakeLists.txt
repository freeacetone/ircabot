cmake_minimum_required(VERSION 3.16)
project(ircabot VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network HttpServer Concurrent)

add_executable(ircabot
    src/main.cpp
    src/config.cpp
    src/config.h
    src/ircclient.cpp
    src/ircclient.h
    src/logstore.cpp
    src/logstore.h
    src/render.cpp
    src/render.h
    src/state.cpp
    src/state.h
    src/util.cpp
    src/util.h
    src/version.h
    src/webui.cpp
    src/webui.h
    assets/assets.qrc
)

target_link_libraries(ircabot PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::HttpServer
    Qt6::Concurrent
)

install(TARGETS ircabot RUNTIME DESTINATION bin)
