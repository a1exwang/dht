add_library(u160 u160.cpp)
target_link_libraries(u160 PRIVATE OpenSSL::Crypto OpenSSL::SSL)