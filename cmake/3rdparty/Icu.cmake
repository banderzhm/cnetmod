include_guard(GLOBAL)

option(CNETMOD_USE_ICU_SUBMODULE "Use the bundled ICU submodule when available" ON)

macro(cnetmod_configure_icu)
    set(CNETMOD_HAS_ICU OFF)
    if(CNETMOD_ENABLE_POSTGRESQL)
        set(_cnetmod_icu_source "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/icu/icu4c/source")
        if(CNETMOD_USE_ICU_SUBMODULE AND WIN32
           AND EXISTS "${_cnetmod_icu_source}/allinone/allinone.sln")
            # ICU ships maintained Visual Studio projects rather than a CMake
            # project. Build just the two libraries PostgreSQL requires and
            # expose them as normal imported CMake targets.
            set(_cnetmod_icu_msbuild "${CMAKE_VS_MSBUILD_COMMAND}")
            if(NOT _cnetmod_icu_msbuild)
                set(_cnetmod_icu_msbuild "${CMAKE_COMMAND}")
            endif()
            find_package(Python3 REQUIRED COMPONENTS Interpreter)
            # ICU's Visual Studio data project invokes `py -3` directly.
            # Some supported Windows toolchains ship Python without the
            # launcher, so provide a tiny build-local compatibility shim.
            set(_cnetmod_icu_python_dir "${CMAKE_CURRENT_BINARY_DIR}/cnetmod_icu_python")
            file(MAKE_DIRECTORY "${_cnetmod_icu_python_dir}")
            file(GENERATE OUTPUT "${_cnetmod_icu_python_dir}/py.cmd" CONTENT
                "@echo off\nset args=%*\nif \"%~1\"==\"-3\" set args=%args:~3%\n\"${Python3_EXECUTABLE}\" %args%\n")
            # ICU's Debug outputs deliberately use the `d` suffix
            # (icuucd.lib/icuind.lib and icuuc78d.dll/icuin78d.dll), while
            # Release outputs do not.  Building only Release and mapping its
            # files into Debug causes a link-time LNK1104 on clean CI runners.
            # Keep the ICU MSBuild invocation configuration-aware; MSBuild is
            # incremental, so an already-built matching configuration is a
            # no-op while a Debug build always materializes its debug imports.
            set(_cnetmod_icu_configuration "$<IF:$<CONFIG:Debug>,Debug,Release>")
            set(_cnetmod_icu_stage_tools)
            # Tool executables have no Debug suffix in bin64. Switching
            # configurations may leave a newer executable from the other
            # configuration, bypassing ICU's incremental custom copy step.
            # Restore matching tools without recompilation or redundant writes.
            set(_cnetmod_icu_data_tools
                genrb gencnval gencfu genbrk genccode gencmn gendict gennorm2
                gensprep gentest icupkg makeconv pkgdata)
            foreach(_cnetmod_icu_tool IN LISTS _cnetmod_icu_data_tools)
                list(APPEND _cnetmod_icu_stage_tools
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${_cnetmod_icu_source}/tools/${_cnetmod_icu_tool}/x64/${_cnetmod_icu_configuration}/${_cnetmod_icu_tool}.exe"
                        "${_cnetmod_icu_source}/../bin64/${_cnetmod_icu_tool}.exe")
            endforeach()
            set(_cnetmod_icu_stamp
                "${CMAKE_CURRENT_BINARY_DIR}/cnetmod_icu_$<CONFIG>.stamp")
            add_custom_command(
                OUTPUT "${_cnetmod_icu_stamp}"
                COMMAND "${CMAKE_COMMAND}" -E env
                    "PATH=${_cnetmod_icu_python_dir}\;${_cnetmod_icu_source}/../bin64\;$ENV{PATH}"
                    "${_cnetmod_icu_msbuild}" "${_cnetmod_icu_source}/allinone/allinone.sln"
                    /target:common,i18n,genrb,gencnval,gencfu,genbrk,genccode,gencmn,gendict,gennorm2,gensprep,gentest,icupkg,makeconv,pkgdata
                    /property:Configuration=${_cnetmod_icu_configuration} /property:Platform=x64
                ${_cnetmod_icu_stage_tools}
                # ICU's makedata project declares its own test projects as
                # ProjectReferences.  They are irrelevant to the production
                # data DLL and can concurrently link to the same output path
                # on Windows.  Build just the data tools above, then run the
                # data project's NMake recipe without traversing those test
                # references.
                # Data generators are linked against the just-built ICU DLLs.
                # A developer machine can accidentally hide this dependency
                # through an existing PATH; clean runners cannot.  Keep the
                # bundled runtime directory first for deterministic execution.
                COMMAND "${CMAKE_COMMAND}" -E env
                    "PATH=${_cnetmod_icu_python_dir}\;${_cnetmod_icu_source}/../bin64\;$ENV{PATH}"
                    "${_cnetmod_icu_msbuild}" "${_cnetmod_icu_source}/data/makedata.vcxproj"
                    /property:Configuration=${_cnetmod_icu_configuration} /property:Platform=x64
                    /property:BuildProjectReferences=false
                    /property:SolutionDir=${_cnetmod_icu_source}/allinone/
                COMMAND "${CMAKE_COMMAND}" -E touch "${_cnetmod_icu_stamp}"
                DEPENDS
                    "${CMAKE_SOURCE_DIR}/cmake/3rdparty/Icu.cmake"
                    "${_cnetmod_icu_source}/allinone/allinone.sln"
                    "${_cnetmod_icu_source}/data/makedata.mak"
                    "${_cnetmod_icu_source}/data/BUILDRULES.py"
                    "${_cnetmod_icu_source}/test/testdata/BUILDRULES.py"
                COMMENT "Building bundled ICU libraries and data for $<CONFIG>")
            add_custom_target(cnetmod_icu DEPENDS "${_cnetmod_icu_stamp}")
            foreach(_cnetmod_icu_lib IN ITEMS uc i18n)
                add_library(ICU::${_cnetmod_icu_lib} SHARED IMPORTED GLOBAL)
                set(_cnetmod_icu_project "${_cnetmod_icu_lib}")
                if(_cnetmod_icu_lib STREQUAL "i18n")
                    set(_cnetmod_icu_project "i18n")
                    set(_cnetmod_icu_name "icuin")
                else()
                    set(_cnetmod_icu_project "common")
                    set(_cnetmod_icu_name "icuuc")
                endif()
                set_target_properties(ICU::${_cnetmod_icu_lib} PROPERTIES
                    IMPORTED_CONFIGURATIONS "DEBUG;RELEASE;RELWITHDEBINFO;MINSIZEREL"
                    INTERFACE_INCLUDE_DIRECTORIES "${_cnetmod_icu_source}/common"
                    IMPORTED_IMPLIB_DEBUG "${_cnetmod_icu_source}/../lib64/${_cnetmod_icu_name}d.lib"
                    IMPORTED_LOCATION_DEBUG "${_cnetmod_icu_source}/../bin64/${_cnetmod_icu_name}78d.dll"
                    IMPORTED_IMPLIB_RELEASE "${_cnetmod_icu_source}/../lib64/${_cnetmod_icu_name}.lib"
                    IMPORTED_LOCATION_RELEASE "${_cnetmod_icu_source}/../bin64/${_cnetmod_icu_name}78.dll"
                    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
                    MAP_IMPORTED_CONFIG_MINSIZEREL Release)
                add_dependencies(ICU::${_cnetmod_icu_lib} cnetmod_icu)
            endforeach()

            # ICU's data DLL is intentionally configuration-independent, unlike
            # icuuc/icuin which carry a Debug suffix.  Remember its location so
            # each Windows executable can receive the complete runtime set.
            set(CNETMOD_BUNDLED_ICU_RUNTIME_DIR "${_cnetmod_icu_source}/../bin64")
        else()
            find_package(ICU COMPONENTS uc i18n QUIET)
        endif()
        if(TARGET ICU::uc AND TARGET ICU::i18n)
            set(CNETMOD_HAS_ICU ON)
        else()
            if(CNETMOD_DEPENDENCY_MODE STREQUAL "system" AND UNIX AND NOT APPLE)
                message(STATUS "Install the missing ICU development package:")
                message(STATUS "  Arch:   sudo pacman -S --needed icu")
                message(STATUS "  Ubuntu: sudo apt install libicu-dev")
                message(STATUS "  CentOS: sudo dnf install libicu-devel")
            endif()
            message(FATAL_ERROR
                "CNETMOD_ENABLE_POSTGRESQL=ON requires ICU uc and i18n for complete RFC 4013 SASLprep. "
                "Install ICU with the selected package manager or disable PostgreSQL explicitly.")
        endif()
    endif()
endmacro()

# Imported shared libraries provide import libraries to the linker but CMake
# does not copy their DLLs next to consumers.  This is especially visible with
# the Visual Studio Debug configuration: executables link icuuc78d.dll and
# icuin78d.dll successfully, then fail at process creation with 0xC0000135.
#
# Schedule this after every subdirectory has declared its targets so tests,
# examples, benchmarks and the main application all get the same deployment
# behaviour.  CMake requires POST_BUILD commands to be declared in the same
# source directory as their target, so a single deployment target is used
# instead.  Every executable depends on it and it copies the runtime set before
# the executable is linked or launched.
function(cnetmod_deploy_bundled_icu_runtime)
    if(NOT WIN32 OR NOT CNETMOD_HAS_ICU OR
       NOT DEFINED CNETMOD_BUNDLED_ICU_RUNTIME_DIR)
        return()
    endif()

    set(_cnetmod_icu_directories "${CMAKE_SOURCE_DIR}")
    set(_cnetmod_icu_executables)
    set(_cnetmod_icu_index 0)
    list(LENGTH _cnetmod_icu_directories _cnetmod_icu_directory_count)
    while(_cnetmod_icu_index LESS _cnetmod_icu_directory_count)
        list(GET _cnetmod_icu_directories ${_cnetmod_icu_index} _cnetmod_icu_directory)
        get_property(_cnetmod_icu_targets DIRECTORY "${_cnetmod_icu_directory}"
            PROPERTY BUILDSYSTEM_TARGETS)
        foreach(_cnetmod_icu_target IN LISTS _cnetmod_icu_targets)
            get_target_property(_cnetmod_icu_target_type ${_cnetmod_icu_target} TYPE)
            if(NOT _cnetmod_icu_target_type STREQUAL "EXECUTABLE")
                continue()
            endif()

            # Do not spray a 33 MiB ICU data DLL into bundled third-party
            # tools (for example BoringSSL's own test programs).  cnetmod
            # executables consume the core library directly, which carries
            # ICU through its public link interface.
            get_target_property(_cnetmod_icu_link_libraries
                ${_cnetmod_icu_target} LINK_LIBRARIES)
            if(NOT _cnetmod_icu_link_libraries MATCHES
               "(^|;)cnetmod(_core|::core)(;|$)")
                continue()
            endif()
            list(APPEND _cnetmod_icu_executables ${_cnetmod_icu_target})
        endforeach()

        get_property(_cnetmod_icu_subdirectories DIRECTORY "${_cnetmod_icu_directory}"
            PROPERTY SUBDIRECTORIES)
        list(APPEND _cnetmod_icu_directories ${_cnetmod_icu_subdirectories})
        math(EXPR _cnetmod_icu_index "${_cnetmod_icu_index} + 1")
        list(LENGTH _cnetmod_icu_directories _cnetmod_icu_directory_count)
    endwhile()

    if(NOT _cnetmod_icu_executables)
        return()
    endif()

    set(_cnetmod_icu_deploy_commands)
    foreach(_cnetmod_icu_target IN LISTS _cnetmod_icu_executables)
        list(APPEND _cnetmod_icu_deploy_commands
            COMMAND "${CMAKE_COMMAND}" -E make_directory
                "$<TARGET_FILE_DIR:${_cnetmod_icu_target}>"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:ICU::uc>"
                "$<TARGET_FILE_DIR:${_cnetmod_icu_target}>"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:ICU::i18n>"
                "$<TARGET_FILE_DIR:${_cnetmod_icu_target}>"
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CNETMOD_BUNDLED_ICU_RUNTIME_DIR}/icudt78.dll"
                "$<TARGET_FILE_DIR:${_cnetmod_icu_target}>")
    endforeach()

    add_custom_target(cnetmod_icu_runtime_deploy ALL
        DEPENDS cnetmod_icu
        ${_cnetmod_icu_deploy_commands}
        COMMENT "Deploying bundled ICU runtime DLLs"
        VERBATIM)
    foreach(_cnetmod_icu_target IN LISTS _cnetmod_icu_executables)
        add_dependencies(${_cnetmod_icu_target} cnetmod_icu_runtime_deploy)
    endforeach()
endfunction()

if(WIN32)
    cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}"
        CALL cnetmod_deploy_bundled_icu_runtime)
endif()

function(cnetmod_link_icu TARGET_NAME)
    if(CNETMOD_HAS_ICU)
        target_link_libraries(${TARGET_NAME} PUBLIC ICU::uc ICU::i18n)
    endif()
endfunction()
