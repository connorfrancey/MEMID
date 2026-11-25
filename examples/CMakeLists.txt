# The examples. The first four are also the tests' fixtures (tests/test_binaries.cpp inspects them).

# A mod DLL: identity and custom metadata from source (MEMID_DEFINE_*), plus a JSON file through the CMake helper
add_library(memid_example_mod SHARED minimal_mod/mod.cpp)
target_link_libraries(memid_example_mod PRIVATE memid::headers)
memid_add_custom_metadata(TARGET memid_example_mod
        NAMESPACE com.example.minimal-mod.settings FORMAT JSON
        FILE ${CMAKE_CURRENT_SOURCE_DIR}/minimal_mod/metadata.json)

# A mod DLL whose identity comes only from CMake (memid_embed_identity)
add_library(memid_example_helper_mod SHARED helper_mod/mod.cpp)
memid_embed_identity(TARGET memid_example_helper_mod
        MOD_ID 0f8fad5b-d9cb-469f-a165-70867728950e
        ID com.example.helper-mod
        NAME "Helper Mod"
        VERSION 2.3.4
        PUBLISHER "Example Publisher"
        CHANNEL beta
        RUNTIME mirage
        INTEROP_MIN 1
        INTEROP_MAX 3)
memid_add_custom_metadata(TARGET memid_example_helper_mod
        NAMESPACE com.example.helper-mod FORMAT TEXT
        FILE ${CMAKE_CURRENT_SOURCE_DIR}/helper_mod/notes.txt)

# An executable that embeds its identity and reads it back through its own exported getter
add_executable(memid_example_tool tool/main.cpp)
target_link_libraries(memid_example_tool PRIVATE memid::memid)

# A DLL without MEMID data: how tools see a legacy mod
add_library(memid_example_legacy SHARED legacy_mod/mod.cpp)

# A C program over the C ABI (the shared library)
if(MEMID_BUILD_SHARED)
    add_executable(memid_example_c c/read_identity.c)
    target_link_libraries(memid_example_c PRIVATE memid_shared)
    set_target_properties(memid_example_c PROPERTIES C_STANDARD 99)
endif()
