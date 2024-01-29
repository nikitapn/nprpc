add_subdirectory(npidl)

list(APPEND IDL_FILES_LIST idl/nprpc_base.npidl idl/nprpc_nameserver.npidl)
foreach(file ${IDL_FILES_LIST})
  string(REPLACE "/" "_" tmp ${file})
  string(REPLACE "idl/" "" filename ${file})
  string(REPLACE ".npidl" ".cpp" filename_cpp ${filename})
  string(REPLACE ".npidl" ".hpp" filename_hpp ${filename})
  string(REPLACE ".npidl" ".ts" filename_ts ${filename})
  add_custom_command(
    OUTPUT ${CMAKE_SOURCE_DIR}/nprpc/include/nprpc/${filename_hpp} 
      ${CMAKE_SOURCE_DIR}/nprpc/nprpcst/${filename_cpp}
    COMMAND npidl/npidl 
      --out-inc-dir ${CMAKE_SOURCE_DIR}/nprpc/include/nprpc 
      --out-src-dir ${CMAKE_SOURCE_DIR}/nprpc/nprpcst
      --out-ts-dir ${CMAKE_SOURCE_DIR}/nprpc/web/src
      ${CMAKE_SOURCE_DIR}/nprpc/idl/${filename}
    DEPENDS npidl ${CMAKE_SOURCE_DIR}/nprpc/idl/${filename}
    BYPRODUCTS ${CMAKE_SOURCE_DIR}/nprpc/include/nprpc/${filename_hpp} 
      ${CMAKE_SOURCE_DIR}/nprpc/nprpcst/${filename_cpp}
      ${CMAKE_SOURCE_DIR}/nprpc/web/src/${filename_ts} 
  )
endforeach()

add_library(nprpc SHARED
  src/client_socket_connection.cpp
  src/framework.h
  src/nprpc.cpp
  src/server_session_socket.cpp
  src/server_session_websocket.cpp
  src/session_context.cpp
  src/session.cpp
  include/nprpc/asio.hpp
  include/nprpc/basic.hpp
  include/nprpc/buffer.hpp
  include/nprpc/endpoint.hpp
  include/nprpc/exception.hpp
  include/nprpc/export.hpp
  include/nprpc/flat.hpp
  include/nprpc/impl/nprpc_impl.hpp
  include/nprpc/impl/session.hpp
  include/nprpc/nprpc_base.hpp
  include/nprpc/nprpc_nameserver.hpp
  include/nprpc/nprpc.hpp
  include/nprpc/object_ptr.hpp
  include/nprpc/serialization/iarchive.h
  include/nprpc/serialization/nvp.hpp
  include/nprpc/serialization/oarchive.h
  include/nprpc/serialization/serialization.h
  include/nprpc/session_context.h
  include/nprpc/utils.hpp
)

target_link_libraries(nprpc PUBLIC 
  /usr/lib/libboost_system.a
  -lpthread
  -lssl
  -lcrypto
)

target_include_directories(nprpc PRIVATE ./include)
target_compile_options(nprpc PRIVATE -fPIC)

add_library(nprpcst STATIC
  include/nprpc/nprpc_nameserver.hpp
  nprpcst/nprpc_nameserver.cpp
)
target_include_directories(nprpcst PRIVATE ../include)

add_executable(npnameserver 
  npnameserver/npnameserver.cpp
)
target_include_directories(npnameserver PRIVATE ../include)

target_link_libraries(npnameserver PUBLIC nprpc nprpcst)

add_custom_target(npm_library
  COMMAND cd ${CMAKE_SOURCE_DIR}/nprpc/web && npm install && npm run build
  DEPENDS npidl 
    ${CMAKE_SOURCE_DIR}/nprpc/web/src/nprpc_base.ts 
    ${CMAKE_SOURCE_DIR}/nprpc/web/src/nprpc_nameserver.ts 
)

#add_dependencies(nprpc npm_library)
