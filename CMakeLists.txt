cmake_minimum_required(VERSION 3.14...3.16)

project(evio-testsuite
        LANGUAGES C CXX
        DESCRIPTION "Testsuite for git submodule evio.")

# Begin of gitache configuration.
set(GITACHE_PACKAGES libcwd_r)
include(cwm4/cmake/StableGitache)
# End of gitache configuration.

# This project uses aicxx modules.
include(cwm4/cmake/AICxxProject)

# Exit if someone tries to contaminate the source directory with an in-source build.
if (${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
  message(FATAL_ERROR "Please do out-of-source builds.\nCleanup: \"rm -rf CMake CMakeCache.txt CMakeFiles/\"")
endif ()

# Do not Build the googlemock subproject.
set(BUILD_GMOCK OFF CACHE BOOL "")
# Disable installation of googletest.
set(INSTALL_GTEST OFF CACHE BOOL "")

include(AICxxSubmodules)

add_subdirectory(src)
add_subdirectory(googletest)
add_subdirectory(tests)
