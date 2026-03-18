function(get_git_commit dir out_var)
  if(NOT GIT_EXECUTABLE)
    set(${out_var} "unavailable" PARENT_SCOPE)
    return()
  endif()

  if(NOT EXISTS "${dir}/.git")
    set(${out_var} "unavailable" PARENT_SCOPE)
    return()
  endif()

  execute_process(
    COMMAND ${GIT_EXECUTABLE} -C ${dir} rev-parse HEAD
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )

  if(result EQUAL 0 AND NOT output STREQUAL "")
    set(${out_var} "${output}" PARENT_SCOPE)
  else()
    set(${out_var} "unavailable" PARENT_SCOPE)
  endif()
endfunction()

function(escape_cpp_string input output_var)
  set(value "${input}")
  string(REPLACE "\\" "\\\\" value "${value}")
  string(REPLACE "\"" "\\\"" value "${value}")
  set(${output_var} "${value}" PARENT_SCOPE)
endfunction()

get_git_commit("${ROOT_DIR}" project_commit)

set(third_party_entries "")
foreach(dir IN LISTS THIRD_PARTY_DIRS)
  get_git_commit("${ROOT_DIR}/third_party/${dir}" dependency_commit)
  escape_cpp_string("${dir}" escaped_name)
  escape_cpp_string("${dependency_commit}" escaped_commit)
  string(APPEND third_party_entries "  build_info_entry{\"${escaped_name}\", \"${escaped_commit}\"},\n")
endforeach()

list(LENGTH THIRD_PARTY_DIRS third_party_count)
if(third_party_count EQUAL 0)
  set(third_party_entries "")
endif()

escape_cpp_string("${PROJECT_VERSION}" escaped_project_version)
escape_cpp_string("${project_commit}" escaped_project_commit)

set(content "#pragma once\n\n#include <array>\n#include <string_view>\n\nnamespace nprpc::impl::build_info {\n\nstruct build_info_entry {\n  std::string_view name;\n  std::string_view commit;\n};\n\ninline constexpr std::string_view project_version = \"${escaped_project_version}\";\ninline constexpr std::string_view project_commit = \"${escaped_project_commit}\";\n\ninline constexpr std::array<build_info_entry, ${third_party_count}> third_party_dependencies{{\n${third_party_entries}}};\n\n} // namespace nprpc::impl::build_info\n")

set(tmp_file "${OUTPUT_FILE}.tmp")
file(WRITE "${tmp_file}" "${content}")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${tmp_file}" "${OUTPUT_FILE}")
file(REMOVE "${tmp_file}")