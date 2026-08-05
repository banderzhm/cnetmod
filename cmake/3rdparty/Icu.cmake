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
            # ICU's Visual Studio solution writes its import libraries and DLLs
            # into the submodule tree.  A custom target without declared
            # outputs is always out of date, so it used to invoke MSBuild for
            # every cnetmod build (and produced a very large, mostly-no-op log).
            #
            # Treat a complete ICU runtime as reusable.  The CI cache restores
            # these four files before configuration; local reconfigures get the
            # same fast path.  A missing or partial runtime still builds ICU
            # normally and then becomes reusable on the next build.
            set(_cnetmod_icu_runtime_files
                "${_cnetmod_icu_source}/../lib64/icuuc.lib"
                "${_cnetmod_icu_source}/../lib64/icuin.lib"
                "${_cnetmod_icu_source}/../bin64/icuuc78.dll"
                "${_cnetmod_icu_source}/../bin64/icuin78.dll")
            set(_cnetmod_icu_ready TRUE)
            foreach(_cnetmod_icu_runtime_file IN LISTS _cnetmod_icu_runtime_files)
                if(NOT EXISTS "${_cnetmod_icu_runtime_file}")
                    set(_cnetmod_icu_ready FALSE)
                    break()
                endif()
            endforeach()

            if(_cnetmod_icu_ready)
                add_custom_target(cnetmod_icu
                    COMMENT "Using existing bundled ICU libraries")
            else()
                add_custom_target(cnetmod_icu
                    COMMAND "${_cnetmod_icu_msbuild}" "${_cnetmod_icu_source}/allinone/allinone.sln"
                        /target:common /target:i18n /property:Configuration=$<CONFIG> /property:Platform=x64
                    COMMENT "Building bundled ICU libraries")
            endif()
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
                    INTERFACE_INCLUDE_DIRECTORIES "${_cnetmod_icu_source}/common"
                    IMPORTED_IMPLIB_DEBUG "${_cnetmod_icu_source}/../lib64/${_cnetmod_icu_name}.lib"
                    IMPORTED_LOCATION_DEBUG "${_cnetmod_icu_source}/../bin64/${_cnetmod_icu_name}78.dll"
                    IMPORTED_IMPLIB_RELEASE "${_cnetmod_icu_source}/../lib64/${_cnetmod_icu_name}.lib"
                    IMPORTED_LOCATION_RELEASE "${_cnetmod_icu_source}/../bin64/${_cnetmod_icu_name}78.dll"
                    MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release
                    MAP_IMPORTED_CONFIG_MINSIZEREL Release)
                add_dependencies(ICU::${_cnetmod_icu_lib} cnetmod_icu)
            endforeach()
        else()
            find_package(ICU COMPONENTS uc i18n QUIET)
        endif()
        if(TARGET ICU::uc AND TARGET ICU::i18n)
            set(CNETMOD_HAS_ICU ON)
        else()
            message(FATAL_ERROR
                "CNETMOD_ENABLE_POSTGRESQL=ON requires ICU uc and i18n for complete RFC 4013 SASLprep. "
                "Install ICU with the selected package manager or disable PostgreSQL explicitly.")
        endif()
    endif()
endmacro()

function(cnetmod_link_icu TARGET_NAME)
    if(CNETMOD_HAS_ICU)
        target_link_libraries(${TARGET_NAME} PUBLIC ICU::uc ICU::i18n)
    endif()
endfunction()
