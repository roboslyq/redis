cmake_minimum_required(VERSION 3.8)
project(redis-3.0-annotated-unstable)

set(CMAKE_C_STANDARD 99)

set(SOURCE_FILES ./src/redis.c ./src/redis.h )
add_library(redis-3.0-annotated-unstable ${SOURCE_FILES})

set(INC_DIR ./deps/lua/src ./deps/hiredis ./deps/jemalloc/src ./deps/hiredis ./deps/hiredis/adapters)

include_directories($(INC_DIR))