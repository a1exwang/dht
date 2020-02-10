add_library(config config.cpp)
target_link_libraries(config PRIVATE Boost::program_options)