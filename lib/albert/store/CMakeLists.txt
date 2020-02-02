add_library(store
        sqlite3_store.cpp
        )

target_link_libraries(store SQLite::SQLite3)