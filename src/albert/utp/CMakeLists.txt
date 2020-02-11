add_library(utp utp.cpp resizable_buffer.cpp)
target_link_libraries(utp PRIVATE utils log Boost::system)