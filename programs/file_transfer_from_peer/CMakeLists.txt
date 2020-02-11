add_executable(file-transfer-from-peer file_transfer_from_peer.cpp)
target_link_libraries(file-transfer-from-peer PRIVATE bt u160)

add_executable(bt-download download.cpp)
target_link_libraries(bt-download PRIVATE dht bt u160 io_latency)
