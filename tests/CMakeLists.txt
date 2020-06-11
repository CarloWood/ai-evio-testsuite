include( AICxxProject )

find_package( Threads REQUIRED )
find_package( Boost CONFIG REQUIRED COMPONENTS program_options system )

add_library( libgtest STATIC )

target_sources( libgtest
    PRIVATE
        ${top_srcdir}/googletest/googletest/src/gtest-all.cc
)

target_include_directories( libgtest
    PUBLIC
        ${top_srcdir}/googletest/googletest/include
    PRIVATE
        ${top_srcdir}/googletest/googletest
    INTERFACE
        ${top_srcdir}/googletest/googlemock/include
)

# Do not use -Wundef or Winline because googletest gives loads of warnings in that case.
target_compile_options( libgtest
    PRIVATE
        -Wno-undef -Wno-inline
)

target_link_libraries( libgtest
    PUBLIC
        Threads::Threads
)

add_executable( google_test gtest.cxx )

target_link_libraries( google_test
    PRIVATE
        ${AICXX_OBJECTS_LIST}
        libgtest
        Boost::program_options
        Boost::system
)

set( TESTS google_test )
