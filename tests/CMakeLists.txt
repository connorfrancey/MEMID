# MEMID's tests: unit tests (GoogleTest), the built example binaries inspected, the C ABI from C, the CLI, and the
# Python example (when Python 3 is found).

add_executable(memid_tests
        test_descriptor.cpp
        test_extensions.cpp
        test_custom_metadata.cpp
        test_registry.cpp
        test_inspector.cpp
        test_binaries.cpp
)
target_link_libraries(memid_tests PRIVATE memid::memid GTest::gtest_main)
target_compile_definitions(memid_tests PRIVATE
        MEMID_SCHEMA_FILE="${PROJECT_SOURCE_DIR}/schemas/memid-v1.1-layout.json"
        MEMID_EXAMPLE_MOD="$<TARGET_FILE:memid_example_mod>"
        MEMID_EXAMPLE_HELPER_MOD="$<TARGET_FILE:memid_example_helper_mod>"
        MEMID_EXAMPLE_TOOL="$<TARGET_FILE:memid_example_tool>"
        MEMID_EXAMPLE_LEGACY="$<TARGET_FILE:memid_example_legacy>"
)
add_dependencies(memid_tests memid_example_mod memid_example_helper_mod memid_example_tool memid_example_legacy)
if(MSVC)
    target_compile_options(memid_tests PRIVATE /W4 /permissive- /utf-8 /bigobj)
endif()
include(GoogleTest)
gtest_discover_tests(memid_tests DISCOVERY_MODE PRE_TEST)

if(MEMID_BUILD_SHARED)
    # The C ABI used from a C translation unit, against the shared library
    add_executable(memid_c_api_test test_c_api.c)
    target_link_libraries(memid_c_api_test PRIVATE memid_shared)
    set_target_properties(memid_c_api_test PROPERTIES C_STANDARD 99)
    add_test(NAME memid.c_api COMMAND memid_c_api_test $<TARGET_FILE:memid_example_mod> $<TARGET_FILE:memid_example_legacy>)
    set_tests_properties(memid.c_api PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:memid_shared>")
    add_test(NAME memid.example_c COMMAND memid_example_c $<TARGET_FILE:memid_example_mod>)
    set_tests_properties(memid.example_c PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:memid_shared>"
            PASS_REGULAR_EXPRESSION "Minimal Mod 1\\.2\\.3.*custom metadata: 3 entries")

    # The Python example: through the C ABI (ctypes) and by reading the format directly
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_FOUND)
        # ctypes can only load a library of Python's own bitness
        execute_process(COMMAND ${Python3_EXECUTABLE} -c "import struct; print(struct.calcsize('P'))"
                        OUTPUT_VARIABLE _memid_python_pointer OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()
    if(Python3_FOUND AND _memid_python_pointer EQUAL CMAKE_SIZEOF_VOID_P)
        add_test(NAME memid.python_ctypes
                COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/examples/python/memid_read.py
                        --library $<TARGET_FILE:memid_shared> $<TARGET_FILE:memid_example_mod>)
        set_tests_properties(memid.python_ctypes PROPERTIES PASS_REGULAR_EXPRESSION "Minimal Mod 1\\.2\\.3 \\(com\\.example\\.minimal-mod\\)")
    endif()
    if(Python3_FOUND)
        add_test(NAME memid.python_direct
                COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/examples/python/memid_read.py --direct $<TARGET_FILE:memid_example_mod>)
        set_tests_properties(memid.python_direct PROPERTIES PASS_REGULAR_EXPRESSION "Minimal Mod 1\\.2\\.3 \\(com\\.example\\.minimal-mod\\)")
    endif()
endif()

if(MEMID_BUILD_TOOLS)
    add_test(NAME memid.inspect_valid COMMAND memid_inspect --validate --custom $<TARGET_FILE:memid_example_mod>)
    add_test(NAME memid.inspect_json COMMAND memid_inspect --json --custom --decode $<TARGET_FILE:memid_example_mod>)
    set_tests_properties(memid.inspect_json PROPERTIES PASS_REGULAR_EXPRESSION "\"reverse_domain\":\"com\\.example\\.minimal-mod\".*trail_color")
    add_test(NAME memid.inspect_legacy COMMAND memid_inspect --json $<TARGET_FILE:memid_example_legacy>)
    set_tests_properties(memid.inspect_legacy PROPERTIES PASS_REGULAR_EXPRESSION "\"identity\":null.*\"identity_status\":\"not found\"")
    add_test(NAME memid.inspect_tool COMMAND memid_example_tool)
    set_tests_properties(memid.inspect_tool PROPERTIES PASS_REGULAR_EXPRESSION "MEMID Example Tool 0\\.4\\.0")
endif()
