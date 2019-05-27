AM_CPPFLAGS = -iquote $(top_srcdir) -iquote $(top_srcdir)/cwds

bin_PROGRAMS = sockaddr_storage arpa socket_address buffer_test threads filedescriptor socket_fd socket listen_socket ofstream_data_test connect

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

filedescriptor_SOURCES = filedescriptor.cxx
filedescriptor_CXXFLAGS = @LIBCWD_R_FLAGS@
filedescriptor_LDADD = ../evio/libevio.la ../threadpool/libthreadpool.la ../threadsafe/libthreadsafe.la ../utils/libutils_r.la ../evio/libev-4.24/libev.la ../cwds/libcwds_r.la

socket_fd_SOURCES = socket_fd.cxx
socket_fd_CXXFLAGS = @LIBCWD_R_FLAGS@
socket_fd_LDADD = ../evio/libevio.la ../threadpool/libthreadpool.la ../threadsafe/libthreadsafe.la ../utils/libutils_r.la ../evio/libev-4.24/libev.la ../cwds/libcwds_r.la

socket_SOURCES = socket.cxx
socket_CXXFLAGS = @LIBCWD_R_FLAGS@
socket_LDADD = ../evio/libevio.la ../threadpool/libthreadpool.la ../threadsafe/libthreadsafe.la ../utils/libutils_r.la ../evio/libev-4.24/libev.la ../cwds/libcwds_r.la

listen_socket_SOURCES = listen_socket.cxx
listen_socket_CXXFLAGS = @LIBCWD_R_FLAGS@
listen_socket_LDADD = ../evio/libevio.la ../threadpool/libthreadpool.la ../threadsafe/libthreadsafe.la ../utils/libutils_r.la ../evio/libev-4.24/libev.la ../cwds/libcwds_r.la

ofstream_data_test_SOURCES = ofstream_data_test.cxx
ofstream_data_test_CXXFLAGS = @LIBCWD_R_FLAGS@
ofstream_data_test_LDADD = ../evio/libevio.la ../threadpool/libthreadpool.la ../threadsafe/libthreadsafe.la ../utils/libutils_r.la ../evio/libev-4.24/libev.la ../cwds/libcwds_r.la

connect_SOURCES = connect.cxx
connect_CXXFLAGS = @LIBCWD_R_FLAGS@
connect_LDADD = ../evio/libevio.la ../threadpool/libthreadpool.la ../threadsafe/libthreadsafe.la ../utils/libutils_r.la ../evio/libev-4.24/libev.la ../cwds/libcwds_r.la

# --------------- Maintainer's Section

if MAINTAINER_MODE
distclean-local:
	rm -f *.s *.ii

clean-local:
	rm -f genmc_sync_egptr.c

genmc_sync_egptr.c: ${srcdir}/genmc_sync_egptr.awk ${srcdir}/genmc_prelude.awk ${srcdir}/genmc_body.awk ${top_srcdir}/evio/StreamBuf.h
	AWKPATH="${srcdir}" gawk -f ${srcdir}/genmc_sync_egptr.awk ${top_srcdir}/evio/StreamBuf.h > genmc_sync_egptr.c

genmc_update_put_area.c: ${srcdir}/genmc_update_put_area.awk ${srcdir}/genmc_prelude.awk ${srcdir}/genmc_body.awk ${top_srcdir}/evio/StreamBuf.cxx
	AWKPATH="${srcdir}" gawk -f ${srcdir}/genmc_update_put_area.awk ${top_srcdir}/evio/StreamBuf.cxx > genmc_update_put_area.c

genmc_store_last_gptr.c: ${srcdir}/genmc_store_last_gptr.awk ${srcdir}/genmc_prelude.awk ${srcdir}/genmc_body.awk ${top_srcdir}/evio/StreamBuf.h
	AWKPATH="${srcdir}" gawk -f ${srcdir}/genmc_store_last_gptr.awk ${top_srcdir}/evio/StreamBuf.h > genmc_store_last_gptr.c

genmc_update_get_area.c: ${srcdir}/genmc_update_get_area.awk ${srcdir}/genmc_prelude.awk ${srcdir}/genmc_body.awk ${top_srcdir}/evio/StreamBuf.cxx
	AWKPATH="${srcdir}" gawk -f ${srcdir}/genmc_update_get_area.awk ${top_srcdir}/evio/StreamBuf.cxx > genmc_update_get_area.c

genmc_get_data_size.c: ${srcdir}/genmc_get_data_size.awk ${srcdir}/genmc_prelude.awk ${srcdir}/genmc_body.awk ${top_srcdir}/evio/StreamBuf.h
	AWKPATH="${srcdir}" gawk -f ${srcdir}/genmc_get_data_size.awk ${top_srcdir}/evio/StreamBuf.h > genmc_get_data_size.c

genmc_unused_in_last_block.c: ${srcdir}/genmc_unused_in_last_block.awk ${srcdir}/genmc_prelude.awk ${srcdir}/genmc_body.awk ${top_srcdir}/evio/StreamBuf.h
	AWKPATH="${srcdir}" gawk -f ${srcdir}/genmc_unused_in_last_block.awk ${top_srcdir}/evio/StreamBuf.h > genmc_unused_in_last_block.c

.PHONY: genmc

genmc: genmc_buffer_reset_test.c genmc_sync_egptr.c genmc_update_put_area.c genmc_store_last_gptr.c genmc_update_get_area.c genmc_get_data_size.c genmc_unused_in_last_block.c
	genmc -unroll=5 -- -std=c11 -I${builddir} ${srcdir}/genmc_buffer_reset_test.c
#	Use this to get everything in a single file for easier debugging.
#	grep -B 1000 '#include "genmc_sync_egptr\.c"' ${srcdir}/genmc_buffer_reset_test.c | grep -v '#include "genmc' > ${srcdir}/genmc_all.c
#	cat genmc_sync_egptr.c genmc_update_put_area.c genmc_store_last_gptr.c genmc_unused_in_last_block.c genmc_get_data_size.c genmc_update_get_area.c >> ${srcdir}/genmc_all.c
#	grep -A 1000 '#include "genmc_update_get_area\.c"' ${srcdir}/genmc_buffer_reset_test.c | grep -v '#include "genmc' >> ${srcdir}/genmc_all.c
#	genmc -unroll=5 -- -std=c11 -I${builddir} ${srcdir}/genmc_all.c
endif

MAINTAINERCLEANFILES = $(srcdir)/Makefile.in
