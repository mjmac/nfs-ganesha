/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 */

/**
 * @file   internal.c
 * @brief Internal definitions for the DAOSFS FSAL
 *
 * This file includes internal function definitions, constants, and
 * variable declarations used to impelment the DAOSFS FSAL, but not
 * exposed as part of the API.
 */

#include <sys/stat.h>
#include "fsal_types.h"
#include "fsal.h"
#include "fsal_convert.h"
#include "FSAL/fsal_commonlib.h"

#define DAOSFS_INTERNAL_C
#include "internal.h"


struct daosfs_fsal_module DAOSFSFSM;


/**
 * @brief FSAL status from DAOSFS error
 *
 * This function returns a fsal_status_t with the FSAL error as the
 * major, and the posix error as minor.	 (DAOSFS's error codes are just
 * negative signed versions of POSIX error codes.)
 *
 * @param[in] daosfs_errorcode DAOSFS error (negative Posix)
 *
 * @return FSAL status.
 */

fsal_status_t daosfs2fsal_error(const int daosfs_errorcode)
{
	fsal_status_t status;
	status.minor = -daosfs_errorcode;

	switch (-daosfs_errorcode) {

	case 0:
		status.major = ERR_FSAL_NO_ERROR;
		break;

	case EPERM:
		status.major = ERR_FSAL_PERM;
		break;

	case ENOENT:
		status.major = ERR_FSAL_NOENT;
		break;

	case ECONNREFUSED:
	case ECONNABORTED:
	case ECONNRESET:
	case EIO:
	case ENFILE:
	case EMFILE:
	case EPIPE:
		status.major = ERR_FSAL_IO;
		break;

	case ENODEV:
	case ENXIO:
		status.major = ERR_FSAL_NXIO;
		break;

	case EBADF:
		/**
		 * @todo: The EBADF error also happens when file is
		 *	  opened for reading, and we try writting in
		 *	  it.  In this case, we return
		 *	  ERR_FSAL_NOT_OPENED, but it doesn't seems to
		 *	  be a correct error translation.
		 */
		status.major = ERR_FSAL_NOT_OPENED;
		break;

	case ENOMEM:
		status.major = ERR_FSAL_NOMEM;
		break;

	case EACCES:
		status.major = ERR_FSAL_ACCESS;
		break;

	case EFAULT:
		status.major = ERR_FSAL_FAULT;
		break;

	case EEXIST:
		status.major = ERR_FSAL_EXIST;
		break;

	case EXDEV:
		status.major = ERR_FSAL_XDEV;
		break;

	case ENOTDIR:
		status.major = ERR_FSAL_NOTDIR;
		break;

	case EISDIR:
		status.major = ERR_FSAL_ISDIR;
		break;

	case EINVAL:
		status.major = ERR_FSAL_INVAL;
		break;

	case EFBIG:
		status.major = ERR_FSAL_FBIG;
		break;

	case ENOSPC:
		status.major = ERR_FSAL_NOSPC;
		break;

	case EMLINK:
		status.major = ERR_FSAL_MLINK;
		break;

	case EDQUOT:
		status.major = ERR_FSAL_DQUOT;
		break;

	case ENAMETOOLONG:
		status.major = ERR_FSAL_NAMETOOLONG;
		break;

	case ENOTEMPTY:
		status.major = ERR_FSAL_NOTEMPTY;
		break;

	case ESTALE:
		status.major = ERR_FSAL_STALE;
		break;

	case EAGAIN:
	case EBUSY:
		status.major = ERR_FSAL_DELAY;
		break;

	default:
		status.major = ERR_FSAL_SERVERFAULT;
		break;
	}

	return status;
}

/**
 * @brief Construct a new filehandle
 *
 * This function constructs a new DAOSFS FSAL object handle and attaches
 * it to the export.  After this call the attributes have been filled
 * in and the handle is up-to-date and usable.
 *
 * @param[in]  export Export on which the object lives
 * @param[in]  daosfs_fh Concise representation of the object name,
 *                    in DAOSFS notation
 * @param[inout] st   Object attributes
 * @param[out] obj    Object created
 *
 * @return 0 on success, negative error codes on failure.
 */

int construct_handle(struct daosfs_fsal_export *export,
		     daosfs_ptr_t node_ptr,
		     struct stat *st,
		     struct daosfs_fsal_handle **obj)

{
	int rc;
	/* Pointer to the handle under construction */
	struct daosfs_fsal_handle *constructing = NULL;
	struct daosfs_node_handle *nh = NULL;
	*obj = NULL;

	constructing = gsh_calloc(1, sizeof(struct daosfs_fsal_handle));
	if (constructing == NULL)
		return -ENOMEM;

	rc = DaosFileSystemGetNodeHandle(node_ptr, &nh);
	if (rc != 0) {
		return rc;
	}
	constructing->node_handle = nh;

	fsal_obj_handle_init(&constructing->handle, &export->export,
			     posix2fsal_type(st->st_mode));
	handle_ops_init(&constructing->handle.obj_ops);
	constructing->handle.fsid = posix2fsal_fsid(st->st_dev);
	constructing->handle.fileid = st->st_ino;

	constructing->export = export;

	*obj = constructing;

	return 0;
}

void deconstruct_handle(struct daosfs_fsal_handle *obj)
{
	DaosFileSystemFreeNodeHandle(obj->node_handle);
	fsal_obj_handle_fini(&obj->handle);
	gsh_free(obj);
}
