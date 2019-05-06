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

add_executable(wsc_test ws/test-wsc.cpp)
set_property(TARGET wsc_test PROPERTY CXX_STANDARD 17)
target_link_libraries(wsc_test ws pthread)

add_library(rpc
  src/rpc.cpp
  include/rpc.hpp
  include/json.hpp
)
target_include_directories(rpc PUBLIC include)
set_property(TARGET rpc PROPERTY CXX_STANDARD 17)

add_library(rpcws
  src/rpcws.cpp
  include/rpcws.hpp
)
target_link_libraries(rpcws rpc ws)
target_include_directories(rpcws PUBLIC include)
set_property(TARGET rpcws PROPERTY CXX_STANDARD 17)

add_executable(rpcws_test
  src/test.cpp
)
target_link_libraries(rpcws_test rpcws)
set_property(TARGET rpcws_test PROPERTY CXX_STANDARD 17)

add_executable(rpcwsc_test
  src/test-client.cpp
)
target_link_libraries(rpcwsc_test rpcws pthread)
set_property(TARGET rpcwsc_test PROPERTY CXX_STANDARD 17)

install(TARGETS rpc ws rpcws
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION include
        FILES_MATCHING PATTERN "*.h*")