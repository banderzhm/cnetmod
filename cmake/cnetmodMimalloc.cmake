include_guard(GLOBAL)

# Enable process-wide mimalloc replacement for a final executable.
#
# This must be called on the executable itself. Linking an allocator only to a
# static library cannot reliably replace malloc/new in the completed process.
function(cnetmod_enable_mimalloc target)
    if(NOT TARGET ${target})
        message(FATAL_ERROR "cnetmod_enable_mimalloc: unknown target '${target}'")
    endif()

    get_target_property(_cnetmod_mimalloc_target_type ${target} TYPE)
    if(NOT _cnetmod_mimalloc_target_type STREQUAL "EXECUTABLE")
        message(FATAL_ERROR
            "cnetmod_enable_mimalloc requires an executable target; '${target}' "
            "is ${_cnetmod_mimalloc_target_type}")
    endif()

    get_target_property(_cnetmod_mimalloc_enabled ${target}
        CNETMOD_MIMALLOC_ENABLED)
    if(_cnetmod_mimalloc_enabled)
        return()
    endif()

    if(WIN32)
        if(NOT TARGET mimalloc)
            message(FATAL_ERROR
                "The cnetmod SDK does not provide its bundled mimalloc target")
        endif()

        target_link_libraries(${target} PRIVATE mimalloc)
        if(MSVC)
            target_link_options(${target} PRIVATE "LINKER:/INCLUDE:mi_version")
        endif()

        if(DEFINED CNETMOD_MIMALLOC_REDIRECT AND
           NOT CNETMOD_MIMALLOC_REDIRECT STREQUAL "")
            set(_cnetmod_mimalloc_redirect "${CNETMOD_MIMALLOC_REDIRECT}")
        else()
            set(_cnetmod_mimalloc_redirect
                "${PACKAGE_PREFIX_DIR}/@CMAKE_INSTALL_BINDIR@/mimalloc-redirect.dll")
        endif()

        get_target_property(_cnetmod_mimalloc_source_dir ${target} SOURCE_DIR)
        if(_cnetmod_mimalloc_source_dir STREQUAL CMAKE_CURRENT_SOURCE_DIR)
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:mimalloc>" "$<TARGET_FILE_DIR:${target}>"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_cnetmod_mimalloc_redirect}" "$<TARGET_FILE_DIR:${target}>"
                VERBATIM)
        else()
            # A parent directory may link a child target (CMP0079), but CMake
            # requires TARGET-form custom commands to be declared where the
            # target was created. Use a dependency target only for that rare
            # aggregate/fallback path.
            set(_cnetmod_mimalloc_runtime_target
                "${target}_cnetmod_mimalloc_runtime")
            add_custom_target(${_cnetmod_mimalloc_runtime_target}
                COMMAND ${CMAKE_COMMAND} -E make_directory
                    "$<TARGET_FILE_DIR:${target}>"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "$<TARGET_FILE:mimalloc>" "$<TARGET_FILE_DIR:${target}>"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_cnetmod_mimalloc_redirect}" "$<TARGET_FILE_DIR:${target}>"
                DEPENDS mimalloc
                VERBATIM)
            add_dependencies(${target} ${_cnetmod_mimalloc_runtime_target})
        endif()
    else()
        if(NOT DEFINED CNETMOD_MIMALLOC_OBJECT OR
           CNETMOD_MIMALLOC_OBJECT STREQUAL "")
            message(FATAL_ERROR
                "The cnetmod SDK does not provide its bundled mimalloc object")
        endif()

        # An object source participates directly in the executable link and is
        # not subject to archive extraction order or --as-needed elimination.
        target_sources(${target} PRIVATE "${CNETMOD_MIMALLOC_OBJECT}")
        if(NOT TARGET mimalloc-static)
            message(FATAL_ERROR
                "The cnetmod SDK does not provide its bundled mimalloc support target")
        endif()
        # Reuse only the upstream target's platform link contract
        # (pthread/rt/atomic where required). Linking the archive itself would
        # needlessly compile and ship a second copy of the implementation.
        get_target_property(_cnetmod_mimalloc_support_libraries
            mimalloc-static INTERFACE_LINK_LIBRARIES)
        if(_cnetmod_mimalloc_support_libraries)
            target_link_libraries(${target} PRIVATE
                ${_cnetmod_mimalloc_support_libraries})
        endif()
    endif()

    set_property(TARGET ${target} PROPERTY CNETMOD_MIMALLOC_ENABLED TRUE)
endfunction()
