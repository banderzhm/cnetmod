include_guard(GLOBAL)

macro(cnetmod_configure_icu)
    set(CNETMOD_HAS_ICU OFF)
    if(CNETMOD_ENABLE_POSTGRESQL)
        find_package(ICU COMPONENTS uc i18n QUIET)
        if(ICU_FOUND AND TARGET ICU::uc AND TARGET ICU::i18n)
            set(CNETMOD_HAS_ICU ON)
        else()
            message(WARNING
                "ICU uc and i18n were not found; PostgreSQL SCRAM accepts only printable ASCII credentials. "
                "Install ICU to enable complete RFC 4013 SASLprep for non-ASCII credentials.")
        endif()
    endif()
endmacro()

function(cnetmod_link_icu TARGET_NAME)
    if(CNETMOD_HAS_ICU)
        target_link_libraries(${TARGET_NAME} PUBLIC ICU::uc ICU::i18n)
    endif()
endfunction()
