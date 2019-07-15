AM_CPPFLAGS = -iquote $(top_srcdir) -iquote $(top_srcdir)/cwds

bin_PROGRAMS = sockaddr_storage arpa socket_address buffer_test filedescriptor socket_fd socket listen_socket ofstream_data_test connect signals_test epoll_bug epoll_threadsafety interface

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
buffer_test_LDADD = ../evio/libevio.la ../cwds/libcwds_r.la

filedescriptor_SOURCES = filedescriptor.cxx
filedescriptor_CXXFLAGS = @LIBCWD_R_FLAGS@
filedescriptor_LDADD = ../evio/libevio.la ../threadpool/libthreadpool.la ../threadsafe/libthreadsafe.la ../utils/libutils_r.la ../cwds/libcwds_r.la

socket_fd_SOURCES = socket_fd.cxx
socket_fd_CXXFLAGS = @LIBCWD_R_FLAGS@
socket_fd_LDADD = ../evio/libevio.la ../threadpool/libthreadpool.la ../threadsafe/libthreadsafe.la ../utils/libutils_r.la ../cwds/libcwds_r.la

socket_SOURCES = socket.cxx
socket_CXXFLAGS = @LIBCWD_R_FLAGS@
socket_LDADD = ../evio/libevio.la ../threadpool/libthreadpool.la ../threadsafe/libthreadsafe.la ../utils/libutils_r.la ../cwds/libcwds_r.la

listen_socket_SOURCES = listen_socket.cxx
listen_socket_CXXFLAGS = @LIBCWD_R_FLAGS@
listen_socket_LDADD = ../evio/libevio.la ../threadpool/libthreadpool.la ../threadsafe/libthreadsafe.la ../utils/libutils_r.la ../cwds/libcwds_r.la

ofstream_data_test_SOURCES = ofstream_data_test.cxx
ofstream_data_test_CXXFLAGS = @LIBCWD_R_FLAGS@
ofstream_data_test_LDADD = ../evio/libevio.la ../threadpool/libthreadpool.la ../threadsafe/libthreadsafe.la ../utils/libutils_r.la ../cwds/libcwds_r.la

connect_SOURCES = connect.cxx
connect_CXXFLAGS = @LIBCWD_R_FLAGS@
connect_LDADD = ../evio/libevio.la ../threadpool/libthreadpool.la ../threadsafe/libthreadsafe.la ../utils/libutils_r.la ../cwds/libcwds_r.la

signals_test_SOURCES = signals_test.cxx
signals_test_CXXFLAGS = @LIBCWD_R_FLAGS@
signals_test_LDADD = ../threadpool/libthreadpool.la ../threadsafe/libthreadsafe.la ../utils/libutils_r.la ../cwds/libcwds_r.la

epoll_bug_SOURCES = epoll_bug.c
epoll_bug_CXXFLAGS =
epoll_bug_LDADD =

epoll_threadsafety_SOURCES = epoll_threadsafety.cxx
epoll_threadsafety_CXXFLAGS = @LIBCWD_R_FLAGS@
epoll_threadsafety_LDADD = ../cwds/libcwds_r.la

interface_SOURCES = interface.cxx
interface_CXXFLAGS = @LIBCWD_R_FLAGS@
interface_LDADD = ../evio/libevio.la ../threadpool/libthreadpool.la ../threadsafe/libthreadsafe.la ../utils/libutils_r.la ../cwds/libcwds_r.la

# --------------- Maintainer's Section

if MAINTAINER_MODE
GENMC_H = genmc_sync_egptr.h genmc_store_last_gptr.h genmc_unused_in_last_block.h genmc_get_data_size.h
GENMC_HC = genmc_update_get_area.hc genmc_update_put_area.hc genmc_xsgetn_a.hc

distclean-local:
	rm -f *.s *.ii ${srcdir}/genmc_all.c

clean-local:
	rm -f ${GENMC_H} ${GENMC_HC}

genmc_%.h: ${srcdir}/genmc_%.awk ${srcdir}/genmc_prelude.awk ${srcdir}/genmc_body.awk ${top_srcdir}/evio/StreamBuf.h
	AWKPATH="${srcdir}" gawk -f $< ${top_srcdir}/evio/StreamBuf.h > $@

genmc_%.hc: ${srcdir}/genmc_%.awk ${srcdir}/genmc_prelude.awk ${srcdir}/genmc_body.awk ${top_srcdir}/evio/StreamBuf.cxx
	AWKPATH="${srcdir}" gawk -f $< ${top_srcdir}/evio/StreamBuf.cxx > $@

.PHONY: genmc

genmc: genmc_buffer_reset_test.c ${GENMC_H} ${GENMC_HC}
	genmc -unroll=5 -- -std=c11 -I${builddir} ${srcdir}/genmc_buffer_reset_test.c
#	Use this to get everything in a single file for easier debugging.
#	grep -B 1000 'INCLUDES_BEGIN' ${srcdir}/genmc_buffer_reset_test.c > ${srcdir}/genmc_all.c
#	cat ${GENMC_H} ${GENMC_HC} >> ${srcdir}/genmc_all.c
#	grep -A 1000 'INCLUDES_END' ${srcdir}/genmc_buffer_reset_test.c >> ${srcdir}/genmc_all.c
#	genmc -unroll=5 -pretty-print-exec-graphs -print-error-trace -- -std=c11 -I${builddir} ${srcdir}/genmc_all.c
endif

MAINTAINERCLEANFILES = $(srcdir)/Makefile.in
