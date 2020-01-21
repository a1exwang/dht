cmake_minimum_required(VERSION 3.0)
project(dht)

set(CMAKE_CXX_STANDARD 20)

if(CMAKE_BUILD_TYPE STREQUAL Release)
    #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
    set(Boost_USE_STATIC_LIBS ON)
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
else()
    add_definitions(-DBOOST_LOG_DYN_LINK=1)
endif()

find_package(Boost REQUIRED COMPONENTS system log)

add_library(bencoding STATIC bencoding.cpp)
add_library(krpc STATIC krpc.cpp)
target_link_libraries(krpc PUBLIC bencoding)
add_library(transaction STATIC transaction.cpp)
target_link_libraries(transaction PUBLIC krpc)
add_library(routing_table STATIC routing_table.cpp)
target_link_libraries(routing_table PUBLIC krpc)

add_library(dht STATIC dht.cpp)
target_link_libraries(dht PUBLIC krpc transaction routing_table bencoding)
target_include_directories(dht PUBLIC ${CMAKE_SOURCE_DIR})

add_executable(main main.cpp)
target_link_libraries(main PRIVATE dht routing_table transaction krpc bencoding Boost::log Boost::system pthread)
add_executable(bencoding_test_echo bencoding_test_echo.cpp)
target_link_libraries(bencoding_test_echo PRIVATE bencoding)
