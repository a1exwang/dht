cmake_minimum_required(VERSION 3.0)
project(dht)

set(CMAKE_CXX_STANDARD 20)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")
endif()

if(CMAKE_BUILD_TYPE STREQUAL Release)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
    endif()
    set(Boost_USE_STATIC_LIBS ON)
    set(BUILD_SHARED_LIBS OFF)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
else()
    add_definitions(-DBOOST_LOG_DYN_LINK=1)
endif()

find_package(Boost REQUIRED COMPONENTS system log)

add_library(log STATIC log.cpp)
target_link_libraries(log PUBLIC Boost::log)

add_library(public_ip STATIC public_ip.cpp)

add_library(bencoding STATIC bencoding.cpp)
add_library(krpc STATIC krpc.cpp)
target_link_libraries(krpc PUBLIC bencoding)
add_library(transaction STATIC transaction.cpp)
target_link_libraries(transaction PUBLIC krpc)
add_library(routing_table STATIC routing_table.cpp)
target_link_libraries(routing_table PUBLIC krpc)

add_library(dht STATIC dht.cpp)
target_link_libraries(dht PUBLIC krpc transaction routing_table bencoding public_ip log)
target_include_directories(dht PUBLIC ${CMAKE_SOURCE_DIR})

add_executable(dht_client main.cpp)
target_link_libraries(dht_client PRIVATE dht Boost::system pthread)
add_executable(bencoding_test_echo bencoding_test_echo.cpp)
target_link_libraries(bencoding_test_echo PRIVATE bencoding)
