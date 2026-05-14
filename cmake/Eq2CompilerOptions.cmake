function(eq2_target_defaults target)
  get_target_property(target_type "${target}" TYPE)

  if(target_type STREQUAL "INTERFACE_LIBRARY")
    set(scope INTERFACE)
  else()
    set(scope PUBLIC)
  endif()

  target_compile_features("${target}" ${scope} cxx_std_20)
  target_compile_definitions("${target}" ${scope}
    $<$<CONFIG:Debug>:EQ2_DEBUG>
  )

  if(MSVC)
    target_compile_options("${target}" ${scope} /W4 /permissive-)
  else()
    target_compile_options("${target}" ${scope} -Wall -Wextra -Wpedantic)
  endif()
endfunction()
