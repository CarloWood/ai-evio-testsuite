include( AICxxProject )

find_package( Threads REQUIRED )

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
target_link_libraries( socket_address PRIVATE ${AICXX_OBJECTS_LIST} )

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
target_link_libraries( connect PRIVATE ${AICXX_OBJECTS_LIST} )

add_executable( signals_test signals_test.cxx )
target_link_libraries( signals_test PRIVATE AICxx::threadpool AICxx::threadsafe AICxx::utils AICxx::cwds )

add_executable( interface interface.cxx )
target_link_libraries( interface PRIVATE ${AICXX_OBJECTS_LIST} )

add_executable( epoll_bug epoll_bug.c )

# --------------- Maintainer's Section

set( GENMC_H genmc_sync_egptr.h genmc_store_last_gptr.h genmc_unused_in_last_block.h genmc_get_data_size.h )
set( GENMC_HC genmc_update_get_area.hc genmc_update_put_area.hc genmc_xsgetn_a.hc )

foreach( h_file ${GENMC_H} )
  string( REPLACE ".h" ".awk" awk_file ${h_file} )
  add_custom_command( OUTPUT ${h_file}
    COMMAND env AWKPATH="${CMAKE_CURRENT_SOURCE_DIR}" gawk -f "${CMAKE_CURRENT_SOURCE_DIR}/${awk_file}"
        "${top_srcdir}/evio/StreamBuf.h" > ${h_file}
  )
endforeach()

foreach( hc_file ${GENMC_HC} )
  string( REPLACE ".hc" ".awk" awk_file ${hc_file} )
  add_custom_command( OUTPUT ${hc_file}
    COMMAND env AWKPATH="${CMAKE_CURRENT_SOURCE_DIR}" gawk -f "${CMAKE_CURRENT_SOURCE_DIR}/${awk_file}"
        "${top_srcdir}/evio/StreamBuf.cxx" > ${hc_file}
  )
endforeach()

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
