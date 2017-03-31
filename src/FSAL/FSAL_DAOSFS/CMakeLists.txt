add_definitions(
  -D_FILE_OFFSET_BITS=64
)

SET(fsaldaosfs_LIB_SRCS
  main.c
  export.c
  handle.c
  internal.c
  internal.h
)

message("DAOSFS_INCLUDE_DIR ${DAOSFS_INCLUDE_DIR} ${DAOS_INCLUDE_DIR}")
include_directories(${DAOSFS_INCLUDE_DIR} ${DAOS_INCLUDE_DIR})

add_library(fsaldaosfs MODULE ${fsaldaosfs_LIB_SRCS})
add_sanitizers(fsaldaosfs)

target_link_libraries(fsaldaosfs ${DAOSFS_LIBRARIES} ${DAOS_LIBRARIES} ${SYSTEM_LIBRARIES})

set_target_properties(fsaldaosfs PROPERTIES VERSION 0.0.1 SOVERSION 0)
install(TARGETS fsaldaosfs COMPONENT fsal DESTINATION ${FSAL_DESTINATION} )
