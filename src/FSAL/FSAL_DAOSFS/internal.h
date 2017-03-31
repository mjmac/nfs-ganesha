/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 */

/**
 * @file   internal.h
 * @brief Internal declarations for the DAOSFS FSAL
 *
 * This file includes declarations of data types, functions,
 * variables, and constants for the DAOSFS FSAL.
 */

#ifndef FSAL_DAOSFS_INTERNAL
#define FSAL_DAOSFS_INTERNAL

#include <stdbool.h>
#include <uuid/uuid.h>
#include <dirent.h> /* NAME_MAX */

#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "fsal_convert.h"
#include "sal_data.h"

#include <include/daosfs_types.h>
#include <include/libdaosfs.h>

/**
 * DAOSFS Main (global) module object
 */

struct daosfs_fsal_module {
	struct fsal_module 	fsal;
	fsal_staticfsinfo_t 	fs_info;
	char 			*init_args;
	daosfs_t 		daosfs;	/* opaque library handle */
};
extern struct daosfs_fsal_module DAOSFSFSM;

#define MAXUUIDLEN 36

/**
 * DAOSFS internal export object
 */

struct daosfs_fsal_export {
	struct fsal_export 		export;	/*< The public export object */
	struct daosfs_fsal_handle	*root;	/*< root handle */
	struct daosfs_fs_handle 	*fs;	/*< "Opaque" fs handle */
	char *daos_server_group;
	char *daos_pool_uuid;
	char *daos_fs_container;
};

/**
 * The DAOSFS FSAL internal handle
 */

struct daosfs_fsal_handle {
	struct fsal_obj_handle handle;	/*< The public handle */
	struct daosfs_node_handle *node_handle;  /*< DAOSFS node handle */
	/* XXXX remove ptr to up-ops--we can always follow export! */
	const struct fsal_up_vector *up_ops;	/*< Upcall operations */
	struct daosfs_fsal_export *export;	/*< The first export this handle
					 *< belongs to */
	struct fsal_share share;
	fsal_openflags_t openflags;
};

/**
 * DAOSFS "file descriptor"
 */
struct daosfs_fsal_open_state {
	struct state_t gsh_open;
	uint32_t flags;
};

/**
 * The attributes this FSAL can interpret or supply.
 */
#define daosfs_supported_attributes (const attrmask_t) (	   \
	ATTR_TYPE      | ATTR_SIZE     | ATTR_FSID  | ATTR_FILEID |\
	ATTR_MODE      | ATTR_NUMLINKS | ATTR_OWNER | ATTR_GROUP  |\
	ATTR_ATIME     | ATTR_RAWDEV   | ATTR_CTIME | ATTR_MTIME  |\
	ATTR_SPACEUSED | ATTR_CHGTIME)

/**
 * The attributes this FSAL can set.
 */
#define daosfs_settable_attributes (const attrmask_t) (		  \
	ATTR_MODE  | ATTR_OWNER | ATTR_GROUP | ATTR_ATIME	 |\
	ATTR_CTIME | ATTR_MTIME | ATTR_SIZE  | ATTR_MTIME_SERVER |\
	ATTR_ATIME_SERVER)

/**
 * Linux supports a stripe pattern with no more than 4096 stripes, but
 * for now we stick to 1024 to keep them da_addrs from being too
 * gigantic.
 */

static const size_t BIGGEST_PATTERN = 1024;

static inline fsal_staticfsinfo_t *daosfs_staticinfo(struct fsal_module *hdl)
{
	struct daosfs_fsal_module *myself =
	    container_of(hdl, struct daosfs_fsal_module, fsal);
	return &myself->fs_info;
}

/* Prototypes */
int construct_handle(struct daosfs_fsal_export *export, daosfs_ptr_t node_ptr,
		     struct stat *st,
		     struct daosfs_fsal_handle **obj);
void deconstruct_handle(struct daosfs_fsal_handle *obj);

fsal_status_t daosfs2fsal_error(const int errorcode);

void export_ops_init(struct export_ops *ops);
void handle_ops_init(struct fsal_obj_ops *ops);

struct state_t *alloc_state(struct fsal_export *exp_hdl,
			enum state_type state_type,
			struct state_t *related_state);
/*
void daosfs_fs_invalidate(void *handle, struct daosfs_fh_hk fh_hk);
*/
#endif				/* !FSAL_DAOSFS_INTERNAL */
