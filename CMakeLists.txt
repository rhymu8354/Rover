# CMakeLists.txt for Rover
#
# © 2018 by Richard Walters

cmake_minimum_required(VERSION 3.8)
set(This Rover)

set(Sources
    src/main.cpp
    src/TimeKeeper.cpp
    src/TimeKeeper.hpp
    src/TlsDecorator.cpp
    src/TlsDecorator.hpp
)

add_executable(${This} ${Sources})
set_target_properties(${This} PROPERTIES
    FOLDER Applications
)

target_link_libraries(${This} PUBLIC
    Http
    HttpNetworkTransport
    SystemAbstractions
    tls ssl crypto
)

if(UNIX AND NOT APPLE)
    target_link_libraries(${This} PRIVATE
        -static-libstdc++
    )
endif(UNIX AND NOT APPLE)
