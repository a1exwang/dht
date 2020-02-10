add_executable(magnet-to-torrent magnet_to_torrent.cpp)
target_link_libraries(magnet-to-torrent PRIVATE cui dht bt Boost::system)
install(TARGETS magnet-to-torrent)
