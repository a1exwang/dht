cmake_minimum_required(VERSION 3.0)
project(dht)

set(CMAKE_CXX_STANDARD 20)
find_package(Boost COMPONENTS system)

add_library(bencoding STATIC bencoding.cpp)
add_library(krpc STATIC krpc.cpp)
target_link_libraries(krpc PUBLIC bencoding)
add_library(transaction STATIC transaction.cpp)
target_link_libraries(transaction PUBLIC krpc)

add_executable(dht dht.cpp)
target_link_libraries(dht PRIVATE transaction krpc bencoding ${BOOST_LIBRARIES} pthread)
add_executable(bencoding_test_echo bencoding_test_echo.cpp)
target_link_libraries(bencoding_test_echo PRIVATE bencoding)