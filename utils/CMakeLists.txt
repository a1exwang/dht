add_library(log STATIC log.cpp)
target_link_libraries(log PUBLIC Boost::log)

add_library(public_ip STATIC public_ip.cpp)

