add_library(krpc STATIC krpc.cpp)
target_link_libraries(krpc PUBLIC u160 bencoding)
