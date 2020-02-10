add_library(
        cui
        cui.cpp
)

target_link_directories(cui PRIVATE bt dht Boost::system)