add_library(bt
        bt.cpp
        config.cpp
        peer_connection.cpp
        peer.cpp
        torrent_resolver.cpp
        ring_buffer.cpp)
target_link_libraries(bt PRIVATE bencoding config utp common)