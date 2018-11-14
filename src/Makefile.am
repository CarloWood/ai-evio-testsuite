AM_CPPFLAGS = -iquote $(top_srcdir) -iquote $(top_srcdir)/cwds

bin_PROGRAMS = socket sockaddr_storage arpa socket_address buffer_test threads

socket_SOURCES = socket.cxx
socket_CXXFLAGS = @LIBCWD_R_FLAGS@
socket_LDADD = ../evio/libevio.la ../threadpool/libthreadpool.la ../threadsafe/libthreadsafe.la ../utils/libutils_r.la ../evio/libev-4.24/libev.la ../cwds/libcwds_r.la

sockaddr_storage_SOURCES = sockaddr_storage.cxx
sockaddr_storage_CXXFLAGS = @LIBCWD_R_FLAGS@
sockaddr_storage_LDADD = ../cwds/libcwds_r.la

arpa_SOURCES = arpa.cxx
arpa_CXXFLAGS = @LIBCWD_R_FLAGS@
arpa_LDADD = ../evio/libevio.la ../cwds/libcwds_r.la

socket_address_SOURCES = socket_address.cxx
socket_address_CXXFLAGS = @LIBCWD_R_FLAGS@
socket_address_LDADD = ../evio/libevio.la ../cwds/libcwds_r.la -lboost_system

buffer_test_SOURCES = buffer_test.cxx
buffer_test_CXXFLAGS = @LIBCWD_R_FLAGS@
buffer_test_LDADD = ../evio/libevio.la ../evio/libev-4.24/libev.la ../cwds/libcwds_r.la

threads_SOURCES = threads.cxx
threads_CXXFLAGS = @LIBCWD_R_FLAGS@
threads_LDADD = ../evio/libevio.la ../threadpool/libthreadpool.la ../threadsafe/libthreadsafe.la ../utils/libutils_r.la ../evio/libev-4.24/libev.la ../cwds/libcwds_r.la

# --------------- Maintainer's Section

if MAINTAINER_MODE
distclean-local:
	rm -f *.s *.ii

clean-local:
endif

MAINTAINERCLEANFILES = $(srcdir)/Makefile.in
