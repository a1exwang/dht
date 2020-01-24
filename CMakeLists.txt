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

include_directories(include)

find_package(Boost REQUIRED COMPONENTS system log)
add_subdirectory(utils)
add_subdirectory(bencoding)
add_subdirectory(krpc)
add_library(transaction STATIC transaction.cpp)
target_link_libraries(transaction PUBLIC krpc)
add_library(routing_table STATIC routing_table.cpp)
target_link_libraries(routing_table PUBLIC krpc)

add_subdirectory(dht)

add_executable(dht_client main.cpp)
target_link_libraries(dht_client PRIVATE dht Boost::system pthread)
add_executable(bencoding_test_echo bencoding/bencoding_test_echo.cpp)
target_link_libraries(bencoding_test_echo PRIVATE bencoding)

