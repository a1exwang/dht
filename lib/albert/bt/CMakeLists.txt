add_library(bt
        bt.cpp
        peer_connection.cpp
        peer.cpp
        torrent_resolver.cpp)
target_link_libraries(bt PRIVATE bencoding)