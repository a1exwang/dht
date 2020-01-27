add_executable(dht_client dht_client.cpp)
target_link_libraries(dht_client PRIVATE dht bt Boost::system)
