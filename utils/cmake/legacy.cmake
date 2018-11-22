# ┌──────────────────────────────────────────────────────────────────┐
#   Functions & macro
# └──────────────────────────────────────────────────────────────────┘
# TODO[med]: does this support cmake-generator-expressions?
# TODO[med]: see also https://github.com/onqtam/ucm/blob/master/cmake/ucm.cmake : ucm_add_flags and ucm_add_linker_flags macros

if(${CMAKE_VERSION} VERSION_LESS "3.13.0-rc2")
    function(target_link_options target scope)
        if (${scope} STREQUAL "PUBLIC" OR ${scope} STREQUAL "PRIVATE")
            foreach(f ${ARGN})
                set_property(TARGET ${target} APPEND_STRING PROPERTY LINK_FLAGS " ${f}")
            endforeach()
#            get_target_property(flags ${target} LINK_FLAGS)
#            message(STATUS "Flags is ${flags}")
        else()
            message(FATAL_ERROR "target_link_options called with invalid arguments")
        endif()
    endfunction()
endif()