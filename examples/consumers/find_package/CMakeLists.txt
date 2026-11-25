# A separate project using an installed MEMID:
#   cmake -S . -B build -DCMAKE_PREFIX_PATH=<MEMID install prefix>
cmake_minimum_required(VERSION 3.25)
project(memid_consumer_package LANGUAGES CXX)

find_package(MEMID 1.1 CONFIG REQUIRED)

add_executable(consumer ../main.cpp)
target_link_libraries(consumer PRIVATE memid::memid)
memid_embed_identity(TARGET consumer
        MOD_ID 2f1b7b0e-1b3c-4b5a-9a8e-1c2d3e4f5a6b
        ID com.example.consumer
        NAME "Package Consumer"
        VERSION 1.0.0)
