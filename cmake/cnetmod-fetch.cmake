cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED CNETMOD_VERSION OR CNETMOD_VERSION STREQUAL "")
    message(FATAL_ERROR "Pass -DCNETMOD_VERSION=<major.minor.patch>")
endif()
if(NOT CNETMOD_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "CNETMOD_VERSION must be a stable semantic version")
endif()
if(NOT DEFINED CNETMOD_TARGET OR CNETMOD_TARGET STREQUAL "")
    message(FATAL_ERROR
        "Pass the exact release target with -DCNETMOD_TARGET=<target>. "
        "The target encodes compiler, standard library, runtime and platform baseline.")
endif()
if(NOT DEFINED CNETMOD_PREFIX OR CNETMOD_PREFIX STREQUAL "")
    set(CNETMOD_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/cnetmod-${CNETMOD_VERSION}-${CNETMOD_TARGET}")
endif()
if(NOT DEFINED CNETMOD_DISTRIBUTION_BASE_URL OR
        CNETMOD_DISTRIBUTION_BASE_URL STREQUAL "")
    set(CNETMOD_DISTRIBUTION_BASE_URL
        "https://github.com/banderzhm/cnetmod/releases/download/v${CNETMOD_VERSION}")
endif()

set(catalog_name "cnetmod-release-${CNETMOD_VERSION}.json")
set(download_root "${CMAKE_CURRENT_BINARY_DIR}/.cnetmod-downloads")
file(MAKE_DIRECTORY "${download_root}")
set(catalog_path "${download_root}/${catalog_name}")
file(DOWNLOAD
    "${CNETMOD_DISTRIBUTION_BASE_URL}/${catalog_name}"
    "${catalog_path}"
    TLS_VERIFY ON
    STATUS catalog_status)
list(GET catalog_status 0 catalog_code)
list(GET catalog_status 1 catalog_message)
if(NOT catalog_code EQUAL 0)
    message(FATAL_ERROR "Unable to download cnetmod catalog: ${catalog_message}")
endif()

file(READ "${catalog_path}" catalog)
string(JSON catalog_version GET "${catalog}" version)
if(NOT catalog_version STREQUAL CNETMOD_VERSION)
    message(FATAL_ERROR
        "Catalog version ${catalog_version} does not match requested ${CNETMOD_VERSION}")
endif()
string(JSON artifact_count LENGTH "${catalog}" artifacts)
set(artifact_index -1)
if(artifact_count GREATER 0)
    math(EXPR artifact_last "${artifact_count} - 1")
    foreach(index RANGE 0 ${artifact_last})
        string(JSON candidate_target GET "${catalog}" artifacts ${index} target)
        if(candidate_target STREQUAL CNETMOD_TARGET)
            set(artifact_index ${index})
            break()
        endif()
    endforeach()
endif()
if(artifact_index EQUAL -1)
    message(FATAL_ERROR
        "Release ${CNETMOD_VERSION} has no artifact for ${CNETMOD_TARGET}")
endif()
string(JSON artifact_file GET "${catalog}" artifacts ${artifact_index} file)
string(JSON artifact_sha256 GET "${catalog}" artifacts ${artifact_index} sha256)
set(archive_path "${download_root}/${artifact_file}")
file(DOWNLOAD
    "${CNETMOD_DISTRIBUTION_BASE_URL}/${artifact_file}"
    "${archive_path}"
    EXPECTED_HASH "SHA256=${artifact_sha256}"
    TLS_VERIFY ON
    SHOW_PROGRESS
    STATUS archive_status)
list(GET archive_status 0 archive_code)
list(GET archive_status 1 archive_message)
if(NOT archive_code EQUAL 0)
    message(FATAL_ERROR "Unable to download cnetmod SDK: ${archive_message}")
endif()

set(staging "${CNETMOD_PREFIX}.staging")
file(REMOVE_RECURSE "${staging}")
file(MAKE_DIRECTORY "${staging}")
file(ARCHIVE_EXTRACT INPUT "${archive_path}" DESTINATION "${staging}")
if(NOT EXISTS "${staging}/lib/cmake/cnetmod/cnetmodConfig.cmake")
    message(FATAL_ERROR "Downloaded archive is not a valid cnetmod SDK")
endif()
set(package_manifest
    "${staging}/share/cnetmod/cnetmod-package-manifest.json")
if(NOT EXISTS "${package_manifest}")
    message(FATAL_ERROR "Downloaded archive has no cnetmod package manifest")
endif()
file(READ "${package_manifest}" package_metadata)
string(JSON package_version GET "${package_metadata}" version)
string(JSON package_target GET "${package_metadata}" target)
if(NOT package_version STREQUAL CNETMOD_VERSION OR
   NOT package_target STREQUAL CNETMOD_TARGET)
    message(FATAL_ERROR
        "Downloaded package identity ${package_version}/${package_target} does not match requested ${CNETMOD_VERSION}/${CNETMOD_TARGET}")
endif()
if(EXISTS "${CNETMOD_PREFIX}" AND NOT EXISTS
        "${CNETMOD_PREFIX}/share/cnetmod/cnetmod-package-manifest.json")
    message(FATAL_ERROR
        "Refusing to replace a directory not owned by cnetmod: ${CNETMOD_PREFIX}")
endif()
set(previous "${CNETMOD_PREFIX}.previous")
if(EXISTS "${previous}")
    file(REMOVE_RECURSE "${previous}")
endif()
if(EXISTS "${CNETMOD_PREFIX}")
    file(RENAME "${CNETMOD_PREFIX}" "${previous}"
        RESULT preserve_result)
    if(NOT preserve_result STREQUAL "0")
        message(FATAL_ERROR
            "Unable to preserve the existing cnetmod SDK: ${preserve_result}")
    endif()
endif()
file(RENAME "${staging}" "${CNETMOD_PREFIX}" RESULT install_result)
if(NOT install_result STREQUAL "0")
    if(EXISTS "${previous}")
        file(RENAME "${previous}" "${CNETMOD_PREFIX}")
    endif()
    message(FATAL_ERROR "Unable to install cnetmod SDK: ${install_result}")
endif()
if(EXISTS "${previous}")
    file(REMOVE_RECURSE "${previous}")
endif()
message(STATUS "Installed cnetmod ${CNETMOD_VERSION} at ${CNETMOD_PREFIX}")
