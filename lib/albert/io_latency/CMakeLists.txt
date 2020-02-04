add_library(io_latency io_latency.cpp)
target_link_libraries(io_latency PRIVATE Boost::system)