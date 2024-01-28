add_subdirectory(npidl)

list(APPEND IDL_FILES_LIST idl/nprpc_base.npidl idl/nprpc_nameserver.npidl)
foreach(file ${IDL_FILES_LIST})
  string(REPLACE "/" "_" tmp ${file})
  string(REPLACE "idl/" "" filename ${file})
  string(REPLACE ".npidl" ".cpp" filename_cpp ${filename})
  string(REPLACE ".npidl" ".hpp" filename_hpp ${filename})
  add_custom_target(generate_idl_for_${tmp}
    COMMAND npidl/npidl 
      --out-inc-dir ${CMAKE_SOURCE_DIR}/nprpc/include/nprpc 
      --out-src-dir ${CMAKE_SOURCE_DIR}/nprpc/nprpcst 
      ${CMAKE_SOURCE_DIR}/nprpc/idl/${filename}
    DEPENDS npidl ${CMAKE_SOURCE_DIR}/nprpc/idl/${filename}
    BYPRODUCTS ${CMAKE_SOURCE_DIR}/nprpc/include/nprpc/${filename_hpp} 
      ${CMAKE_SOURCE_DIR}/nprpc/nprpcst/${filename_cpp}
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
  include/nprpc/nprpc.hpp
  # include/nprpc/nprpc_nameserver.cpp
  # include/nprpc/nprpc_nameserver.hpp
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

add_dependencies(nprpc npidl)
