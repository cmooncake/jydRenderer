if(NOT DEFINED ENV{JYD_RUNTIME_DESTINATION})
    message(FATAL_ERROR "JYD_RUNTIME_DESTINATION was not provided")
endif()

set(_jyd_destination "$ENV{JYD_RUNTIME_DESTINATION}")
set(_jyd_runtime_dlls "$ENV{JYD_RUNTIME_DLLS}")
string(REPLACE "|" ";" _jyd_runtime_dlls "${_jyd_runtime_dlls}")

set(_jyd_search_roots "$ENV{JYD_RUNTIME_SEARCH_ROOTS}")
string(REPLACE "|" ";" _jyd_search_roots "${_jyd_search_roots}")
set(_jyd_search_directories)
foreach(_jyd_search_root IN LISTS _jyd_search_roots)
    list(APPEND _jyd_search_directories
        "${_jyd_search_root}/bin"
        "${_jyd_search_root}/debug/bin")
endforeach()

# TARGET_RUNTIME_DLLS only follows imported targets that describe their DLL
# location. Packages found through FindZLIB can expose only an import library,
# so also inspect the completed bridge and resolve such dependencies by name.
if(DEFINED ENV{JYD_RUNTIME_BINARY} AND EXISTS "$ENV{JYD_RUNTIME_BINARY}")
    file(GET_RUNTIME_DEPENDENCIES
        LIBRARIES "$ENV{JYD_RUNTIME_BINARY}"
        DIRECTORIES ${_jyd_search_directories}
        RESOLVED_DEPENDENCIES_VAR _jyd_resolved_dlls
        UNRESOLVED_DEPENDENCIES_VAR _jyd_unresolved_dlls
        PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
        POST_EXCLUDE_REGEXES ".*[Ww]indows[/\\\\][Ss]ystem32.*")
    list(APPEND _jyd_runtime_dlls ${_jyd_resolved_dlls})
endif()

list(REMOVE_DUPLICATES _jyd_runtime_dlls)

foreach(_jyd_runtime_dll IN LISTS _jyd_runtime_dlls)
    if(_jyd_runtime_dll AND EXISTS "${_jyd_runtime_dll}")
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_jyd_runtime_dll}"
                "${_jyd_destination}"
            COMMAND_ERROR_IS_FATAL ANY
        )
    endif()
endforeach()

if(_jyd_unresolved_dlls)
    list(JOIN _jyd_unresolved_dlls ", " _jyd_unresolved_text)
    message(FATAL_ERROR
        "Unresolved runtime dependencies for $ENV{JYD_RUNTIME_BINARY}: "
        "${_jyd_unresolved_text}")
endif()
