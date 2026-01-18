function(npidl_generate_idl_files idl_files_list module_name)
  # Track all input IDL files for the custom target
  set(all_idl_files)
  
  foreach(file ${idl_files_list})
    get_filename_component(basename ${file} NAME_WE)
    add_custom_command(
      OUTPUT
        ${CMAKE_BINARY_DIR}/${module_name}/src/gen/include/${basename}.hpp 
        ${CMAKE_BINARY_DIR}/${module_name}/src/gen/${basename}.cpp
        ${CMAKE_BINARY_DIR}/${module_name}/src/gen/js/${basename}.ts
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/${module_name}/src/gen/include
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/${module_name}/src/gen/js
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/npidl_tmp
      COMMAND npidl
        --cpp --ts
        --output-dir ${CMAKE_BINARY_DIR}/npidl_tmp
        ${file}
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/npidl_tmp/${basename}.hpp
        ${CMAKE_BINARY_DIR}/${module_name}/src/gen/include/${basename}.hpp
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/npidl_tmp/${basename}.cpp
        ${CMAKE_BINARY_DIR}/${module_name}/src/gen/${basename}.cpp
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/npidl_tmp/${basename}.ts
        ${CMAKE_BINARY_DIR}/${module_name}/src/gen/js/${basename}.ts
      DEPENDS npidl ${file}
      COMMENT "Generating stubs from ${file}"
      VERBATIM
    )
    list(APPEND ${module_name}_GENERATED_SOURCES 
      ${CMAKE_BINARY_DIR}/${module_name}/src/gen/${basename}.cpp
    )
    list(APPEND ${module_name}_GENERATED_HEADERS 
      ${CMAKE_BINARY_DIR}/${module_name}/src/gen/include/${basename}.hpp
    )
    list(APPEND ${module_name}_GENERATED_TS
      ${CMAKE_BINARY_DIR}/${module_name}/src/gen/js/${basename}.ts
    )
    # Add the source IDL file to the list for the custom target
    list(APPEND all_idl_files ${file})
  endforeach()

  set(${module_name}_INCLUDE_DIR ${CMAKE_BINARY_DIR}/${module_name}/src/gen/include PARENT_SCOPE)
  set(${module_name}_GENERATED_SOURCES ${${module_name}_GENERATED_SOURCES} PARENT_SCOPE)
  set(${module_name}_GENERATED_HEADERS ${${module_name}_GENERATED_HEADERS} PARENT_SCOPE)
  set(${module_name}_GENERATED_TS ${${module_name}_GENERATED_TS} PARENT_SCOPE)

  # Custom target should depend on the generated files AND sources for proper dependency tracking
  add_custom_target(
    ${module_name}_gen
    DEPENDS 
      ${${module_name}_GENERATED_SOURCES}
      ${${module_name}_GENERATED_HEADERS}
      ${${module_name}_GENERATED_TS}
    SOURCES 
      ${all_idl_files}
    COMMENT 
      "Generating all ${module_name} files"
  )
endfunction()