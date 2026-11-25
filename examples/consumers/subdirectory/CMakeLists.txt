# A separate project with MEMID's source tree as a subproject (only the static library is built then)
cmake_minimum_required(VERSION 3.25)
project(memid_consumer_subdirectory LANGUAGES CXX)

add_subdirectory(../../.. memid)

add_executable(consumer ../main.cpp)
target_link_libraries(consumer PRIVATE memid::memid)
memid_embed_identity(TARGET consumer
        MOD_ID 2f1b7b0e-1b3c-4b5a-9a8e-1c2d3e4f5a6b
        ID com.example.consumer
        NAME "Subdirectory Consumer"
        VERSION 1.0.0)
