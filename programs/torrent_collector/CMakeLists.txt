add_executable(torrent-collector torrent_collector.cpp)
target_link_libraries(torrent-collector PRIVATE cui bt dht)