add_library(transaction transaction.cpp)
target_link_libraries(transaction PUBLIC krpc)

add_library(routing_table routing_table.cpp)
target_link_libraries(routing_table PUBLIC krpc)

add_library(
        dht
        dht.cpp
        dht_impl.cpp
        helpers.cpp
        queries.cpp
        responses.cpp
        timer_handlers.cpp
        get_peers.cpp
        config.cpp
        sample_infohashes/sample_infohashes_manager.cpp
)
target_link_libraries(
        dht PUBLIC
        krpc transaction routing_table bencoding public_ip log utils
        Boost::program_options
)


