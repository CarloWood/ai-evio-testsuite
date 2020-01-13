cmake_minimum_required( VERSION 3.14...3.16 )

#===============================================================================
# Lets use Hunter (third party library (build) caching).

include( FetchContent )

# Fetch the hunter gate repository.
# Note that if a parent project already did a FetchContent_Declare with the same
# name then this line will be ignored. Since we require special support only add-
# ed to the CarloWood repository we therefore use a special name (CWHunterGate).
#
# For other repositories not forked from http://github.com/CarloWood/gate please
# use a different name.
set( hg_name "CWHunterGate" CACHE INTERNAL
     "The FetchContent_Declare name for HunterGate." )

string( TOLOWER ${hg_name} lc_name )
string( TOUPPER ${hg_name} uc_name )
FetchContent_Declare( ${hg_name}
    GIT_REPOSITORY "http://github.com/CarloWood/gate.git"
    GIT_TAG "9e99c6a090c8c531704965a77850a8f62be4ec99"
            # Or just use "master" to get the latest version.
)
# Allows a parent project to specify its own FetchContent_Declare for ${hg_name}.
if ( NOT ${lc_name}_POPULATED )
  # While working on gate; set FETCHCONTENT_SOURCE_DIR_HUNTERGATE to the local
  # gate-submodule. This allows to safely edit gate without having to push it
  # to github and update the above hash.
  #set( FETCHCONTENT_SOURCE_DIR_${uc_name}
  #     "${CMAKE_CURRENT_SOURCE_DIR}/gate" CACHE PATH "Path to gate submodule" )
  FetchContent_Populate( ${hg_name} )
  # Source the fetched content.
  include( ${${lc_name}_SOURCE_DIR}/cmake/HunterGate.cmake )
endif ()
# Our HunterGate allows to use GIT_REPOSITORY and specify a git repository.
# In that case, the specified SHA1 is the git commit hash that must be checked
# out (detached HEAD).
HunterGate(
    GIT_REPOSITORY "https://github.com/CarloWood/hunter.git"
    SHA1 "d17660f885ce27107364e51f38cca0b639f68ce1"
    #    FILEPATH "cwm4/cmake/hunter-config.cmake"
        # This is relative to the CMAKE_CURRENT_SOURCE_DIR
        # (the directory containing this CMakeLists.txt).
)
#===============================================================================

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
