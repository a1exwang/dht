add_library(log STATIC ../log/log.cpp)
target_link_libraries(log PUBLIC Boost::log)

add_library(utils STATIC utils.cpp)
if(UNIX AND NOT APPLE)
    add_subdirectory(linux)
    target_link_libraries(utils PRIVATE utils_linux)
endif()
