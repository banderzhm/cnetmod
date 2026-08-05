include_guard(GLOBAL)

# ==============================================================================
# BoringSSL Submodule Support
# ==============================================================================
option(CNETMOD_USE_BORINGSSL_SUBMODULE "Use BoringSSL as submodule (preferred over system)" ON)

# BoringSSL build options
option(BORINGSSL_BUILD_SHARED "Build shared libraries instead of static" OFF)
option(BORINGSSL_ENABLE_ASM "Enable assembly optimizations" ON)
option(BORINGSSL_FIPS "Build with FIPS mode" OFF)

macro(cnetmod_configure_boringssl_submodule)
    if(CNETMOD_ENABLE_SSL AND CNETMOD_USE_BORINGSSL_SUBMODULE)
        set(CNETMOD_BORINGSSL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/boringssl")

        if(EXISTS "${CNETMOD_BORINGSSL_DIR}/CMakeLists.txt")
            message(STATUS "Using BoringSSL from submodule: ${CNETMOD_BORINGSSL_DIR}")

            # Configure BoringSSL build options
            if(BORINGSSL_BUILD_SHARED)
                set(BUILD_SHARED_LIBS ON CACHE BOOL "" FORCE)
                message(STATUS "BoringSSL: Building shared libraries")
            else()
                set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
                message(STATUS "BoringSSL: Building static libraries")
            endif()

            # BoringSSL requires NASM for optimized x86/x64 Windows builds.
            # A standard Visual Studio Build Tools installation does not ship it,
            # so make the portable C++ implementation the automatic fallback.
            set(_cnetmod_boringssl_disable_asm OFF)
            if(NOT BORINGSSL_ENABLE_ASM)
                set(_cnetmod_boringssl_disable_asm ON)
            endif()
            if(WIN32 AND BORINGSSL_ENABLE_ASM)
                find_program(_cnetmod_boringssl_nasm NAMES nasm nasm.exe)
                if(NOT _cnetmod_boringssl_nasm)
                    set(_cnetmod_boringssl_disable_asm ON)
                    message(STATUS "BoringSSL: NASM not found; using portable C++ implementation")
                endif()
            endif()

            if(_cnetmod_boringssl_disable_asm)
                set(FORCE_DO_ASM OFF CACHE BOOL "" FORCE)
                set(NO_ASM ON CACHE BOOL "" FORCE)
                # This is the BoringSSL option that controls enable_language(ASM_NASM).
                set(OPENSSL_NO_ASM "1")
                message(STATUS "BoringSSL: Assembly optimizations disabled")
            else()
                message(STATUS "BoringSSL: Assembly optimizations enabled")
            endif()

            if(BORINGSSL_FIPS)
                set(BORINGSSL_ENABLE_FIPS ON CACHE BOOL "" FORCE)
                message(STATUS "BoringSSL: FIPS mode enabled")
            endif()

            # Build BoringSSL. The root project deliberately uses broad MSVC
            # compile options for C++ modules (/std:c++latest, /utf-8, etc.).
            # CMake propagates directory compile options to subdirectories;
            # if they reach BoringSSL, the Visual Studio generator also passes
            # them to NASM. NASM then rejects the MSVC-only switches and the
            # build fails with MSB3721 on GitHub's Windows runner.
            #
            # Keep third-party compiler flags isolated while adding BoringSSL,
            # then restore cnetmod's options for all subsequent project targets.
            get_directory_property(_cnetmod_saved_compile_options COMPILE_OPTIONS)
            set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")
            add_subdirectory("${CNETMOD_BORINGSSL_DIR}" third_party/boringssl/build EXCLUDE_FROM_ALL)
            set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "${_cnetmod_saved_compile_options}")

            # cnetmod enables broad warnings globally.  BoringSSL deliberately
            # keeps compatibility no-op parameters, so do not promote its
            # third-party warnings to build failures.
            if(TARGET fipsmodule)
                if(NOT MSVC)
                    target_compile_options(fipsmodule PRIVATE -Wno-error)
                endif()
            endif()
            foreach(_boringssl_target IN ITEMS crypto ssl)
                if(TARGET ${_boringssl_target} AND NOT MSVC)
                    target_compile_options(${_boringssl_target} PRIVATE -Wno-unused-parameter)
                endif()
            endforeach()

            # Find the targets directly - BoringSSL exports 'crypto' and 'ssl' targets
            if(TARGET crypto AND TARGET ssl)
                set(BoringSSL_FOUND TRUE)
                set(CNETMOD_HAS_SSL ON)
                set(BoringSSL_INCLUDE_DIRS "${CNETMOD_BORINGSSL_DIR}/include")
                set(BoringSSL_LIBRARIES ssl crypto)

                message(STATUS "BoringSSL submodule targets found: ssl, crypto")
            else()
                message(WARNING "BoringSSL submodule built but targets not found. Checking for library files...")
                # Fallback: look for static libraries
                if(MSVC)
                    # Visual Studio builds output to out/Release
                    if(EXISTS "${CNETMOD_BORINGSSL_DIR}/out/Release/ssl.lib" AND EXISTS "${CNETMOD_BORINGSSL_DIR}/out/Release/crypto.lib")
                        add_library(BoringSSL::ssl STATIC IMPORTED)
                        set_target_properties(BoringSSL::ssl PROPERTIES
                            IMPORTED_LOCATION "${CNETMOD_BORINGSSL_DIR}/out/Release/ssl.lib"
                        )

                        add_library(BoringSSL::crypto STATIC IMPORTED)
                        set_target_properties(BoringSSL::crypto PROPERTIES
                            IMPORTED_LOCATION "${CNETMOD_BORINGSSL_DIR}/out/Release/crypto.lib"
                        )

                        set(BoringSSL_FOUND TRUE)
                        set(BoringSSL_INCLUDE_DIRS "${CNETMOD_BORINGSSL_DIR}/include")
                        set(BoringSSL_LIBRARIES BoringSSL::ssl BoringSSL::crypto)
                    endif()
                elseif(UNIX OR APPLE)
                    # Unix-like systems build to src/out or lib
                    set(_potential_lib_dirs "" "${CNETMOD_BORINGSSL_DIR}/src/out/Debug")
                    set(_potential_lib_dirs "${_potential_lib_dirs}" "${CNETMOD_BORINGSSL_DIR}/src/out/Release")
                    set(_potential_lib_dirs "${_potential_lib_dirs}" "${CNETMOD_BORINGSSL_DIR}/lib")

                    foreach(lib_dir ${_potential_lib_dirs})
                        if(EXISTS "${lib_dir}/libssl.a" AND EXISTS "${lib_dir}/libcrypto.a")
                            add_library(BoringSSL::ssl STATIC IMPORTED)
                            set_target_properties(BoringSSL::ssl PROPERTIES
                                IMPORTED_LOCATION "${lib_dir}/libssl.a"
                            )

                            add_library(BoringSSL::crypto STATIC IMPORTED)
                            set_target_properties(BoringSSL::crypto PROPERTIES
                                IMPORTED_LOCATION "${lib_dir}/libcrypto.a"
                            )

                            set(BoringSSL_FOUND TRUE)
                            set(BoringSSL_INCLUDE_DIRS "${CNETMOD_BORINGSSL_DIR}/include")
                            set(BoringSSL_LIBRARIES BoringSSL::ssl BoringSSL::crypto)
                            break()
                        endif()
                    endforeach()
                endif()
            endif()
        else()
            message(WARNING "BoringSSL submodule not initialized. Run:\n  git submodule init\n  git submodule update")
            set(CNETMOD_USE_BORINGSSL_SUBMODULE FALSE)
        endif()
    endif()
endmacro()

macro(cnetmod_configure_boringssl_quic)
    # First check submodule
    cnetmod_configure_boringssl_submodule()

    option(CNETMOD_ENABLE_BORINGSSL_QUIC "Use BoringSSL with QUIC API support" OFF)
    set(CNETMOD_HAS_QUIC OFF)

    if(CNETMOD_ENABLE_SSL AND CNETMOD_ENABLE_BORINGSSL_QUIC)
        if(BoringSSL_FOUND)
            set(CNETMOD_HAS_QUIC ON)
            # CNETMOD_HAS_SSL describes availability of the SSL abstraction,
            # not a particular provider.  BoringSSL is the sole provider when
            # QUIC is enabled.
            set(CNETMOD_HAS_SSL ON)
            message(STATUS "BoringSSL with QUIC API found - QUIC support enabled")

            # Include directories and linking are wired after the target
            # exists, in cnetmod_link_third_party_dependencies().
        else()
            message(WARNING "BoringSSL not found - QUIC support disabled even if requested. "
                          "Please install BoringSSL via vcpkg or use BoringSSL submodule.")
            set(CNETMOD_HAS_QUIC FALSE)
        endif()
    elseif(CNETMOD_ENABLE_SSL AND NOT CNETMOD_ENABLE_BORINGSSL_QUIC)
        # The bundled provider is available for TLS; its QUIC API is opt-in.
        message(STATUS "BoringSSL TLS enabled - QUIC support not requested")
        set(CNETMOD_HAS_QUIC FALSE)
    else()
        set(CNETMOD_HAS_QUIC FALSE)
    endif()
endmacro()
