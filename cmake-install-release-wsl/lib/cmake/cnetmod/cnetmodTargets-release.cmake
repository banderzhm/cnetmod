#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "cnetmod::leveldb" for configuration "Release"
set_property(TARGET cnetmod::leveldb APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(cnetmod::leveldb PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/leveldb.lib"
  )

list(APPEND _cmake_import_check_targets cnetmod::leveldb )
list(APPEND _cmake_import_check_files_for_cnetmod::leveldb "${_IMPORT_PREFIX}/lib/leveldb.lib" )

# Import target "cnetmod::cnetmod_core" for configuration "Release"
set_property(TARGET cnetmod::cnetmod_core APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(cnetmod::cnetmod_core PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELEASE "CXX"
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/cnetmod_core.lib"
  )

list(APPEND _cmake_import_check_targets cnetmod::cnetmod_core )
list(APPEND _cmake_import_check_files_for_cnetmod::cnetmod_core "${_IMPORT_PREFIX}/lib/cnetmod_core.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
