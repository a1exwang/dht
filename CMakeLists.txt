cmake_minimum_required(VERSION 3.0)
project(dht)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++")
find_package(Boost COMPONENTS system)

add_library(bencoding STATIC bencoding.cpp)
add_library(krpc STATIC krpc.cpp)
target_link_libraries(krpc PUBLIC bencoding)
add_library(transaction STATIC transaction.cpp)
target_link_libraries(transaction PUBLIC krpc)
add_library(routing_table STATIC routing_table.cpp)
target_link_libraries(routing_table PUBLIC krpc)

add_executable(dht dht.cpp)
target_link_libraries(dht PRIVATE routing_table transaction krpc bencoding ${BOOST_LIBRARIES} pthread)
add_executable(bencoding_test_echo bencoding_test_echo.cpp)
target_link_libraries(bencoding_test_echo PRIVATE bencoding)
