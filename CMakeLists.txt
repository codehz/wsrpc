cmake_minimum_required(VERSION 3.3)

project(rpc-websocket)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(minsec
  sec/sha1.c
  sec/sha1.h
)
target_include_directories(minsec PUBLIC sec)

add_library(ws
  ws/ws.cpp
  include/ws.hpp
)
target_link_libraries(ws minsec)
target_include_directories(ws PUBLIC include)
set_property(TARGET ws PROPERTY CXX_STANDARD 17)

add_executable(ws_test ws/test.cpp)
set_property(TARGET ws_test PROPERTY CXX_STANDARD 17)
target_link_libraries(ws_test ws)

add_library(rpc
  src/rpc.cpp
  include/rpc.hpp
  include/json.hpp
)
target_include_directories(rpc PUBLIC include)
set_property(TARGET rpc PROPERTY CXX_STANDARD 17)