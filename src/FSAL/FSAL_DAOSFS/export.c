/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
  */

/* export.c
 * DAOSFS FSAL export object
 */

#include <limits.h>
#include <stdint.h>
#include <sys/statvfs.h>
#include "abstract_mem.h"
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_api.h"
#include "FSAL/fsal_commonlib.h"
#include "FSAL/fsal_config.h"
#include "internal.h"

/**
 * @brief Clean up an export
 *
 * This function cleans up an export after the last reference is
 * released.
 *
 * @param[in,out] export The export to be released
 *
 * @retval ERR_FSAL_NO_ERROR on success.
 * @retval ERR_FSAL_BUSY if the export is in use.
 */

static void release(struct fsal_export *export_pub)
{
	/* The private, expanded export */
	struct daosfs_fsal_export *export =
	    container_of(export_pub, struct daosfs_fsal_export, export);

	// This segfaults somewhere deep inside mercury for some reason,
	// but it's fine when it's called from LibDaosFileSystemFini().
	// Race?
	/*int rc = CloseDaosFileSystem(export->fs);
	assert(rc == 0);*/
	deconstruct_handle(export->root);
	export->fs = NULL;
	export->root = NULL;

	fsal_detach_export(export->export.fsal, &export->export.exports);
	free_export_ops(&export->export);

	gsh_free(export);
	export = NULL;
}

/**
 * @brief Return a handle corresponding to a path
 *
 * This function looks up the given path and supplies an FSAL object
 * handle.
 *
 * @param[in]  export_pub The export in which to look up the file
 * @param[in]  path       The path to look up
 * @param[out] pub_handle The created public FSAL handle
 *
 * @return FSAL status.
 */

static fsal_status_t lookup_path(struct fsal_export *export_pub,
				 const char *path,
				 struct fsal_obj_handle **pub_handle,
				 struct attrlist *attrs_out)
{
	/* The 'private' full export handle */
	struct daosfs_fsal_export *export =
	    container_of(export_pub, struct daosfs_fsal_export, export);
	/* The 'private' full object handle */
	struct daosfs_fsal_handle *handle = NULL;
	/* FSAL status structure */
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	/* The buffer in which to store stat info */
	struct stat st;
	/* Return code from DAOSFS */
	int rc;
	/* temp handle */
	struct daosfs_node_handle *nh = NULL;

	*pub_handle = NULL;

	rc = DaosFileSystemLookupPath(export->root->node_handle,
					(char *)path, &nh);
	if (rc < 0)
		return daosfs2fsal_error(rc);

	/* get Unix attrs */
	rc = DaosFileSystemGetAttr(nh, &st);
	if (rc < 0) {
		return daosfs2fsal_error(rc);
	}

	rc = construct_handle(export, nh->node_ptr, &st, &handle);
	if (rc < 0) {
		return daosfs2fsal_error(rc);
	}

	*pub_handle = &handle->handle;

	if (attrs_out != NULL) {
		posix2fsal_attributes(&st, attrs_out);
	}

	return status;
}

/**
 * @brief Decode a digested handle
 *
 * This function decodes a previously digested handle.
 *
 * @param[in]  exp_handle  Handle of the relevant fs export
 * @param[in]  in_type  The type of digest being decoded
 * @param[out] fh_desc  Address and length of key
 */
static fsal_status_t extract_handle(struct fsal_export *exp_hdl,
				    fsal_digesttype_t in_type,
				    struct gsh_buffdesc *fh_desc,
				    int flags)
{
	switch (in_type) {
		/* Digested Handles */
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		/* wire handles */
		fh_desc->len = sizeof(struct daosfs_node_key);
		break;
	default:
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Create a handle object from a wire handle
 *
 * The wire handle is given in a buffer outlined by desc, which it
 * looks like we shouldn't modify.
 *
 * @param[in]  export_pub Public export
 * @param[in]  desc       Handle buffer descriptor
 * @param[out] pub_handle The created handle
 *
 * @return FSAL status.
 */
static fsal_status_t create_handle(struct fsal_export *export_pub,
				   struct gsh_buffdesc *desc,
				   struct fsal_obj_handle **pub_handle,
				   struct attrlist *attrs_out)
{
	/* Full 'private' export structure */
	struct daosfs_fsal_export *export =
	    container_of(export_pub, struct daosfs_fsal_export, export);

	/* FSAL status to return */
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	/* The FSAL specific portion of the handle received by the
	   client */
	int rc = 0;
	/* Stat buffer */
	struct stat st;
	/* Handle to be created */
	struct daosfs_fsal_handle *handle = NULL;
	/* DAOSFS node hash key */
	struct daosfs_node_key nk;
	/* DAOSFS node handle instance */
	struct daosfs_node_handle *nh = NULL;

	*pub_handle = NULL;

	if (desc->len != sizeof(struct daosfs_node_key)) {
		status.major = ERR_FSAL_INVAL;
		return status;
	}

	memcpy((char *) &nk, desc->addr, desc->len);

	rc = DaosFileSystemLookupHandle(export->fs, &nk, &nh);
	if (rc < 0)
		return daosfs2fsal_error(-ESTALE);

	rc = DaosFileSystemGetAttr(nh, &st);
	if (rc < 0)
		return daosfs2fsal_error(rc);

	rc = construct_handle(export, nh->node_ptr, &st, &handle);
	if (rc < 0) {
		return daosfs2fsal_error(rc);
	}

	*pub_handle = &handle->handle;

	if (attrs_out != NULL) {
		posix2fsal_attributes(&st, attrs_out);
	}

	return status;
}

/**
 * @brief Get dynamic filesystem info
 *
 * This function returns dynamic filesystem information for the given
 * export.
 *
 * @param[in]  export_pub The public export handle
 * @param[out] info       The dynamic FS information
 *
 * @return FSAL status.
 */
static fsal_status_t get_fs_dynamic_info(struct fsal_export *export_pub,
					 struct fsal_obj_handle *obj_hdl,
					 fsal_dynamicfsinfo_t *info)
{
	/* Full 'private' export */
	struct daosfs_fsal_export *export =
	    container_of(export_pub, struct daosfs_fsal_export, export);

	int rc = 0;

	/* Filesystem stat */
	struct daosfs_statvfs vfs_st;

	rc = DaosFileSystemStatFs(export->fs, &vfs_st);
	if (rc < 0)
		return daosfs2fsal_error(rc);

	/* TODO: implement in daosfs_file */
	memset(info, 0, sizeof(fsal_dynamicfsinfo_t));
	info->total_bytes = vfs_st.f_frsize * vfs_st.f_blocks;
	info->free_bytes = vfs_st.f_frsize * vfs_st.f_bfree;
	info->avail_bytes = vfs_st.f_frsize * vfs_st.f_bavail;
	info->total_files = vfs_st.f_files;
	info->free_files = vfs_st.f_ffree;
	info->avail_files = vfs_st.f_favail;
	info->time_delta.tv_sec = 1;
	info->time_delta.tv_nsec = 0;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Query the FSAL's capabilities
 *
 * This function queries the capabilities of an FSAL export.
 *
 * @param[in] export_pub The public export handle
 * @param[in] option     The option to check
 *
 * @retval true if the option is supported.
 * @retval false if the option is unsupported (or unknown).
 */

static bool fs_supports(struct fsal_export *export_pub,
			fsal_fsinfo_options_t option)
{
	struct fsal_staticfsinfo_t *info = daosfs_staticinfo(export_pub->fsal);
	return fsal_supports(info, option);
}

/**
 * @brief Return the longest file supported
 *
 * This function returns the length of the longest file supported.
 *
 * @param[in] export_pub The public export
 *
 * @return UINT64_MAX.
 */

static uint64_t fs_maxfilesize(struct fsal_export *export_pub)
{
	return UINT64_MAX;
}

/**
 * @brief Return the longest read supported
 *
 * This function returns the length of the longest read supported.
 *
 * @param[in] export_pub The public export
 *
 * @return 4 mebibytes.
 */

static uint32_t fs_maxread(struct fsal_export *export_pub)
{
	return 0x400000;
}

/**
 * @brief Return the longest write supported
 *
 * This function returns the length of the longest write supported.
 *
 * @param[in] export_pub The public export
 *
 * @return 4 mebibytes.
 */

static uint32_t fs_maxwrite(struct fsal_export *export_pub)
{
	return 0x400000;
}

/**
 * @brief Return the maximum number of hard links to a file
 *
 * This function returns the maximum number of hard links supported to
 * any file.
 *
 * @param[in] export_pub The public export
 *
 * @return 1024.
 */

static uint32_t fs_maxlink(struct fsal_export *export_pub)
{
	/* Ceph does not like hard links.  See the anchor table
	   design.  We should fix this, but have to do it in the Ceph
	   core. */
	return 1024;
}

/**
 * @brief Return the maximum size of a Ceph filename
 *
 * This function returns the maximum filename length.
 *
 * @param[in] export_pub The public export
 *
 * @return UINT32_MAX.
 */

static uint32_t fs_maxnamelen(struct fsal_export *export_pub)
{
	/* Ceph actually supports filenames of unlimited length, at
	   least according to the protocol docs.  We may wish to
	   constrain this later. */
	return UINT32_MAX;
}

/**
 * @brief Return the maximum length of a Ceph path
 *
 * This function returns the maximum path length.
 *
 * @param[in] export_pub The public export
 *
 * @return UINT32_MAX.
 */

static uint32_t fs_maxpathlen(struct fsal_export *export_pub)
{
	/* Similarly unlimited int he protocol */
	return UINT32_MAX;
}

/**
 * @brief Return the lease time
 *
 * This function returns the lease time.
 *
 * @param[in] export_pub The public export
 *
 * @return five minutes.
 */

static struct timespec fs_lease_time(struct fsal_export *export_pub)
{
	struct timespec lease = { 300, 0 };

	return lease;
}

/**
 * @brief Return ACL support
 *
 * This function returns the export's ACL support.
 *
 * @param[in] export_pub The public export
 *
 * @return FSAL_ACLSUPPORT_DENY.
 */

static fsal_aclsupp_t fs_acl_support(struct fsal_export *export_pub)
{
	return FSAL_ACLSUPPORT_DENY;
}

/**
 * @brief Return the attributes supported by this FSAL
 *
 * This function returns the mask of attributes this FSAL can support.
 *
 * @param[in] export_pub The public export
 *
 * @return supported_attributes as defined in internal.c.
 */

static attrmask_t fs_supported_attrs(struct fsal_export *export_pub)
{
	return daosfs_supported_attributes;
}

/**
 * @brief Return the mode under which the FSAL will create files
 *
 * This function modifies the default mode on any new file created.
 *
 * @param[in] export_pub The public export
 *
 * @return 0 (usually).  Bits set here turn off bits in created files.
 */

static uint32_t fs_umask(struct fsal_export *export_pub)
{
	return fsal_umask(daosfs_staticinfo(export_pub->fsal));
}

/**
 * @brief Return the mode for extended attributes
 *
 * This function returns the access mode applied to extended
 * attributes.  Dubious.
 *
 * @param[in] export_pub The public export
 *
 * @return 0644.
 */

static uint32_t fs_xattr_access_rights(struct fsal_export *export_pub)
{
	return fsal_xattr_access_rights(daosfs_staticinfo(export_pub->fsal));
}

/**
 * @brief Set operations for exports
 *
 * This function overrides operations that we've implemented, leaving
 * the rest for the default.
 *
 * @param[in,out] ops Operations vector
 */

void export_ops_init(struct export_ops *ops)
{
	ops->release = release;
	ops->lookup_path = lookup_path;
	ops->extract_handle = extract_handle;
	ops->create_handle = create_handle;
	ops->get_fs_dynamic_info = get_fs_dynamic_info;
	ops->fs_supports = fs_supports;
	ops->fs_maxfilesize = fs_maxfilesize;
	ops->fs_maxread = fs_maxread;
	ops->fs_maxwrite = fs_maxwrite;
	ops->fs_maxlink = fs_maxlink;
	ops->fs_maxnamelen = fs_maxnamelen;
	ops->fs_maxpathlen = fs_maxpathlen;
	ops->fs_lease_time = fs_lease_time;
	ops->fs_acl_support = fs_acl_support;
	ops->fs_supported_attrs = fs_supported_attrs;
	ops->fs_umask = fs_umask;
	ops->fs_xattr_access_rights = fs_xattr_access_rights;
	ops->alloc_state = alloc_state;
}
