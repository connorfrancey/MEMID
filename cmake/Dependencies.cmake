# ======================================================================================================================
# Dependencies.cmake - third-party dependencies (tests only: the library itself has none)
# ======================================================================================================================

include_guard(GLOBAL)

if(MEMID_BUILD_TESTS)
    # The CRT GoogleTest links must match the tests': follow CMAKE_MSVC_RUNTIME_LIBRARY when it asks for the static one
    set(_memid_gtest_shared_crt ON)
    if(CMAKE_MSVC_RUNTIME_LIBRARY AND NOT CMAKE_MSVC_RUNTIME_LIBRARY MATCHES "DLL")
        set(_memid_gtest_shared_crt OFF)
    endif()
    CPMAddPackage(
            NAME googletest
            GITHUB_REPOSITORY google/googletest
            GIT_TAG v1.17.0
            OPTIONS
                "BUILD_GMOCK OFF"
                "INSTALL_GTEST OFF"
                "gtest_force_shared_crt ${_memid_gtest_shared_crt}"
    )
endif()
