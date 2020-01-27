add_executable(magnet_to_torrent magnet_to_torrent.cpp)
target_link_libraries(magnet_to_torrent PRIVATE cui dht bt Boost::system)
