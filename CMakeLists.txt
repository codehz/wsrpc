cmake_minimum_required(VERSION 3.7)

project(rpc-websocket)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(OPENSSL OFF CACHE BOOL "Add OpenSSL support for wss")
set(DEMO OFF CACHE BOOL "Building demo")
add_compile_options(-Wall -Werror)

add_library(ssl INTERFACE)

if (OPENSSL)
  find_package(OpenSSL REQUIRED)
  target_link_libraries(ssl INTERFACE OpenSSL::SSL OpenSSL::Crypto)
  target_compile_definitions(ssl INTERFACE OPENSSL_ENABLED=1)
endif()

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
target_link_libraries(rpcws rpc ws ssl)
target_include_directories(rpcws PUBLIC include)
set_property(TARGET rpcws PROPERTY CXX_STANDARD 17)

if (DEMO)
  add_executable(ws_test ws/test.cpp)
  set_property(TARGET ws_test PROPERTY CXX_STANDARD 17)
  target_link_libraries(ws_test ws)

  add_executable(wsc_test ws/test-wsc.cpp)
  set_property(TARGET wsc_test PROPERTY CXX_STANDARD 17)
  target_link_libraries(wsc_test ws pthread)

  add_executable(rpcws_test
    src/test.cpp
  )
  target_link_libraries(rpcws_test rpcws)
  set_property(TARGET rpcws_test PROPERTY CXX_STANDARD 17)

  add_executable(rpcwsc_test
    src/test-client.cpp
  )
  target_link_libraries(rpcwsc_test rpcws)
  set_property(TARGET rpcwsc_test PROPERTY CXX_STANDARD 17)

  add_executable(rpcwsp_test
    src/test-proxy.cpp
  )
  target_link_libraries(rpcwsp_test rpcws)
  set_property(TARGET rpcwsp_test PROPERTY CXX_STANDARD 17)

  add_executable(promise_test
    src/test-promise.cpp
  )
  target_link_libraries(promise_test rpcws)
  set_property(TARGET promise_test PROPERTY CXX_STANDARD 17)

  if (OPENSSL)
    add_executable(rpcws_sslserver
      src/test-sslserver.cpp
    )
    target_link_libraries(rpcws_sslserver rpcws)
    set_property(TARGET rpcws_sslserver PROPERTY CXX_STANDARD 17)

    add_executable(rpcws_sslclient
      src/test-sslclient.cpp
    )
    target_link_libraries(rpcws_sslclient rpcws)
    set_property(TARGET rpcws_sslclient PROPERTY CXX_STANDARD 17)
  endif()
endif()

install(TARGETS rpc ws rpcws minsec
        RUNTIME DESTINATION bin
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
        DESTINATION include
        FILES_MATCHING PATTERN "*.h*")