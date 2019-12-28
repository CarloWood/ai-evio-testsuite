cmake_minimum_required( VERSION 3.12 )

# Note: use -DHUNTER_ENABLED=NO to turn off the usage of Hunter.
include( cwm4/gate/cmake/HunterGate.cmake )
# Hunter Gate to use when Hunter is turned on.
HunterGate(
    URL "https://github.com/ruslo/hunter/archive/v0.23.214.tar.gz"
    SHA1 "e14bc153a7f16d6a5eeec845fb0283c8fad8c358"
)

project( evio-testsuite
         LANGUAGES C CXX
         DESCRIPTION "Testsuite for git submodule evio." )

# This project uses aicxx modules.
include( cwm4/cmake/AICxxProject )

# Exit if someone tries to contaminate the source directory with an in-source build.
if ( ${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR} )
  message( FATAL_ERROR "Please do out-of-source builds.\nCleanup: \"rm -rf CMake CMakeCache.txt CMakeFiles/\"" )
endif ()

# Do not Build the googlemock subproject.
set( BUILD_GMOCK OFF CACHE BOOL "" )
# Disable installation of googletest.
set( INSTALL_GTEST OFF CACHE BOOL "" )

include( AICxxSubmodules )

add_subdirectory( src )
add_subdirectory( googletest )
add_subdirectory( tests )
