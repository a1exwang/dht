add_library(
        bencoding
        bencoding.cpp
)

add_executable(bencoding_test_echo bencoding_test_echo.cpp)
target_link_libraries(bencoding_test_echo PRIVATE bencoding)

