# echo-memory: the on-device memory & recall engine (Phase 15).
#
# The first module whose entire purpose is the product's actual differentiator —
# a local, private store of people, reminders, and a narrow event log, backed by
# real SQLite persistence. Unlike the Phase 3 AI engines this is NOT behind an
# ECHO_WITH_* flag: persistence across restarts is the whole point, so it is
# always built, and it stays dependency-free (the SQLite amalgamation is vendored
# in-tree) so the stub build remains green.

# --- Vendored SQLite ---------------------------------------------------------
# Compiled as its own tiny static lib so it does NOT inherit echo-project-options'
# -Wall -Wextra -Wpedantic / -Werror-adjacent policy (it is upstream C we do not
# lint). See memory/vendor/sqlite3/README.md and ADR-13.
add_library(echo-sqlite3 STATIC vendor/sqlite3/sqlite3.c)
target_include_directories(echo-sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sqlite3)
target_compile_definitions(echo-sqlite3 PRIVATE
    SQLITE_OMIT_LOAD_EXTENSION
    SQLITE_THREADSAFE=1
    SQLITE_DQS=0
    SQLITE_DEFAULT_MEMSTATUS=0
)
if(NOT MSVC)
    target_compile_options(echo-sqlite3 PRIVATE -w)   # do not warn on vendored C
endif()

# --- The memory engine -------------------------------------------------------
add_library(echo-memory STATIC
    src/memory_engine.cpp
    src/utterance.cpp
    src/aes256.cpp       # Phase 16: dependency-free AES-256 for encryption at rest
    src/device_key.cpp   # Phase 16: per-device key management
)

target_include_directories(echo-memory PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(echo-memory
    PUBLIC  echo::common
    PRIVATE echo-project-options echo-sqlite3   # sqlite stays a private impl detail
)

add_library(echo::memory ALIAS echo-memory)
