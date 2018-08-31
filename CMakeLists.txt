# CMakeLists.txt for Rover
#
# © 2018 by Richard Walters

cmake_minimum_required(VERSION 3.8)
set(This Rover)

set(Sources
    src/main.cpp
    src/TimeKeeper.cpp
    src/TimeKeeper.hpp
)

add_executable(${This} ${Sources})
set_target_properties(${This} PROPERTIES
    FOLDER Applications
)

target_link_libraries(${This} PUBLIC
    Http
    HttpNetworkTransport
    SystemAbstractions
)

if(UNIX)
    target_link_libraries(${This} PRIVATE
        -static-libstdc++
    )
endif(UNIX)
