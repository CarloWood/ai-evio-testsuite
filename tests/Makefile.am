AM_CPPFLAGS = -iquote $(top_srcdir) -iquote $(top_srcdir)/cwds

check_LTLIBRARIES = libgtest.la
check_PROGRAMS = gtest

# Do not use -Wundef because googletest gives loads of warnings in that case.
CXXFLAGS = -std=c++17 -g -Wall -Woverloaded-virtual -Wpointer-arith -Wwrite-strings -Winline

libgtest_la_SOURCES = ../googletest/googletest/src/gtest-all.cc
libgtest_la_CXXFLAGS = -I$(top_srcdir)/googletest/googletest/include -I$(top_srcdir)/googletest/googletest -pthread

gtest_SOURCES = gtest.cxx
gtest_CXXFLAGS = -I$(top_srcdir)/googletest/googletest/include -I$(top_srcdir)/googletest/googlemock/include @LIBCWD_R_FLAGS@
gtest_LDADD = ../evio/libevio.la ../threadpool/libthreadpool.la ../threadsafe/libthreadsafe.la ../utils/libutils_r.la ../evio/libev-4.24/libev.la libgtest.la ../cwds/libcwds_r.la

TESTS = gtest

# --------------- Maintainer's Section

if MAINTAINER_MODE
distclean-local:
	rm -f *.s *.ii

clean-local:
endif

MAINTAINERCLEANFILES = $(srcdir)/Makefile.in
