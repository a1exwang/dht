add_executable(magnet-resolver-daemon magnet_resolver_daemon.cpp)
target_link_libraries(magnet-resolver-daemon PRIVATE dht bt store signal)
install(TARGETS magnet-resolver-daemon)