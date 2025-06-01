add_subdirectory(npidl)

include(${CMAKE_SOURCE_DIR}/nprpc/cmake/npidl.cmake)

set(IDL_FILES
  ${CMAKE_SOURCE_DIR}/nprpc/idl/nprpc_base.npidl        # core RPC files must have "nprpc_" prefix
  ${CMAKE_SOURCE_DIR}/nprpc/idl/nprpc_nameserver.npidl
)

npidl_generate_idl_files("${IDL_FILES}" nprpc_stub)

# Copy nprpc_stub generated headers to the nprpc/include/nprpc_stub directory
foreach(file ${nprpc_stub_GENERATED_HEADERS})
  get_filename_component(filename ${file} NAME)
  add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/nprpc/include/nprpc_stub/${filename}
    DEPENDS ${file}
    COMMAND ${CMAKE_COMMAND} -E make_directory
      ${CMAKE_SOURCE_DIR}/nprpc/include/nprpc_stub
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${file}
      ${CMAKE_SOURCE_DIR}/nprpc/include/nprpc_stub
    COMMENT "Copying nprpc_stub generated headers to nprpc/include/nprpc/gen"
  )
endforeach()

add_library(nprpc SHARED
  ${nprpc_stub_GENERATED_SOURCES}
  include/nprpc_stub/nprpc_base.hpp
  include/nprpc_stub/nprpc_nameserver.hpp
  src/framework.h
  src/nprpc.cpp
  src/rpc_impl.cpp
  # src/shared_memory_channel.cpp
  # src/shared_memory_connection.cpp
  include/nprpc/impl/async_connect.hpp
  src/async_connect.cpp
  src/server_session_socket.cpp
  src/http_server.cpp
  include/nprpc/impl/websocket_session.hpp
  src/server_session_websocket.cpp
  src/client_websocket_connection.cpp
  src/websocket_session.cpp
  src/client_socket_connection.cpp
  src/session_context.cpp
  src/session.cpp
  src/uuid.cpp
  src/helper.hpp
  src/helper.cpp
  include/nprpc/common.hpp
  include/nprpc/basic.hpp
  include/nprpc/buffer.hpp
  include/nprpc/endpoint.hpp
  include/nprpc/exception.hpp
  include/nprpc/export.hpp
  include/nprpc/flat.hpp
  include/nprpc/impl/nprpc_impl.hpp
  include/nprpc/impl/session.hpp
  include/nprpc/impl/uuid.hpp
  include/nprpc/nprpc.hpp
  include/nprpc/object_ptr.hpp
  include/nprpc/serialization/iarchive.h
  include/nprpc/serialization/nvp.hpp
  include/nprpc/serialization/oarchive.h
  include/nprpc/serialization/serialization.h
  include/nprpc/session_context.h
  include/nprpc/utils.hpp
)

message(STATUS "nprpc_stub_INCLUDE_DIR: ${nprpc_stub_GENERATED_HEADERS}")

target_include_directories(nprpc 
  PRIVATE 
    ./include
  )

if(WIN32)
  target_link_libraries(nprpc PUBLIC
    libssl
    libcrypto
  )
  target_compile_definitions(nprpc PRIVATE -DNPRPC_EXPORTS)
else()
  target_link_libraries(nprpc PUBLIC
  ${BOOST_LIB_PREFIX}/libboost_system.a
    pthread
    ssl
    crypto
  )
  target_compile_options(nprpc PRIVATE -fPIC)
endif()

# nameserver executable
add_executable(npnameserver
  npnameserver/npnameserver.cpp
)
target_include_directories(npnameserver 
  PRIVATE 
    ${CMAKE_SOURCE_DIR}/nprpc/include
  )
target_link_libraries(npnameserver PUBLIC nprpc)

# Test executable for shared memory reference counter
# add_executable(test_shared_memory_ref_counter
#   test_shared_memory_ref_counter.cpp
# )
# target_include_directories(test_shared_memory_ref_counter PRIVATE ${CMAKE_SOURCE_DIR}/nprpc/include/)
# target_link_libraries(test_shared_memory_ref_counter PUBLIC nprpc)

add_custom_target(npm_library
  COMMAND cd ${CMAKE_SOURCE_DIR}/nprpc/web && npm install && npm run build
  DEPENDS
    ${CMAKE_SOURCE_DIR}/nprpc/web/src/nprpc_base.ts
    ${CMAKE_SOURCE_DIR}/nprpc/web/src/nprpc_nameserver.ts
)

add_subdirectory(check)
add_subdirectory(test)