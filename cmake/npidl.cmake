function(npidl_generate_idl_files idl_files_list module_name)
  # Track all input IDL files for the custom target
  set(all_idl_files)
  
  foreach(file ${idl_files_list})
    get_filename_component(basename ${file} NAME_WE)
    add_custom_command(
      OUTPUT
        ${CMAKE_BINARY_DIR}/${module_name}/src/gen/include/${module_name}/${basename}.hpp 
        ${CMAKE_BINARY_DIR}/${module_name}/src/gen/${basename}.cpp
        ${CMAKE_BINARY_DIR}/${module_name}/src/gen/js/${basename}.ts
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/${module_name}/src/gen/include/${module_name}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/${module_name}/src/gen/js
      COMMAND npidl
        --module-name ${module_name}
        --out-inc-dir ${CMAKE_BINARY_DIR}/${module_name}/src/gen/include/${module_name}
        --out-src-dir ${CMAKE_BINARY_DIR}/${module_name}/src/gen
        --out-ts-dir ${CMAKE_BINARY_DIR}/${module_name}/src/gen/js
        ${file}
      DEPENDS npidl ${file}
      # BYPRODUCTS ${CMAKE_BINARY_DIR}/${module_name}/src/gen/js/${basename}.ts
      COMMENT "Generating npc files from ${file}"
      VERBATIM
    )
    list(APPEND ${module_name}_GENERATED_SOURCES 
      ${CMAKE_BINARY_DIR}/${module_name}/src/gen/${basename}.cpp
    )
    list(APPEND ${module_name}_GENERATED_HEADERS 
      ${CMAKE_BINARY_DIR}/${module_name}/src/gen/include/${module_name}/${basename}.hpp
    )
    # Add the source IDL file to the list for the custom target
    list(APPEND all_idl_files ${file})
  endforeach()
  
  set(${module_name}_INCLUDE_DIR ${CMAKE_BINARY_DIR}/${module_name}/src/gen/include PARENT_SCOPE)
  set(${module_name}_GENERATED_SOURCES ${${module_name}_GENERATED_SOURCES} PARENT_SCOPE)
  set(${module_name}_GENERATED_HEADERS ${${module_name}_GENERATED_HEADERS} PARENT_SCOPE)

  # Custom target should depend on the generated files AND sources for proper dependency tracking
  add_custom_target(
    ${module_name}_gen
    DEPENDS ${${module_name}_GENERATED_SOURCES} ${${module_name}_GENERATED_HEADERS}
    SOURCES ${all_idl_files}
    COMMENT "Generating all ${module_name} files"
  )
endfunction()