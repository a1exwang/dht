add_executable(torrent-collector2 torrent_collector2.cpp)
target_link_libraries(torrent-collector2 PRIVATE dht store io_latency)
