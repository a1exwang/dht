add_executable(torrent-to-magnet torrent_to_magnet.cpp)
target_link_libraries(torrent-to-magnet u160 bencoding Boost::system)