cmake_minimum_required(VERSION 3.25)
project(MEMID
        VERSION 1.1.0
        DESCRIPTION "MEMID, the Mirror's Edge Mod Identifier: an identity format embedded in binaries"
        LANGUAGES CXX
)

# ======================================================================================================================
# Options (standalone: everything on; as a subproject: only the static library)
# ======================================================================================================================

option(MEMID_BUILD_STATIC   "Build the static library (memid::memid_static)" ON)
option(MEMID_BUILD_SHARED   "Build the shared library (memid::memid_shared)" ${PROJECT_IS_TOP_LEVEL})
option(MEMID_BUILD_TOOLS    "Build memid-inspect" ${PROJECT_IS_TOP_LEVEL})
option(MEMID_BUILD_TESTS    "Build the tests" ${PROJECT_IS_TOP_LEVEL})
option(MEMID_BUILD_EXAMPLES "Build the examples" ${PROJECT_IS_TOP_LEVEL})
option(MEMID_ENABLE_INSTALL "Install headers, libraries, tools and the CMake package" ${PROJECT_IS_TOP_LEVEL})

if(NOT MEMID_BUILD_STATIC AND NOT MEMID_BUILD_SHARED)
    message(FATAL_ERROR "MEMID: MEMID_BUILD_STATIC or MEMID_BUILD_SHARED must be ON")
endif()

include(GNUInstallDirs)
include(${CMAKE_CURRENT_LIST_DIR}/cmake/MEMIDHelpers.cmake)

# ======================================================================================================================
# Libraries
# ======================================================================================================================

# The headers alone: enough to embed an identity (memid/embed.hpp) without linking the library
add_library(memid_headers INTERFACE)
add_library(memid::headers ALIAS memid_headers)
target_include_directories(memid_headers INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_compile_features(memid_headers INTERFACE cxx_std_20)
set_target_properties(memid_headers PROPERTIES EXPORT_NAME headers)

set(MEMID_SOURCES
        src/descriptor.cpp
        src/extensions.cpp
        src/custom_metadata.cpp
        src/binary_inspector.cpp
        src/registry.cpp
        src/c_api.cpp
)

function(memid_library_settings target)
    target_link_libraries(${target} PUBLIC memid_headers)
    if(MSVC)
        # C4251: std members of exported classes (both sides use the same compiler and standard library)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /wd4251)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

set(_memid_targets memid_headers)

if(MEMID_BUILD_STATIC)
    add_library(memid_static STATIC ${MEMID_SOURCES})
    add_library(memid::memid_static ALIAS memid_static)
    memid_library_settings(memid_static)
    target_compile_definitions(memid_static PUBLIC MEMID_STATIC)
    set_target_properties(memid_static PROPERTIES OUTPUT_NAME memid_static EXPORT_NAME memid_static
                                                  POSITION_INDEPENDENT_CODE ON)
    list(APPEND _memid_targets memid_static)
endif()

if(MEMID_BUILD_SHARED)
    # Built separately from the static library: on Windows its objects need dllexport (MEMID_BUILDING)
    add_library(memid_shared SHARED ${MEMID_SOURCES})
    add_library(memid::memid_shared ALIAS memid_shared)
    memid_library_settings(memid_shared)
    target_compile_definitions(memid_shared PRIVATE MEMID_BUILDING)
    set_target_properties(memid_shared PROPERTIES
            OUTPUT_NAME memid EXPORT_NAME memid_shared
            CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
            VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
    list(APPEND _memid_targets memid_shared)
endif()

# memid::memid: the static library when it is built, else the shared one
add_library(memid_default INTERFACE)
add_library(memid::memid ALIAS memid_default)
set_target_properties(memid_default PROPERTIES EXPORT_NAME memid)
if(MEMID_BUILD_STATIC)
    target_link_libraries(memid_default INTERFACE memid_static)
else()
    target_link_libraries(memid_default INTERFACE memid_shared)
endif()
list(APPEND _memid_targets memid_default)

# ======================================================================================================================
# Tools
# ======================================================================================================================

if(MEMID_BUILD_TOOLS)
    add_executable(memid_inspect tools/memid_inspect.cpp)
    add_executable(memid::memid_tools ALIAS memid_inspect)
    target_link_libraries(memid_inspect PRIVATE memid_default)
    set_target_properties(memid_inspect PROPERTIES OUTPUT_NAME memid-inspect EXPORT_NAME memid_inspect)
    if(MSVC)
        target_compile_options(memid_inspect PRIVATE /W4 /permissive- /utf-8)
    endif()
    list(APPEND _memid_targets memid_inspect)
endif()

# ======================================================================================================================
# Examples and tests
# ======================================================================================================================

if(MEMID_BUILD_EXAMPLES OR MEMID_BUILD_TESTS)
    if(MEMID_BUILD_SHARED)
        enable_language(C)	# The C example and the C ABI test (C_API.md's claim that the header is plain C)
    endif()
    add_subdirectory(examples)
endif()

if(MEMID_BUILD_TESTS)
    enable_testing()
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/CPM.cmake)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/Dependencies.cmake)
    add_subdirectory(tests)
endif()

# ======================================================================================================================
# Install
# ======================================================================================================================

if(MEMID_ENABLE_INSTALL)
    include(CMakePackageConfigHelpers)
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(TARGETS ${_memid_targets}
            EXPORT MEMIDTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    set(_memid_cmake_dir ${CMAKE_INSTALL_LIBDIR}/cmake/MEMID)
    install(EXPORT MEMIDTargets NAMESPACE memid:: DESTINATION ${_memid_cmake_dir})
    configure_package_config_file(cmake/MEMIDConfig.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/MEMIDConfig.cmake
                                  INSTALL_DESTINATION ${_memid_cmake_dir})
    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/MEMIDConfigVersion.cmake
                                      COMPATIBILITY SameMajorVersion)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/MEMIDConfig.cmake ${CMAKE_CURRENT_BINARY_DIR}/MEMIDConfigVersion.cmake
                  cmake/MEMIDHelpers.cmake
            DESTINATION ${_memid_cmake_dir})
endif()
