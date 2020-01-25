add_library(bt STATIC
        bt.cpp
        peer_connection.cpp)
target_link_libraries(bt PRIVATE bencoding)