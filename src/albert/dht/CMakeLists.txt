add_subdirectory(routing_table)

add_library(transaction transaction.cpp)
target_link_libraries(transaction PUBLIC krpc)

add_library(
        dht
        blacklist.cpp
        config.cpp
        dht.cpp
        dht_impl.cpp
        get_peers.cpp
        helpers.cpp
        queries.cpp
        responses.cpp
        timer_handlers.cpp
        sample_infohashes/sample_infohashes_manager.cpp
)
target_link_libraries(
        dht PUBLIC
        krpc transaction routing_table bencoding public_ip log utils config rps_throttler
)


