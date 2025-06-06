function(npidl_generate_idl_files idl_files_list module_name)
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
  endforeach()
  set(${module_name}_INCLUDE_DIR ${CMAKE_BINARY_DIR}/${module_name}/src/gen/include PARENT_SCOPE)
  set(${module_name}_GENERATED_SOURCES ${${module_name}_GENERATED_SOURCES} PARENT_SCOPE)
  set(${module_name}_GENERATED_HEADERS ${${module_name}_GENERATED_HEADERS} PARENT_SCOPE)
endfunction()