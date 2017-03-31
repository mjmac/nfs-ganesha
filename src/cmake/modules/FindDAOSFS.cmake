# - Find DAOSFS
# Find the DAOS Filesystem
# See https://github.com/daos-stack/
#
# This module accepts the following optional variables:
#    DAOSFS_PREFIX   = A hint on DAOSFS install path.
#
# This module defines the following variables:
#    DAOSFS_FOUND       = Was DAOSFS found or not?
#    DAOSFS_LIBRARIES   = The list of libraries to link to when using DAOSFS
#    DAOSFS_INCLUDE_DIR = The path to DAOSFS include directory
#
# On can set DAOSFS_PREFIX before using find_package(DAOSFS) and the
# module with use the PATH as a hint to find DAOSFS.
#
# The hint can be given on the command line too:
#   cmake -DDAOSFS_PREFIX=/DATA/ERIC/DAOSFS /path/to/source

if(DAOSFS_PREFIX)
  message(STATUS "FindDAOSFS: using PATH HINT: ${DAOSFS_PREFIX}")
  # Try to make the prefix override the normal paths
  find_path(DAOSFS_INCLUDE_DIR
    NAMES include/libdaosfs.h
    PATHS ${DAOSFS_PREFIX}
    NO_DEFAULT_PATH
    DOC "The DAOSFS include headers")
    message("DAOSFS_INCLUDE_DIR ${DAOSFS_INCLUDE_DIR}")

  find_path(DAOSFS_LIBRARY_DIR
    NAMES libdaosfs.so
    PATHS ${DAOSFS_PREFIX}
    PATH_SUFFIXES lib lib64
    NO_DEFAULT_PATH
    DOC "The DAOSFS libraries")
endif()

if (NOT DAOSFS_INCLUDE_DIR)
  find_path(DAOSFS_INCLUDE_DIR
    NAMES libdaosfs.h
    PATHS ${DAOSFS_PREFIX}
    DOC "The DAOSFS include headers")
endif (NOT DAOSFS_INCLUDE_DIR)

message(STATUS "Found daosfs headers: ${DAOSFS_INCLUDE_DIR}")

if (NOT DAOSFS_LIBRARY_DIR)
  find_path(DAOSFS_LIBRARY_DIR
    NAMES libdaosfs.so
    PATHS ${DAOSFS_PREFIX}
    PATH_SUFFIXES lib lib64
    DOC "The DAOSFS libraries")
endif (NOT DAOSFS_LIBRARY_DIR)

find_library(DAOSFS_LIBRARY daosfs PATHS ${DAOSFS_LIBRARY_DIR} NO_DEFAULT_PATH)
#check_library_exists(daosfs OpenDaosFileSystem ${DAOSFS_LIBRARY_DIR} DAOSFSLIB)
#if (NOT DAOSFSLIB)
#  unset(DAOSFS_LIBRARY_DIR CACHE)
#  unset(DAOSFS_INCLUDE_DIR CACHE)
#endif (NOT DAOSFSLIB)

set(DAOSFS_LIBRARIES ${DAOSFS_LIBRARY})
message(STATUS "Found daosfs libraries: ${DAOSFS_LIBRARIES}")

set(DAOSFS_FILE_HEADER "${DAOSFS_INCLUDE_DIR}/libdaosfs.h")
if (EXISTS ${DAOSFS_FILE_HEADER})
  file(STRINGS ${DAOSFS_FILE_HEADER} DAOSFS_MAJOR REGEX
    "LIBDAOSFS_FILE_VER_MAJOR (\\d*).*$")
  string(REGEX REPLACE ".+LIBDAOSFS_FILE_VER_MAJOR (\\d*)" "\\1" DAOSFS_MAJOR
    "${DAOSFS_MAJOR}")

  file(STRINGS ${DAOSFS_FILE_HEADER} DAOSFS_MINOR REGEX
    "LIBDAOSFS_FILE_VER_MINOR (\\d*).*$")
  string(REGEX REPLACE ".+LIBDAOSFS_FILE_VER_MINOR (\\d*)" "\\1" DAOSFS_MINOR
    "${DAOSFS_MINOR}")

  file(STRINGS ${DAOSFS_FILE_HEADER} DAOSFS_EXTRA REGEX
    "LIBDAOSFS_FILE_VER_EXTRA (\\d*).*$")
  string(REGEX REPLACE ".+LIBDAOSFS_FILE_VER_EXTRA (\\d*)" "\\1" DAOSFS_EXTRA
    "${DAOSFS_EXTRA}")

  set(DAOSFS_FILE_VERSION "${DAOSFS_MAJOR}.${DAOSFS_MINOR}.${DAOSFS_EXTRA}")
else()
  set(DAOSFS_FILE_VERSION "0.0.0")
endif()

# handle the QUIETLY and REQUIRED arguments and set PRELUDE_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(DAOSFS
  REQUIRED_VARS DAOSFS_INCLUDE_DIR DAOSFS_LIBRARY_DIR
  VERSION_VAR DAOSFS_FILE_VERSION
  )
# VERSION FPHSA options not handled by CMake version < 2.8.2)
#                                  VERSION_VAR)

mark_as_advanced(DAOSFS_INCLUDE_DIR)
mark_as_advanced(DAOSFS_LIBRARY_DIR)
