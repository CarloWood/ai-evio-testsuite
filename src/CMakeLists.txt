include( AICxxProject )
 
find_package( Boost REQUIRED COMPONENTS system )

find_package( PkgConfig )
pkg_check_modules( Libcwd_r libcwd_r IMPORTED_TARGET )

add_executable( pipe pipe.cxx )
target_link_libraries( pipe PRIVATE ${AICXX_OBJECTS_LIST} )

add_executable( unix_socket unix_socket.cxx )
target_link_libraries( unix_socket PRIVATE ${AICXX_OBJECTS_LIST} )

add_executable( tls_socket tls_socket.cxx )
target_link_libraries( tls_socket PRIVATE ${AICXX_OBJECTS_LIST} )

add_executable( epoll_states epoll_states.cxx )
target_link_libraries( epoll_states PRIVATE Threads::Threads )

add_executable( function_size function_size.cxx )
target_link_libraries( function_size PRIVATE PkgConfig::Libcwd_r )

add_executable( sockaddr_storage sockaddr_storage.cxx )
target_link_libraries( sockaddr_storage PRIVATE AICxx::cwds )

add_executable( arpa arpa.cxx )
target_link_libraries( arpa PRIVATE ${AICXX_OBJECTS_LIST} )

add_executable( socket_address socket_address.cxx )
target_link_libraries( socket_address PRIVATE ${AICXX_OBJECTS_LIST} Boost::system )

add_executable( buffer_test buffer_test.cxx )
target_link_libraries( buffer_test PRIVATE ${AICXX_OBJECTS_LIST} )

add_executable( filedescriptor filedescriptor.cxx )
target_link_libraries( filedescriptor PRIVATE ${AICXX_OBJECTS_LIST} )

add_executable( socket_fd socket_fd.cxx )
target_link_libraries( socket_fd PRIVATE ${AICXX_OBJECTS_LIST} )

add_executable( socket socket.cxx )
target_link_libraries( socket PRIVATE ${AICXX_OBJECTS_LIST} )

add_executable( listen_socket listen_socket.cxx )
target_link_libraries( listen_socket PRIVATE ${AICXX_OBJECTS_LIST} )

add_executable( ofstream_data_test ofstream_data_test.cxx )
target_link_libraries( ofstream_data_test PRIVATE ${AICXX_OBJECTS_LIST} )

add_executable( connect connect.cxx )
target_link_libraries( connect PRIVATE ${AICXX_OBJECTS_LIST} Threads::Threads )

add_executable( signals_test signals_test.cxx )
target_link_libraries( signals_test PRIVATE AICxx::threadpool AICxx::threadsafe AICxx::utils AICxx::cwds )

add_executable( epoll_bug epoll_bug.c )

add_executable( interface interface.cxx )
target_link_libraries( interface PRIVATE ${AICXX_OBJECTS_LIST} )

# --------------- Maintainer's Section

set( GENMC_H genmc_sync_egptr.h genmc_store_last_gptr.h genmc_unused_in_last_block.h genmc_get_data_size.h )
set( GENMC_HC genmc_update_get_area.hc genmc_update_put_area.hc genmc_xsgetn_a.hc )

add_custom_command( OUTPUT genmc_sync_egptr.h
  COMMAND AWKPATH="${CMAKE_CURRENT_SOURCE_DIR}" gawk -f sync_egptr ${CMAKE_CURRENT_SOURCE_DIR}/StreamBuf.h > genmc_sync_egptr.h
)

add_custom_target( genmc
    DEPENDS genmc_buffer_reset_test.c ${GENMC_H} ${GENMC_HC}
    COMMENT Running genmc...
    COMMAND genmc -unroll=5 -- -std=c11 -I${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/genmc_buffer_reset_test.c
)

add_custom_target( genmc_all
    DEPENDS genmc_buffer_reset_test.c ${GENMC_H} ${GENMC_HC}
    BYPRODUCTS genmc_all.c
    COMMAND grep -B 1000 'INCLUDES_BEGIN' ${CMAKE_CURRENT_SOURCE_DIR}/genmc_buffer_reset_test.c > ${CMAKE_CURRENT_SOURCE_DIR}/genmc_all.c
    COMMAND cat ${GENMC_H} ${GENMC_HC} >> ${CMAKE_CURRENT_SOURCE_DIR}/genmc_all.c
    COMMAND grep -A 1000 'INCLUDES_END' ${CMAKE_CURRENT_SOURCE_DIR}/genmc_buffer_reset_test.c >> ${CMAKE_CURRENT_SOURCE_DIR}/genmc_all.c
    COMMENT Running genmc on genmc_all.c
    COMMAND genmc -unroll=5 -pretty-print-exec-graphs -print-error-trace -- -std=c11 -I${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/genmc_all.c
)

#[[ if MAINTAINER_MODE
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
#]]
