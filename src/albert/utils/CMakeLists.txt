add_library(log STATIC ../log/log.cpp)
target_link_libraries(log PUBLIC Boost::log)

add_library(utils STATIC utils.cpp)
