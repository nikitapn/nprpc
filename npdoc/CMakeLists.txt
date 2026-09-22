# npdoc - collects the public API and its doc comments from the C++ headers,
# the Swift package and the IDL files into one JSON model.

find_path(NPDOC_LIBCLANG_INCLUDE_DIR clang-c/Index.h)
find_library(NPDOC_LIBCLANG_LIBRARY NAMES clang libclang)

if(NOT NPDOC_LIBCLANG_INCLUDE_DIR OR NOT NPDOC_LIBCLANG_LIBRARY)
  message(STATUS "npdoc: libclang not found, API docs tool disabled")
  return()
endif()

find_path(NPDOC_MD4C_INCLUDE_DIR md4c-html.h)
find_library(NPDOC_MD4C_LIBRARY md4c)
find_library(NPDOC_MD4C_HTML_LIBRARY md4c-html)
if(NOT NPDOC_MD4C_INCLUDE_DIR OR NOT NPDOC_MD4C_LIBRARY OR NOT NPDOC_MD4C_HTML_LIBRARY)
  message(STATUS "npdoc: md4c not found, API docs tool disabled")
  return()
endif()

add_library(npdoc_core STATIC
  src/cpp_extract.cpp
  src/cpp_extract.hpp
  src/doc_text.cpp
  src/doc_text.hpp
  src/idl_extract.cpp
  src/idl_extract.hpp
  src/json_io.hpp
  src/markdown.cpp
  src/markdown.hpp
  src/model.hpp
  src/swift_extract.cpp
  src/swift_extract.hpp
)

target_include_directories(npdoc_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/glaze/include
  PRIVATE
    ${NPDOC_LIBCLANG_INCLUDE_DIR}
    ${NPDOC_MD4C_INCLUDE_DIR}
)
target_link_libraries(npdoc_core PRIVATE
  ${NPDOC_LIBCLANG_LIBRARY} ${NPDOC_MD4C_HTML_LIBRARY} ${NPDOC_MD4C_LIBRARY})
target_compile_features(npdoc_core PUBLIC cxx_std_23)

if(NOT TARGET Boost::program_options)
  if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
  endif()
  find_package(Boost QUIET COMPONENTS program_options)
endif()
if(NOT TARGET Boost::program_options)
  message(STATUS "npdoc: Boost.ProgramOptions not found, API docs tool disabled")
  return()
endif()

add_executable(npdoc src/main.cpp)
target_link_libraries(npdoc PRIVATE npdoc_core Boost::program_options)

# --- docs_api: build/docs/api.json from this repository ---------------------
#
# The C++ headers and the IDL are handled here. Swift needs its own compiler
# run (see `just docs-api`); point NPRPC_DOCS_SWIFT_SYMBOLS at the resulting
# NPRPC.symbols.json to include it.
set(NPRPC_DOCS_SWIFT_SYMBOLS "" CACHE STRING
    "Swift <Module>.symbols.json files (;-separated) to merge into build/docs/api.json (optional)")

if(TARGET npidl AND TARGET nprpc)
  set(NPDOC_OUT_DIR ${CMAKE_BINARY_DIR}/docs)
  set(NPDOC_IDL_FILES
    ${CMAKE_SOURCE_DIR}/idl/nprpc_base.npidl
    ${CMAKE_SOURCE_DIR}/idl/nprpc_nameserver.npidl
  )

  # libclang looks for its builtin headers next to the clang binary it came
  # with; asking the compiler that matches the library is the reliable way.
  find_program(NPDOC_CLANG clang)
  set(NPDOC_RESOURCE_ARGS)
  if(NPDOC_CLANG)
    execute_process(COMMAND ${NPDOC_CLANG} -print-resource-dir
                    OUTPUT_VARIABLE NPDOC_RESOURCE_DIR
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NPDOC_RESOURCE_DIR)
      set(NPDOC_RESOURCE_ARGS --clang-arg=-resource-dir=${NPDOC_RESOURCE_DIR})
    endif()
  endif()

  set(NPDOC_INCLUDES "$<TARGET_PROPERTY:nprpc,INCLUDE_DIRECTORIES>")
  set(NPDOC_DEFINES "$<TARGET_PROPERTY:nprpc,COMPILE_DEFINITIONS>")

  set(NPDOC_IDL_ARGS)
  foreach(idl ${NPDOC_IDL_FILES})
    get_filename_component(stem ${idl} NAME_WE)
    list(APPEND NPDOC_IDL_ARGS --idl ${NPDOC_OUT_DIR}/${stem}.doc.json)
  endforeach()

  # Rerun when any public header changes, not only when the IDL does.
  file(GLOB_RECURSE NPDOC_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/include/nprpc/*.hpp
    ${CMAKE_SOURCE_DIR}/include/nprpc/*.h
  )
  file(GLOB NPDOC_GUIDES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/docs/*.md)

  set(NPDOC_SWIFT_ARGS)
  foreach(graph ${NPRPC_DOCS_SWIFT_SYMBOLS})
    list(APPEND NPDOC_SWIFT_ARGS --swift-symbols ${graph})
  endforeach()
  if(NPDOC_SWIFT_ARGS)
    # Generated/ is npidl output; the IDL entries document those types.
    list(APPEND NPDOC_SWIFT_ARGS --swift-exclude nprpc_swift/Sources/NPRPC/Generated/)
  endif()

  add_custom_command(
    OUTPUT ${NPDOC_OUT_DIR}/api.json
    COMMAND ${CMAKE_COMMAND} -E make_directory ${NPDOC_OUT_DIR}
    COMMAND $<TARGET_FILE:npidl> --doc-json --output-dir ${NPDOC_OUT_DIR}
            ${NPDOC_IDL_FILES}
    COMMAND $<TARGET_FILE:npdoc>
            --root ${CMAKE_SOURCE_DIR}
            --cpp-headers ${CMAKE_SOURCE_DIR}/include/nprpc
            --cpp-exclude impl/
            --clang-arg=-std=c++23
            "$<$<BOOL:${NPDOC_INCLUDES}>:--clang-arg=-I$<JOIN:${NPDOC_INCLUDES},;--clang-arg=-I>>"
            "$<$<BOOL:${NPDOC_DEFINES}>:--clang-arg=-D$<JOIN:${NPDOC_DEFINES},;--clang-arg=-D>>"
            ${NPDOC_RESOURCE_ARGS}
            ${NPDOC_SWIFT_ARGS}
            ${NPDOC_IDL_ARGS}
            --guides ${CMAKE_SOURCE_DIR}/docs
            --out ${NPDOC_OUT_DIR}/api.json
    DEPENDS npdoc npidl nprpc_copy_headers ${NPDOC_IDL_FILES} ${NPDOC_HEADERS} ${NPDOC_GUIDES}
            ${NPRPC_DOCS_SWIFT_SYMBOLS}
    COMMAND_EXPAND_LISTS
    COMMENT "Collecting API docs into docs/api.json"
    VERBATIM
  )
  add_custom_target(docs_api DEPENDS ${NPDOC_OUT_DIR}/api.json)
endif()

if(NPRPC_BUILD_TESTS AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test)
  add_subdirectory(test)
endif()
