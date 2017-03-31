# - Find DAOS
# Find the DAOS Stack
# See https://github.com/daos-stack/
#
# This module accepts the following optional variables:
#    DAOS_PREFIX   = A hint on DAOS install path.
#
# This module defines the following variables:
#    DAOS_FOUND       = Was DAOS found or not?
#    DAOS_LIBRARIES   = The list of libraries to link to when using DAOS
#    DAOS_INCLUDE_DIR = The path to DAOS include directory
#
# On can set DAOS_PREFIX before using find_package(DAOS) and the
# module with use the PATH as a hint to find DAOS.
#
# The hint can be given on the command line too:
#   cmake -DDAOS_PREFIX=/DATA/ERIC/DAOS /path/to/source

if(DAOS_PREFIX)
  message(STATUS "FindDAOS: using PATH HINT: ${DAOS_PREFIX}")
  # Try to make the prefix override the normal paths
  find_path(DAOS_INCLUDE_DIR
    NAMES include/daos.h
    PATHS ${DAOS_PREFIX}
    NO_DEFAULT_PATH
    DOC "The DAOS include headers")
    message("DAOS_INCLUDE_DIR ${DAOS_INCLUDE_DIR}")

  find_path(DAOS_LIBRARY_DIR
    NAMES libdaos.so
    PATHS ${DAOS_PREFIX}
    PATH_SUFFIXES lib lib64
    NO_DEFAULT_PATH
    DOC "The DAOS libraries")
endif()

if (NOT DAOS_INCLUDE_DIR)
  find_path(DAOS_INCLUDE_DIR
    NAMES daos.h
    PATHS ${DAOS_PREFIX}
    DOC "The DAOS include headers")
endif (NOT DAOS_INCLUDE_DIR)

message(STATUS "Found daos headers: ${DAOS_INCLUDE_DIR}")

if (NOT DAOS_LIBRARY_DIR)
  find_path(DAOS_LIBRARY_DIR
    NAMES libdaos.so
    PATHS ${DAOS_PREFIX}
    PATH_SUFFIXES lib lib64
    DOC "The DAOS libraries")
endif (NOT DAOS_LIBRARY_DIR)

find_library(DAOS_LIBRARY daos PATHS ${DAOS_LIBRARY_DIR} NO_DEFAULT_PATH)
#check_library_exists(daosfs OpenDaosFileSystem ${DAOS_LIBRARY_DIR} DAOSLIB)
#if (NOT DAOSLIB)
#  unset(DAOS_LIBRARY_DIR CACHE)
#  unset(DAOS_INCLUDE_DIR CACHE)
#endif (NOT DAOSLIB)

set(DAOS_LIBRARIES ${DAOS_LIBRARY})
message(STATUS "Found daos libraries: ${DAOS_LIBRARIES}")

set(DAOS_FILE_HEADER "${DAOS_INCLUDE_DIR}/daos.h")
if (EXISTS ${DAOS_FILE_HEADER})
  file(STRINGS ${DAOS_FILE_HEADER} DAOS_MAJOR REGEX
    "LIBDAOS_FILE_VER_MAJOR (\\d*).*$")
  string(REGEX REPLACE ".+LIBDAOS_FILE_VER_MAJOR (\\d*)" "\\1" DAOS_MAJOR
    "${DAOS_MAJOR}")

  file(STRINGS ${DAOS_FILE_HEADER} DAOS_MINOR REGEX
    "LIBDAOS_FILE_VER_MINOR (\\d*).*$")
  string(REGEX REPLACE ".+LIBDAOS_FILE_VER_MINOR (\\d*)" "\\1" DAOS_MINOR
    "${DAOS_MINOR}")

  file(STRINGS ${DAOS_FILE_HEADER} DAOS_EXTRA REGEX
    "LIBDAOS_FILE_VER_EXTRA (\\d*).*$")
  string(REGEX REPLACE ".+LIBDAOS_FILE_VER_EXTRA (\\d*)" "\\1" DAOS_EXTRA
    "${DAOS_EXTRA}")

  set(DAOS_FILE_VERSION "${DAOS_MAJOR}.${DAOS_MINOR}.${DAOS_EXTRA}")
else()
  set(DAOS_FILE_VERSION "0.0.0")
endif()

# handle the QUIETLY and REQUIRED arguments and set PRELUDE_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(DAOS
  REQUIRED_VARS DAOS_INCLUDE_DIR DAOS_LIBRARY_DIR
  VERSION_VAR DAOS_FILE_VERSION
  )
# VERSION FPHSA options not handled by CMake version < 2.8.2)
#                                  VERSION_VAR)

mark_as_advanced(DAOS_INCLUDE_DIR)
mark_as_advanced(DAOS_LIBRARY_DIR)
