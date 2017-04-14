/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 */

/* handle.c
 * DAOSFS object (file|dir) handle object
 */

#include <fcntl.h>
#include "fsal.h"
#include "fsal_types.h"
#include "fsal_convert.h"
#include "fsal_api.h"
#include "internal.h"
#include "nfs_exports.h"
#include "FSAL/fsal_commonlib.h"

/**
 * @brief Release an object
 *
 * @param[in] obj_hdl The object to release
 *
 * @return FSAL status codes.
 */

static void release(struct fsal_obj_handle *obj_hdl)
{
	struct daosfs_fsal_handle *obj =
		container_of(obj_hdl, struct daosfs_fsal_handle, handle);
	struct daosfs_fsal_export *export = obj->export;

	if (obj->node_handle != export->root->node_handle) {
		/* release DAOSFS ref */
		DaosFileSystemFreeNodeHandle(obj->node_handle);
	}
	deconstruct_handle(obj);
}

/**
 * @brief Look up an object by name
 *
 * This function looks up an object by name in a directory.
 *
 * @param[in]     dir_hdl    The directory in which to look up the object.
 * @param[in]     path       The name to look up.
 * @param[in,out] obj_hdl    The looked up object.
 * @param[in,out] attrs_out  Optional attributes for newly created object
 *
 * @return FSAL status codes.
 */

static fsal_status_t lookup_int(struct fsal_obj_handle *dir_hdl,
				const char *path,
				struct fsal_obj_handle **obj_hdl,
				struct attrlist *attrs_out)
{
	int rc;
	struct stat st;
	struct daosfs_fsal_handle *obj = NULL;
	struct daosfs_node_handle *nh = NULL;

	*obj_hdl = NULL;

	struct daosfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct daosfs_fsal_export,
				export);

	struct daosfs_fsal_handle *dir = container_of(dir_hdl,
				struct daosfs_fsal_handle, handle);

	LogFullDebug(COMPONENT_FSAL,
		"%s enter dir_hdl %p path %s", __func__, dir_hdl, path);

	rc = DaosFileSystemLookupPath(dir->node_handle, (char *)path, &nh);
	if (rc < 0)
		return daosfs2fsal_error(rc);

	rc = DaosFileSystemGetAttr(nh, &st);
	if (rc < 0)
		return daosfs2fsal_error(rc);

	rc = construct_handle(export, nh->node_ptr, &st, &obj);
	if (rc < 0) {
		return daosfs2fsal_error(rc);
	}

	*obj_hdl = &obj->handle;

	if (attrs_out != NULL) {
		posix2fsal_attributes(&st, attrs_out);
	}

	return fsalstat(0, 0);
}

static fsal_status_t lookup(struct fsal_obj_handle *dir_hdl,
			const char *path, struct fsal_obj_handle **obj_hdl,
			struct attrlist *attrs_out)
{
	return lookup_int(dir_hdl, path, obj_hdl, attrs_out);
}

struct daosfs_cb_arg {
	fsal_readdir_cb cb;
	void *fsal_arg;
	struct fsal_obj_handle *dir_hdl;
	attrmask_t attrmask;
};

static bool daosfs_cb(char *name, void *arg, uint64_t offset)
{
	struct daosfs_cb_arg *daosfs_cb_arg = arg;
	struct fsal_obj_handle *obj;
	fsal_status_t status;
	struct attrlist attrs;
	bool cb_rc;

	fsal_prepare_attrs(&attrs, daosfs_cb_arg->attrmask);

	status = lookup_int(daosfs_cb_arg->dir_hdl, name, &obj, &attrs);
	if (FSAL_IS_ERROR(status))
		return false;

	cb_rc = daosfs_cb_arg->cb(name, obj, &attrs, daosfs_cb_arg->fsal_arg, offset, NULL);

	fsal_release_attrs(&attrs);

	return cb_rc;
}

/**
 * @brief Read a directory
 *
 * This function reads the contents of a directory and passes dirent
 * information to the supplied callback.
 *
 * @param[in]  dir_hdl     The directory to read
 * @param[in]  whence      The cookie indicating resumption, NULL to start
 * @param[in]  dir_state   Opaque, passed to cb
 * @param[in]  cb          Callback that receives directory entries
 * @param[out] eof         True if there are no more entries
 *
 * @return FSAL status.
 */

static fsal_status_t daosfs_fsal_readdir(struct fsal_obj_handle *dir_hdl,
				  fsal_cookie_t *whence, void *cb_arg,
				  fsal_readdir_cb cb, attrmask_t attrmask,
				  bool *eof)
{
	int rc;
	fsal_status_t fsal_status = {ERR_FSAL_NO_ERROR, 0};

	struct daosfs_cb_arg daosfs_cb_arg = {cb, cb_arg, dir_hdl, attrmask};
	uint64_t r_whence = (whence) ? *whence : 0;

	struct daosfs_fsal_handle *dir = container_of(dir_hdl,
			struct daosfs_fsal_handle, handle);

	LogFullDebug(COMPONENT_FSAL,
		"%s enter dir_hdl %p", __func__, dir_hdl);

	/* MDCACHE assumes we will reach eod, contrary to what the readdir
	 * fsal op signature implies */
	rc = 0;
	*eof = false;
	while ((rc == 0) && (!*eof)) {
		rc = DaosFileSystemReadDir(dir->node_handle, &r_whence,
				daosfs_cb, &daosfs_cb_arg, eof);
	}
	if (rc < 0)
		return daosfs2fsal_error(rc);

	return fsal_status;
}

/**
 * @brief Create a regular file
 *
 * This function creates an empty, regular file.
 *
 * @param[in]     dir_hdl    Directory in which to create the file
 * @param[in]     name       Name of file to create
 * @param[in]     attrs_in   Attributes of newly created file
 * @param[in,out] obj_hdl    Handle for newly created file
 * @param[in,out] attrs_out  Optional attributes for newly created object
 *
 * @return FSAL status.
 */

static fsal_status_t daosfs_fsal_create(struct fsal_obj_handle *dir_hdl,
				const char *name,
				struct attrlist *attrs_in,
				struct fsal_obj_handle **obj_hdl,
				struct attrlist *attrs_out)
{
	int rc;
	struct daosfs_node_handle *nh = NULL;
	struct daosfs_fsal_handle *obj;
	struct stat st;

	struct daosfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct daosfs_fsal_export, export);

	struct daosfs_fsal_handle *dir = container_of(dir_hdl,
			struct daosfs_fsal_handle, handle);

	LogFullDebug(COMPONENT_FSAL,
		"%s enter dir_hdl %p name %s", __func__, dir_hdl, name);

	memset(&st, 0, sizeof(struct stat));

	st.st_uid = op_ctx->creds->caller_uid;
	st.st_gid = op_ctx->creds->caller_gid;
	st.st_mode = fsal2unix_mode(attrs_in->mode)
	    & ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

	rc = DaosFileSystemCreate(dir->node_handle, (char *)name, &st, 
			0 /* posix flags */, &nh);
	if (rc < 0)
		return daosfs2fsal_error(rc);

	rc = construct_handle(export, nh->node_ptr, &st, &obj);
	if (rc < 0) {
		return daosfs2fsal_error(rc);
	}

	*obj_hdl = &obj->handle;

	if (attrs_out != NULL) {
		posix2fsal_attributes(&st, attrs_out);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Create a directory
 *
 * This function creates a new directory.
 *
 * For support_ex, this method will handle attribute setting. The caller
 * MUST include the mode attribute and SHOULD NOT include the owner or
 * group attributes if they are the same as the op_ctx->cred.
 *
 * @param[in]     dir_hdl Directory in which to create the directory
 * @param[in]     name    Name of directory to create
 * @param[in]     attrib  Attributes to set on newly created object
 * @param[out]    new_obj Newly created object
 *
 * @note On success, @a new_obj has been ref'd
 *
 * @return FSAL status.
 */

static fsal_status_t daosfs_fsal_mkdir(struct fsal_obj_handle *dir_hdl,
				const char *name, struct attrlist *attrs_in,
				struct fsal_obj_handle **obj_hdl,
				struct attrlist *attrs_out)
{
	int rc;
	struct daosfs_node_handle *nh = NULL;
	struct daosfs_fsal_handle *obj;
	struct stat st;

	struct daosfs_fsal_export *export =
	    container_of(op_ctx->fsal_export, struct daosfs_fsal_export, export);

	struct daosfs_fsal_handle *dir = container_of(dir_hdl,
			struct daosfs_fsal_handle, handle);

	LogFullDebug(COMPONENT_FSAL,
		"%s enter dir_hdl %p name %s", __func__, dir_hdl, name);

	memset(&st, 0, sizeof(struct stat));

	st.st_uid = op_ctx->creds->caller_uid;
	st.st_gid = op_ctx->creds->caller_gid;
	st.st_mode = fsal2unix_mode(attrs_in->mode)
	    & ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);

	rc = DaosFileSystemMkdir(dir->node_handle, (char *)name, &st, &nh);
	if (rc < 0)
		return daosfs2fsal_error(rc);

	rc = construct_handle(export, nh->node_ptr, &st, &obj);
	if (rc < 0) {
		return daosfs2fsal_error(rc);
	}

	*obj_hdl = &obj->handle;

	if (attrs_out != NULL) {
		posix2fsal_attributes(&st, attrs_out);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Freshen and return attributes
 *
 * This function freshens and returns the attributes of the given
 * file.
 *
 * @param[in]  obj_hdl Object to interrogate
 *
 * @return FSAL status.
 */
static fsal_status_t getattrs(struct fsal_obj_handle *obj_hdl,
			struct attrlist *attrs)
{
	int rc;
	struct stat st;

	struct daosfs_fsal_handle *handle = container_of(obj_hdl,
			struct daosfs_fsal_handle, handle);
						

	LogFullDebug(COMPONENT_FSAL,
		"%s enter obj_hdl %p", __func__, obj_hdl);

	rc = DaosFileSystemGetAttr(handle->node_handle, &st);
	if (rc < 0) {
		if (attrs->request_mask & ATTR_RDATTR_ERR) {
			/* Caller asked for error to be visible. */
			attrs->valid_mask = ATTR_RDATTR_ERR;
		}
		return daosfs2fsal_error(rc);
	}

	posix2fsal_attributes(&st, attrs);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Set attributes on an object
 *
 * This function sets attributes on an object.  Which attributes are
 * set is determined by attrib_set->valid_mask. The FSAL must manage bypass
 * or not of share reservations, and a state may be passed.
 *
 * @param[in] obj_hdl    File on which to operate
 * @param[in] state      state_t to use for this operation
 * @param[in] attrib_set Attributes to set
 *
 * @return FSAL status.
 */
fsal_status_t daosfs_fsal_setattr2(struct fsal_obj_handle *obj_hdl,
				bool bypass,
				struct state_t *state,
				struct attrlist *attrib_set)
{
	fsal_status_t status = {0, 0};
	int rc = 0;
	bool has_lock = false;
	bool closefd = false;
	struct stat st;
	/* Mask of attributes to set */
	uint32_t mask = 0;

	struct daosfs_fsal_handle *handle = container_of(obj_hdl,
			struct daosfs_fsal_handle, handle);

	LogFullDebug(COMPONENT_FSAL,
		"%s enter obj_hdl %p state %p", __func__, obj_hdl, state);

	if (attrib_set->valid_mask & ~daosfs_settable_attributes) {
		LogDebug(COMPONENT_FSAL,
			"bad mask %"PRIx64" not settable %"PRIx64,
			attrib_set->valid_mask,
			attrib_set->valid_mask & ~daosfs_settable_attributes);
		return fsalstat(ERR_FSAL_INVAL, 0);
	}

	LogAttrlist(COMPONENT_FSAL, NIV_FULL_DEBUG,
		    "attrs ", attrib_set, false);

	/* apply umask, if mode attribute is to be changed */
	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_MODE))
		attrib_set->mode &=
			~op_ctx->fsal_export->exp_ops.fs_umask(
				op_ctx->fsal_export);

	/* Test if size is being set, make sure file is regular and if so,
	 * require a read/write file descriptor.
	 */
	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_SIZE)) {
		if (obj_hdl->type != REGULAR_FILE) {
			LogFullDebug(COMPONENT_FSAL,
				"Setting size on non-regular file");
			return fsalstat(ERR_FSAL_INVAL, EINVAL);
		}

		/* We don't actually need an open fd, we are just doing the
		 * share reservation checking, thus the NULL parameters.
		 */
		status = fsal_find_fd(NULL, obj_hdl, NULL, &handle->share,
				bypass, state, FSAL_O_RDWR, NULL, NULL,
				&has_lock, &closefd, false);

		if (FSAL_IS_ERROR(status)) {
			LogFullDebug(COMPONENT_FSAL,
				"fsal_find_fd status=%s",
				fsal_err_txt(status));
			goto out;
		}
	}

	memset(&st, 0, sizeof(struct stat));

	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_SIZE)) {
		rc = DaosFileSystemTruncate(handle->node_handle,
				attrib_set->filesize);

		if (rc < 0) {
			status = daosfs2fsal_error(rc);
			LogDebug(COMPONENT_FSAL,
				"truncate returned %s (%d)",
				strerror(-rc), -rc);
			goto out;
		}
	}

	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_MODE)) {
		mask |= DAOSFS_SETATTR_MODE;
		st.st_mode = fsal2unix_mode(attrib_set->mode);
	}

	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_OWNER)) {
		mask |= DAOSFS_SETATTR_UID;
		st.st_uid = attrib_set->owner;
	}

	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_GROUP)) {
		mask |= DAOSFS_SETATTR_GID;
		st.st_gid = attrib_set->group;
	}

	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_ATIME)) {
		mask |= DAOSFS_SETATTR_ATIME;
		st.st_atim = attrib_set->atime;
	}

	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_ATIME_SERVER)) {
		mask |= DAOSFS_SETATTR_ATIME;
		struct timespec timestamp;

		rc = clock_gettime(CLOCK_REALTIME, &timestamp);
		if (rc != 0) {
			LogDebug(COMPONENT_FSAL,
				"clock_gettime returned %s (%d)",
				strerror(-rc), -rc);
			status = daosfs2fsal_error(rc);
			goto out;
		}
		st.st_atim = timestamp;
	}

	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_MTIME)) {
		mask |= DAOSFS_SETATTR_MTIME;
		st.st_mtim = attrib_set->mtime;
	}
	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_MTIME_SERVER)) {
		mask |= DAOSFS_SETATTR_MTIME;
		struct timespec timestamp;

		rc = clock_gettime(CLOCK_REALTIME, &timestamp);
		if (rc != 0) {
			LogDebug(COMPONENT_FSAL,
				 "clock_gettime returned %s (%d)",
				 strerror(-rc), -rc);
			status = daosfs2fsal_error(rc);
			goto out;
		}
		st.st_mtim = timestamp;
	}

	if (FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_CTIME)) {
		mask |= DAOSFS_SETATTR_CTIME;
		st.st_ctim = attrib_set->ctime;
	}

	rc = DaosFileSystemSetAttr(handle->node_handle, &st, mask);
	if (rc < 0) {
		LogDebug(COMPONENT_FSAL,
			 "setattr returned %s (%d)",
			 strerror(-rc), -rc);

		status = daosfs2fsal_error(rc);
	} else {
		/* Success */
		status = fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

 out:

	if (has_lock)
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	return status;
}

/**
 * @brief Rename a file
 *
 * This function renames a file, possibly moving it into another
 * directory.  We assume most checks are done by the caller.
 *
 * @param[in] olddir_hdl Source directory
 * @param[in] old_name   Original name
 * @param[in] newdir_hdl Destination directory
 * @param[in] new_name   New name
 *
 * @return FSAL status.
 */

static fsal_status_t daosfs_fsal_rename(struct fsal_obj_handle *obj_hdl,
				struct fsal_obj_handle *olddir_hdl,
				const char *old_name,
				struct fsal_obj_handle *newdir_hdl,
				const char *new_name)
{
#if 0
	int rc;

	struct daosfs_export *export =
		container_of(op_ctx->fsal_export, struct daosfs_export, export);

	struct daosfs_handle *olddir = container_of(olddir_hdl, struct daosfs_handle,
						handle);

	struct daosfs_handle *newdir = container_of(newdir_hdl, struct daosfs_handle,
						handle);

	LogFullDebug(COMPONENT_FSAL,
		"%s enter obj_hdl %p olddir_hdl %p oname %s newdir_hdl %p nname %s",
		__func__, obj_hdl, olddir_hdl, old_name, newdir_hdl, new_name);

	rc = daosfs_rename(export->daosfs_fs, olddir->daosfs_fh, old_name,
			newdir->daosfs_fh, new_name, DAOSFS_RENAME_FLAG_NONE);
	if (rc < 0)
		return daosfs2fsal_error(rc);
#endif

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Remove a name
 *
 * This function removes a name from the filesystem and possibly
 * deletes the associated file.  Directories must be empty to be
 * removed.
 *
 * @param[in] dir_hdl The directory from which to remove the name
 * @param[in] obj_hdl The object being removed
 * @param[in] name    The name to remove
 *
 * @return FSAL status.
 */

static fsal_status_t daosfs_fsal_unlink(struct fsal_obj_handle *dir_hdl,
				struct fsal_obj_handle *obj_hdl,
				const char *name)
{
	int rc;

	struct daosfs_fsal_handle *dir = container_of(dir_hdl,
			struct daosfs_fsal_handle, handle);

	LogFullDebug(COMPONENT_FSAL,
		"%s enter dir_hdl %p obj_hdl %p name %s", __func__, dir_hdl,
		obj_hdl, name);

	rc = DaosFileSystemUnlink(dir->node_handle, (char *)name);
	if (rc < 0)
		return daosfs2fsal_error(rc);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Merge a duplicate handle with an original handle
 *
 * This function is used if an upper layer detects that a duplicate
 * object handle has been created. It allows the FSAL to merge anything
 * from the duplicate back into the original.
 *
 * The caller must release the object (the caller may have to close
 * files if the merge is unsuccessful).
 *
 * @param[in]  orig_hdl  Original handle
 * @param[in]  dupe_hdl Handle to merge into original
 *
 * @return FSAL status.
 *
 */

fsal_status_t daosfs_merge(struct fsal_obj_handle *orig_hdl,
			struct fsal_obj_handle *dupe_hdl)
{
	fsal_status_t status = {ERR_FSAL_NO_ERROR, 0};

	if (orig_hdl->type == REGULAR_FILE &&
	    dupe_hdl->type == REGULAR_FILE) {
		/* We need to merge the share reservations on this file.
		 * This could result in ERR_FSAL_SHARE_DENIED.
		 */
		struct daosfs_fsal_handle *orig, *dupe;

		orig = container_of(orig_hdl, struct daosfs_fsal_handle, handle);
		dupe = container_of(dupe_hdl, struct daosfs_fsal_handle, handle);

		/* This can block over an I/O operation. */
		PTHREAD_RWLOCK_wrlock(&orig_hdl->obj_lock);

		status = merge_share(&orig->share, &dupe->share);

		PTHREAD_RWLOCK_unlock(&orig_hdl->obj_lock);
	}

	return status;
}

/**
 * @brief Open a file descriptor for read or write and possibly create
 *
 * This function opens a file for read or write, possibly creating it.
 * If the caller is passing a state, it must hold the state_lock
 * exclusive.
 *
 * state can be NULL which indicates a stateless open (such as via the
 * NFS v3 CREATE operation), in which case the FSAL must assure protection
 * of any resources. If the file is being created, such protection is
 * simple since no one else will have access to the object yet, however,
 * in the case of an exclusive create, the common resources may still need
 * protection.
 *
 * If Name is NULL, obj_hdl is the file itself, otherwise obj_hdl is the
 * parent directory.
 *
 * On an exclusive create, the upper layer may know the object handle
 * already, so it MAY call with name == NULL. In this case, the caller
 * expects just to check the verifier.
 *
 * On a call with an existing object handle for an UNCHECKED create,
 * we can set the size to 0.
 *
 * If attributes are not set on create, the FSAL will set some minimal
 * attributes (for example, mode might be set to 0600).
 *
 * If an open by name succeeds and did not result in Ganesha creating a file,
 * the caller will need to do a subsequent permission check to confirm the
 * open. This is because the permission attributes were not available
 * beforehand.
 *
 * @param[in] obj_hdl               File to open or parent directory
 * @param[in,out] state             state_t to use for this operation
 * @param[in] openflags             Mode for open
 * @param[in] createmode            Mode for create
 * @param[in] name                  Name for file if being created or opened
 * @param[in] attrib_set            Attributes to set on created file
 * @param[in] verifier              Verifier to use for exclusive create
 * @param[in,out] new_obj           Newly created object
 * @param[in,out] caller_perm_check The caller must do a permission check
 *
 * @return FSAL status.
 */

fsal_status_t daosfs_fsal_open2(struct fsal_obj_handle *obj_hdl,
			struct state_t *state,
			fsal_openflags_t openflags,
			enum fsal_create_mode createmode,
			const char *name,
			struct attrlist *attrib_set,
			fsal_verifier_t verifier,
			struct fsal_obj_handle **new_obj,
			struct attrlist *attrs_out,
			bool *caller_perm_check)
{
	int posix_flags = 0;
	int rc;
	mode_t unix_mode;
	fsal_status_t status = {0, 0};
	struct stat st;
	bool truncated;
	bool setattrs = attrib_set != NULL;
	bool created = false;
	struct attrlist verifier_attr;
	struct daosfs_node_handle *nh = NULL;
	struct daosfs_fsal_handle *obj;

	struct daosfs_fsal_export *export =
		container_of(op_ctx->fsal_export, struct daosfs_fsal_export,
				export);

	struct daosfs_fsal_handle *handle = container_of(obj_hdl,
			struct daosfs_fsal_handle, handle);

	LogFullDebug(COMPONENT_FSAL,
		"%s enter obj_hdl %p state %p", __func__, obj_hdl, state);

	if (setattrs)
		LogAttrlist(COMPONENT_FSAL, NIV_FULL_DEBUG,
			    "attrs ", attrib_set, false);

	fsal2posix_openflags(openflags, &posix_flags);

	truncated = (posix_flags & O_TRUNC) != 0;

	/* Now fixup attrs for verifier if exclusive create */
	if (createmode >= FSAL_EXCLUSIVE) {
		if (!setattrs) {
			/* We need to use verifier_attr */
			attrib_set = &verifier_attr;
			memset(&verifier_attr, 0, sizeof(verifier_attr));
		}

		set_common_verifier(attrib_set, verifier);
	}

	if (!name) {
		/* This is an open by handle */
		if (state) {
			/* Prepare to take the share reservation, but only if we
			 * are called with a valid state (if state is NULL the
			 * caller is a stateless create such as NFS v3 CREATE).
			 */

			/* This can block over an I/O operation. */
			PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

			/* Check share reservation conflicts. */
			status = check_share_conflict(&handle->share,
						      openflags, false);

			if (FSAL_IS_ERROR(status)) {
				PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
				return status;
			}

			/* Take the share reservation now by updating the
			 * counters.
			 */
			update_share_counters(&handle->share, FSAL_O_CLOSED,
					      openflags);

			PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
		} else {
			/* DAOSFS doesn't have a file descriptor/open abstraction,
			 * and actually forbids concurrent opens;  This is
			 * where more advanced FSALs would fall back to using
			 * a "global" fd--what we always use;  We still need
			 * to take the lock expected by ULP
			 */
#if 0
			my_fd = &hdl->fd;
#endif
			PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);
		}

		rc = DaosFileSystemOpen(handle->node_handle, posix_flags);
		if (rc < 0) {
			if (!state) {
				/* Release the lock taken above, and return
				 * since there is nothing to undo.
				 */
				PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
				return daosfs2fsal_error(rc);
			} else {
				/* Error - need to release the share */
				goto undo_share;
			}
		}

		if (createmode >= FSAL_EXCLUSIVE || truncated) {
			/* refresh attributes */
			rc = DaosFileSystemGetAttr(handle->node_handle, &st);
			if (rc < 0) {
				status = daosfs2fsal_error(rc);
			} else {
				LogFullDebug(COMPONENT_FSAL,
					"New size = %"PRIx64, st.st_size);
				/* Now check verifier for exclusive, but not for
				 * FSAL_EXCLUSIVE_9P.
				 */
				if (createmode >= FSAL_EXCLUSIVE &&
					createmode != FSAL_EXCLUSIVE_9P &&
					!obj_hdl->obj_ops.check_verifier(
						obj_hdl, verifier)) {
					/* Verifier didn't match */
					status =
						fsalstat(posix2fsal_error(
							EEXIST),
							EEXIST);
				}
			}
		}

		if (!state) {
			/* If no state, release the lock taken above and return
			 * status. If success, we haven't done any permission
			 * check so ask the caller to do so.
			 */
			PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
			*caller_perm_check = !FSAL_IS_ERROR(status);
			return status;
		}

		if (!FSAL_IS_ERROR(status)) {
			/* Return success. We haven't done any permission
			 * check so ask the caller to do so.
			 */
			*caller_perm_check = true;
			return status;
		}

		/* close on error */
		(void) DaosFileSystemClose(handle->node_handle);

 undo_share:

		/* Can only get here with state not NULL and an error */

		/* On error we need to release our share reservation
		 * and undo the update of the share counters.
		 * This can block over an I/O operation.
		 */
		PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

		update_share_counters(&handle->share, openflags, FSAL_O_CLOSED);

		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

		return status;
	} /* !name */

	/* In this path where we are opening by name, we can't check share
	 * reservation yet since we don't have an object_handle yet. If we
	 * indeed create the object handle (there is no race with another
	 * open by name), then there CAN NOT be a share conflict, otherwise
	 * the share conflict will be resolved when the object handles are
	 * merged.
	 */

	if (createmode == FSAL_NO_CREATE) {
		/* Non creation case, libdaosfs doesn't have open by name so we
		 * have to do a lookup and then handle as an open by handle.
		 */
		struct fsal_obj_handle *temp = NULL;

		/* We don't have open by name... */
		status = obj_hdl->obj_ops.lookup(obj_hdl, name, &temp, NULL);

		if (FSAL_IS_ERROR(status)) {
			LogFullDebug(COMPONENT_FSAL,
				     "lookup returned %s",
				     fsal_err_txt(status));
			return status;
		}

		/* Now call ourselves without name and attributes to open. */
		status = obj_hdl->obj_ops.open2(temp, state, openflags,
						FSAL_NO_CREATE, NULL, NULL,
						verifier, new_obj,
						attrs_out,
						caller_perm_check);

		if (FSAL_IS_ERROR(status)) {
			/* Release the object we found by lookup. */
			temp->obj_ops.release(temp);
			LogFullDebug(COMPONENT_FSAL,
				     "open returned %s",
				     fsal_err_txt(status));
		}

		return status;
	}

	/* Now add in O_CREAT and O_EXCL.
	 * Even with FSAL_UNGUARDED we try exclusive create first so
	 * we can safely set attributes.
	 */
	if (createmode != FSAL_NO_CREATE) {
		posix_flags |= O_CREAT;

		if (createmode >= FSAL_GUARDED || setattrs)
			posix_flags |= O_EXCL;
	}

	if (setattrs && FSAL_TEST_MASK(attrib_set->valid_mask, ATTR_MODE)) {
		unix_mode = fsal2unix_mode(attrib_set->mode) &
		    ~op_ctx->fsal_export->exp_ops.fs_umask(op_ctx->fsal_export);
		/* Don't set the mode if we later set the attributes */
		FSAL_UNSET_MASK(attrib_set->valid_mask, ATTR_MODE);
	} else {
		/* Default to mode 0600 */
		unix_mode = 0600;
	}

	memset(&st, 0, sizeof(struct stat)); /* XXX needed? */

	st.st_uid = op_ctx->creds->caller_uid;
	st.st_gid = op_ctx->creds->caller_gid;
	st.st_mode = unix_mode;

	rc = DaosFileSystemCreate(handle->node_handle, (char *)name, &st,
			posix_flags, &nh);
	if (rc < 0) {
		LogFullDebug(COMPONENT_FSAL,
			     "Create %s failed with %s",
			     name, strerror(-rc));
	}

	/* XXX won't get here, but maybe someday */
	if (rc == -EEXIST && createmode == FSAL_UNCHECKED) {
		/* We tried to create O_EXCL to set attributes and failed.
		 * Remove O_EXCL and retry, also remember not to set attributes.
		 * We still try O_CREAT again just in case file disappears out
		 * from under us.
		 */
		posix_flags &= ~O_EXCL;
		rc = DaosFileSystemCreate(handle->node_handle, (char *)name,
				&st, posix_flags, &nh);
		if (rc < 0) {
			LogFullDebug(COMPONENT_FSAL,
				     "Non-exclusive Create %s failed with %s",
				     name, strerror(-rc));
		}
	}

	if (rc < 0) {
		return daosfs2fsal_error(rc);
	}

	/* Remember if we were responsible for creating the file.
	 * Note that in an UNCHECKED retry we MIGHT have re-created the
	 * file and won't remember that. Oh well, so in that rare case we
	 * leak a partially created file if we have a subsequent error in here.
	 * Since we were able to do the permission check even if we were not
	 * creating the file, let the caller know the permission check has
	 * already been done. Note it IS possible in the case of a race between
	 * an UNCHECKED open and an external unlink, we did create the file.
	 */
	created = (posix_flags & O_EXCL) != 0;
	*caller_perm_check = false;

	construct_handle(export, nh->node_ptr, &st, &obj);

	/* here FSAL_CEPH operates on its (for DAOSFS non-existent) global
	 * fd */
#if 0
	/* If we didn't have a state above, use the global fd. At this point,
	 * since we just created the global fd, no one else can have a
	 * reference to it, and thus we can mamnipulate unlocked which is
	 * handy since we can then call setattr2 which WILL take the lock
	 * without a double locking deadlock.
	 */
	if (my_fd == NULL)
		my_fd = &hdl->fd;

	my_fd->fd = fd;
#endif
	handle->openflags = openflags;

	*new_obj = &obj->handle;

	rc = DaosFileSystemOpen(nh, posix_flags);
	if (rc < 0) {
		goto fileerr;
	}

	if (created && setattrs && attrib_set->valid_mask != 0) {
		/* Set attributes using our newly opened file descriptor as the
		 * share_fd if there are any left to set (mode and truncate
		 * have already been handled).
		 *
		 * Note that we only set the attributes if we were responsible
		 * for creating the file.
		 */
		status = (*new_obj)->obj_ops.setattr2(*new_obj,
						      false,
						      state,
						      attrib_set);

		if (FSAL_IS_ERROR(status)) {
			/* Release the handle we just allocated. */
			(*new_obj)->obj_ops.release(*new_obj);
			*new_obj = NULL;
			goto fileerr;
		}

		if (attrs_out != NULL) {
			status = (*new_obj)->obj_ops.getattrs(*new_obj,
							      attrs_out);
			if (FSAL_IS_ERROR(status) &&
			    (attrs_out->request_mask & ATTR_RDATTR_ERR) == 0) {
				/* Get attributes failed and caller expected
				 * to get the attributes. Otherwise continue
				 * with attrs_out indicating ATTR_RDATTR_ERR.
				 */
				goto fileerr;
			}
		}
	} else if (attrs_out != NULL) {
		/* Since we haven't set any attributes other than what was set
		 * on create (if we even created), just use the stat results
		 * we used to create the fsal_obj_handle.
		 */
		posix2fsal_attributes(&st, attrs_out);
	}

	if (state != NULL) {
		/* Prepare to take the share reservation, but only if we are
		 * called with a valid state (if state is NULL the caller is
		 * a stateless create such as NFS v3 CREATE).
		 */

		/* This can block over an I/O operation. */
		PTHREAD_RWLOCK_wrlock(&(*new_obj)->obj_lock);

		/* Take the share reservation now by updating the counters. */
		update_share_counters(&handle->share, FSAL_O_CLOSED, openflags);

		PTHREAD_RWLOCK_unlock(&(*new_obj)->obj_lock);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);

 fileerr:

	/* Close the file we just opened. */
	(void) DaosFileSystemClose(handle->node_handle);

	if (created) {
		/* Remove the file we just created */
		(void) DaosFileSystemUnlink(handle->node_handle, (char *)name);
	}

	return status;
}

/**
 * @brief Return open status of a state.
 *
 * This function returns open flags representing the current open
 * status for a state. The state_lock must be held.
 *
 * @param[in] obj_hdl     File on which to operate
 * @param[in] state       File state to interrogate
 *
 * @retval Flags representing current open status
 */

fsal_openflags_t daosfs_fsal_status2(struct fsal_obj_handle *obj_hdl,
				struct state_t *state)
{
	struct daosfs_fsal_handle *handle = container_of(obj_hdl,
					struct daosfs_fsal_handle, handle);
						
	/* normal FSALs recover open state in "state" */

	return handle->openflags;
}

/**
 * @brief Re-open a file that may be already opened
 *
 * This function supports changing the access mode of a share reservation and
 * thus should only be called with a share state. The state_lock must be held.
 *
 * This MAY be used to open a file the first time if there is no need for
 * open by name or create semantics. One example would be 9P lopen.
 *
 * @param[in] obj_hdl     File on which to operate
 * @param[in] state       state_t to use for this operation
 * @param[in] openflags   Mode for re-open
 *
 * @return FSAL status.
 */

fsal_status_t daosfs_fsal_reopen2(struct fsal_obj_handle *obj_hdl,
			struct state_t *state,
			fsal_openflags_t openflags)
{
	fsal_status_t status = {0, 0};
	int posix_flags = 0;
	fsal_openflags_t old_openflags;

	struct daosfs_fsal_handle *handle = container_of(obj_hdl,
			struct daosfs_fsal_handle, handle);

	LogFullDebug(COMPONENT_FSAL,
		"%s enter obj_hdl %p", __func__, obj_hdl);

	/* DAOSFS fsal does not permit concurrent opens, so openflags
	 * are recovered from handle */

	fsal2posix_openflags(openflags, &posix_flags);

	/* This can block over an I/O operation. */
	PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

	old_openflags = handle->openflags;

	/* We can conflict with old share, so go ahead and check now. */
	status = check_share_conflict(&handle->share, openflags, false);

	if (FSAL_IS_ERROR(status)) {
		PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

		return status;
	}

	/* Set up the new share so we can drop the lock and not have a
	 * conflicting share be asserted, updating the share counters.
	 */
	update_share_counters(&handle->share, old_openflags, openflags);

	PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);

	/* perform a provider open iff not already open */
	if (!fsal_is_open(obj_hdl)) {

		/* XXX also, how do we know the ULP tracks opens?
		 * 9P does, V3 does not */

		int rc = DaosFileSystemOpen(handle->node_handle, posix_flags);
		if (rc < 0) {
			/* We had a failure on open - we need to revert the
			 * share. This can block over an I/O operation.
			 */
			PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

			update_share_counters(&handle->share, openflags,
					old_openflags);

			PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
		}

		status = daosfs2fsal_error(rc);
	}

	return status;
}

/**
 * @brief Read data from a file
 *
 * This function reads data from the given file. The FSAL must be able to
 * perform the read whether a state is presented or not. This function also
 * is expected to handle properly bypassing or not share reservations.
 *
 * @param[in]     obj_hdl        File on which to operate
 * @param[in]     bypass         If state doesn't indicate a share reservation,
 *                               bypass any deny read
 * @param[in]     state          state_t to use for this operation
 * @param[in]     offset         Position from which to read
 * @param[in]     buffer_size    Amount of data to read
 * @param[out]    buffer         Buffer to which data are to be copied
 * @param[out]    read_amount    Amount of data read
 * @param[out]    end_of_file    true if the end of file has been reached
 * @param[in,out] info           more information about the data
 *
 * @return FSAL status.
 */

fsal_status_t daosfs_fsal_read2(struct fsal_obj_handle *obj_hdl,
			bool bypass,
			struct state_t *state,
			uint64_t offset,
			size_t buffer_size,
			void *buffer,
			size_t *read_amount,
			bool *end_of_file,
			struct io_info *info)
{
	struct daosfs_fsal_handle *handle = container_of(obj_hdl,
			struct daosfs_fsal_handle, handle);

	LogFullDebug(COMPONENT_FSAL,
		"%s enter obj_hdl %p state %p", __func__, obj_hdl, state);

	if (info != NULL) {
		/* Currently we don't support READ_PLUS */
		return fsalstat(ERR_FSAL_NOTSUPP, 0);
	}

	/* DAOSFS does not support a file descriptor abstraction--so
	 * reads are handle based */

	int rc = DaosFileSystemRead(handle->node_handle, offset,
			buffer_size, read_amount, buffer);
	if (rc < 0)
		return daosfs2fsal_error(rc);

	*end_of_file = (read_amount == 0);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Write data to a file
 *
 * This function writes data to a file. The FSAL must be able to
 * perform the write whether a state is presented or not. This function also
 * is expected to handle properly bypassing or not share reservations. Even
 * with bypass == true, it will enforce a mandatory (NFSv4) deny_write if
 * an appropriate state is not passed).
 *
 * The FSAL is expected to enforce sync if necessary.
 *
 * @param[in]     obj_hdl        File on which to operate
 * @param[in]     bypass         If state doesn't indicate a share reservation,
 *                               bypass any non-mandatory deny write
 * @param[in]     state          state_t to use for this operation
 * @param[in]     offset         Position at which to write
 * @param[in]     buffer         Data to be written
 * @param[in,out] fsal_stable    In, if on, the fsal is requested to write data
 *                               to stable store. Out, the fsal reports what
 *                               it did.
 * @param[in,out] info           more information about the data
 *
 * @return FSAL status.
 */

fsal_status_t daosfs_fsal_write2(struct fsal_obj_handle *obj_hdl,
			bool bypass,
			struct state_t *state,
			uint64_t offset,
			size_t buffer_size,
			void *buffer,
			size_t *wrote_amount,
			bool *fsal_stable,
			struct io_info *info)
{
	struct daosfs_fsal_handle *handle = container_of(obj_hdl,
			struct daosfs_fsal_handle, handle);

	LogFullDebug(COMPONENT_FSAL,
		"%s enter obj_hdl %p state %p", __func__, obj_hdl, state);

	if (info != NULL) {
		/* Currently we don't support WRITE_PLUS */
		return fsalstat(ERR_FSAL_NOTSUPP, 0);
	}

	/* XXX note no call to fsal_find_fd (or wrapper) */

	int rc = DaosFileSystemWrite(handle->node_handle, offset,
			buffer_size, wrote_amount, buffer);

	LogFullDebug(COMPONENT_FSAL,
		"%s post obj_hdl %p state %p returned %d", __func__, obj_hdl,
		state, rc);

	if (rc < 0)
		return daosfs2fsal_error(rc);

	if (*fsal_stable) {
		rc = DaosFileSystemCommit(handle->node_handle, 0, 0);
		if (rc < 0)
			return daosfs2fsal_error(rc);
	}

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Commit written data
 *
 * This function flushes possibly buffered data to a file. This method
 * differs from commit due to the need to interact with share reservations
 * and the fact that the FSAL manages the state of "file descriptors". The
 * FSAL must be able to perform this operation without being passed a specific
 * state.
 *
 * @param[in] obj_hdl          File on which to operate
 * @param[in] state            state_t to use for this operation
 * @param[in] offset           Start of range to commit
 * @param[in] len              Length of range to commit
 *
 * @return FSAL status.
 */

fsal_status_t daosfs_fsal_commit2(struct fsal_obj_handle *obj_hdl,
			off_t offset, size_t length)
{
	int rc;

	struct daosfs_fsal_handle *handle = container_of(obj_hdl,
			struct daosfs_fsal_handle, handle);

	LogFullDebug(COMPONENT_FSAL,
		"%s enter obj_hdl %p offset %"PRIx64" length %zx",
		__func__, obj_hdl, (uint64_t) offset, length);

	rc = DaosFileSystemCommit(handle->node_handle, offset, length);
	if (rc < 0)
		return daosfs2fsal_error(rc);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Manage closing a file when a state is no longer needed.
 *
 * When the upper layers are ready to dispense with a state, this method is
 * called to allow the FSAL to close any file descriptors or release any other
 * resources associated with the state. A call to free_state should be assumed
 * to follow soon.
 *
 * @param[in] obj_hdl    File on which to operate
 * @param[in] state      state_t to use for this operation
 *
 * @return FSAL status.
 */

fsal_status_t daosfs_fsal_close2(struct fsal_obj_handle *obj_hdl,
			struct state_t *state)
{
	int rc;

	struct daosfs_fsal_handle *handle = container_of(obj_hdl,
			struct daosfs_fsal_handle, handle);

	LogFullDebug(COMPONENT_FSAL,
		"%s enter obj_hdl %p state %p", __func__, obj_hdl, state);

	if (state) {
		if (state->state_type == STATE_TYPE_SHARE ||
			state->state_type == STATE_TYPE_NLM_SHARE ||
			state->state_type == STATE_TYPE_9P_FID) {
			/* This is a share state, we must update the share
			 * counters.  This can block over an I/O operation.
			 */
			PTHREAD_RWLOCK_wrlock(&obj_hdl->obj_lock);

			update_share_counters(&handle->share,
					handle->openflags,
					FSAL_O_CLOSED);

			PTHREAD_RWLOCK_unlock(&obj_hdl->obj_lock);
		}
	}

	rc = DaosFileSystemClose(handle->node_handle);
	if (rc < 0)
		return daosfs2fsal_error(rc);

	handle->openflags = FSAL_O_CLOSED;

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Close the global FD for a file
 *
 * This function closes a file, freeing resources used for read/write
 * access and releasing capabilities.
  *
 * @param[in] handle_pub File to close
  *
  * @return FSAL status.
  */
static fsal_status_t daosfs_fsal_close(struct fsal_obj_handle *handle_pub)
{
	return daosfs_fsal_close2(handle_pub, NULL);
}


/**
 * @brief Write wire handle
 *
 * This function writes a 'wire' handle to be sent to clients and
 * received from the.
 *
 * @param[in]     obj_hdl  Handle to digest
 * @param[in]     output_type Type of digest requested
 * @param[in,out] fh_desc     Location/size of buffer for
 *                            digest/Length modified to digest length
 *
 * @return FSAL status.
 */

static fsal_status_t handle_digest(const struct fsal_obj_handle *obj_hdl,
				   uint32_t output_type,
				   struct gsh_buffdesc *fh_desc)
{
	size_t key_size = sizeof(struct daosfs_node_key);
	/* The private 'full' object handle */
	const struct daosfs_fsal_handle *handle =
	    container_of(obj_hdl, const struct daosfs_fsal_handle, handle);

	switch (output_type) {
		/* Digested Handles */
	case FSAL_DIGEST_NFSV3:
	case FSAL_DIGEST_NFSV4:
		if (fh_desc->len < key_size) {
			LogMajor(COMPONENT_FSAL,
				 "DAOSFS handle_digest: space too small for handle.  Need %zu, have %zu",
				 key_size, fh_desc->len);
			return fsalstat(ERR_FSAL_TOOSMALL, 0);
		} else {
			memcpy(fh_desc->addr, &(handle->node_handle->key),
				key_size);
			fh_desc->len = key_size;
		}
		break;

	default:
		return fsalstat(ERR_FSAL_SERVERFAULT, 0);
	}

	LogDebug(COMPONENT_FSAL, "wrote key len %zd", fh_desc->len);
	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/**
 * @brief Give a hash key for file handle
 *
 * This function locates a unique hash key for a given file.
 *
 * @param[in]  obj_hdl The file whose key is to be found
 * @param[out] fh_desc    Address and length of key
 */

static void handle_to_key(struct fsal_obj_handle *obj_hdl,
			  struct gsh_buffdesc *fh_desc)
{
	struct daosfs_fsal_handle *handle =
		container_of(obj_hdl, struct daosfs_fsal_handle, handle);

	fh_desc->addr = &(handle->node_handle->key);
	fh_desc->len = sizeof(struct daosfs_node_key);
	LogDebug(COMPONENT_FSAL, "set key len %zd", fh_desc->len);
}

/**
 * @brief Override functions in ops vector
 *
 * This function overrides implemented functions in the ops vector
 * with versions for this FSAL.
 *
 * @param[in] ops Handle operations vector
 */

void handle_ops_init(struct fsal_obj_ops *ops)
{
	ops->release = release;
	ops->merge = daosfs_merge;
	ops->lookup = lookup;
	ops->create = daosfs_fsal_create;
	ops->mkdir = daosfs_fsal_mkdir;
	ops->readdir = daosfs_fsal_readdir;
	ops->getattrs = getattrs;
	ops->rename = daosfs_fsal_rename;
	ops->unlink = daosfs_fsal_unlink;
	ops->close = daosfs_fsal_close;
	ops->handle_digest = handle_digest;
	ops->handle_to_key = handle_to_key;
	ops->open2 = daosfs_fsal_open2;
	ops->status2 = daosfs_fsal_status2;
	ops->reopen2 = daosfs_fsal_reopen2;
	ops->read2 = daosfs_fsal_read2;
	ops->write2 = daosfs_fsal_write2;
	ops->commit2 = daosfs_fsal_commit2;
	ops->setattr2 = daosfs_fsal_setattr2;
	ops->close2 = daosfs_fsal_close2;
}
