add_library(dht STATIC
        dht.cpp
        dht_impl.cpp
        helpers.cpp
        queries.cpp
        responses.cpp
        timer_handlers.cpp
        get_peers.cpp
        )
target_link_libraries(dht PUBLIC krpc transaction routing_table bencoding public_ip log)

