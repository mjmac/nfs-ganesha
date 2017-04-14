/*
 * vim:noexpandtab:shiftwidth=8:tabstop=8:
 *
 */

/* main.c
 * Module core functions
 */

#include <stdlib.h>
#include <assert.h>
#include "gsh_list.h"
#include "fsal.h"
#include "fsal_types.h"
#include "FSAL/fsal_init.h"
#include "FSAL/fsal_commonlib.h"
#include "fsal_api.h"
#include "internal.h"
#include "abstract_mem.h"
#include "nfs_exports.h"
#include "export_mgr.h"

static const char *module_name = "DAOSFS";

/* filesystem info for DAOSFS */
static struct fsal_staticfsinfo_t default_daosfs_info = {
	.maxfilesize = UINT64_MAX,
	.maxlink = _POSIX_LINK_MAX,
	.maxnamelen = 1024,
	.maxpathlen = 1024,
	.no_trunc = true,
	.chown_restricted = false,
	.case_insensitive = false,
	.case_preserving = true,
	.link_support = false,
	.symlink_support = false,
	.lock_support = false,
	.lock_support_owner = false,
	.lock_support_async_block = false,
	.named_attr = true, /* XXX */
	.unique_handles = true,
	.lease_time = {10, 0},
	.acl_support = false,
	.cansettime = true,
	.homogenous = true,
	.supported_attrs = daosfs_supported_attributes,
	.maxread = FSAL_MAXIOSIZE,
	.maxwrite = FSAL_MAXIOSIZE,
	.umask = 0,
	.rename_changes_key = true,
};

static struct config_item daosfs_items[] = {
	CONF_ITEM_STR("init_args", 1, MAXPATHLEN, NULL,
		daosfs_fsal_module, init_args),
	CONF_ITEM_MODE("umask", 0,
			daosfs_fsal_module, fs_info.umask),
	CONF_ITEM_MODE("xattr_access_rights", 0,
			daosfs_fsal_module, fs_info.xattr_access_rights),
	CONFIG_EOL
};

struct config_block daosfs_block = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.daosfs",
	.blk_desc.name = "DAOSFS",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = daosfs_items,
	.blk_desc.u.blk.commit = noop_conf_commit
};

//static pthread_mutex_t init_mtx = PTHREAD_MUTEX_INITIALIZER;

/* Module methods
 */

/* init_config
 * must be called with a reference taken (via lookup_fsal)
 */

static fsal_status_t init_config(struct fsal_module *module_in,
				config_file_t config_struct,
				struct config_error_type *err_type)
{
	struct daosfs_fsal_module *myself =
	    container_of(module_in, struct daosfs_fsal_module, fsal);

	LogDebug(COMPONENT_FSAL,
		 "DAOSFS module setup.");

	myself->fs_info = default_daosfs_info;
	(void) load_config_from_parse(config_struct,
				      &daosfs_block,
				      myself,
				      true,
				      err_type);
	if (!config_error_is_harmless(err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

 /**
 * @brief Indicate support for extended operations.
 *
 * @retval true if extended operations are supported.
 */

bool support_ex(struct fsal_obj_handle *obj)
{
	return true;
}

/**
 * @brief Create a new export under this FSAL
 *
 * This function creates a new export object for the DAOSFS FSAL.
 *
 * @param[in]     module_in  The supplied module handle
 * @param[in]     path       The path to export
 * @param[in]     options    Export specific options for the FSAL
 * @param[in,out] list_entry Our entry in the export list
 * @param[in]     next_fsal  Next stacked FSAL
 * @param[out]    pub_export Newly created FSAL export object
 *
 * @return FSAL status.
 */

static struct config_item export_params[] = {
	CONF_ITEM_STR("daos_server_group", 0, 63, NULL,
		      daosfs_fsal_export, daos_server_group),
	CONF_MAND_STR("daos_pool_uuid", 0, MAXUUIDLEN, NULL,
		      daosfs_fsal_export, daos_pool_uuid),
	CONF_MAND_STR("daos_fs_container", 0, MAXUUIDLEN, NULL,
		      daosfs_fsal_export, daos_fs_container),
	CONFIG_EOL
};

static struct config_block export_param_block = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.daosfs-export%d",
	.blk_desc.name = "FSAL",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = export_params,
	.blk_desc.u.blk.commit = noop_conf_commit
};

static fsal_status_t create_export(struct fsal_module *module_in,
				   void *parse_node,
				   struct config_error_type *err_type,
				   const struct fsal_up_vector *up_ops)
{
	/* The status code to return */
	fsal_status_t status = { ERR_FSAL_NO_ERROR, 0 };
	/* The internal export object */
	struct daosfs_fsal_export *export = NULL;
	/* The 'private' root handle */
	struct daosfs_fsal_handle *handle = NULL;
	/* Stat for root */
	struct stat st;
	/* Return code */
	int rc = 0;
	/* Return code from DAOSFS calls */
	int daosfs_status;
	/* True if we have called fsal_export_init */
	bool initialized = false;

	/* once */
	if (!DAOSFSFSM.daosfs) {
		EnableDaosFileSystemDebug();

		rc = LibDaosFileSystemInit(&DAOSFSFSM.daosfs);
		if (rc != 0) {
			LogCrit(COMPONENT_FSAL,
				"DAOSFS module: LibDaosFileSystemInit() failed (%d)",
				rc);
		}
	}

	if (rc != 0) {
		status.major = ERR_FSAL_BAD_INIT;
		goto error;
	}

	export = gsh_calloc(1, sizeof(struct daosfs_fsal_export));
	if (export == NULL) {
		status.major = ERR_FSAL_NOMEM;
		LogCrit(COMPONENT_FSAL,
			"Unable to allocate export object for %s.",
			op_ctx->ctx_export->fullpath);
		goto error;
	}

	fsal_export_init(&export->export);
	export_ops_init(&export->export.exp_ops);

	/* get params for this export, if any */
	if (parse_node) {
		rc = load_config_from_node(parse_node,
					   &export_param_block,
					   export,
					   true,
					   err_type);

		if (rc != 0) {
			gsh_free(export);
			return fsalstat(ERR_FSAL_INVAL, 0);
		}
	}

	initialized = true;

	daosfs_status = OpenDaosFileSystem(export->daos_server_group,
					   export->daos_pool_uuid,
					   export->daos_fs_container,
					   &export->fs);
	if (daosfs_status != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to mount DAOSFS cluster for %s.",
			op_ctx->ctx_export->fullpath);
		goto error;
	}

	if (fsal_attach_export(module_in, &export->export.exports) != 0) {
		status.major = ERR_FSAL_SERVERFAULT;
		LogCrit(COMPONENT_FSAL,
			"Unable to attach export for %s.",
			op_ctx->ctx_export->fullpath);
		goto error;
	}

	/*
	if (daosfs_register_invalidate(export->daosfs_fs, daosfs_fs_invalidate,
					up_ops->up_export,
					DAOSFS_REG_INVALIDATE_FLAG_NONE) != 0) {
		LogCrit(COMPONENT_FSAL,
			"Unable to register invalidates for %s.",
			op_ctx->ctx_export->fullpath);
		goto error;
	}
	*/

	export->export.fsal = module_in;

	LogDebug(COMPONENT_FSAL,
		 "DAOSFS module export %s.",
		 op_ctx->ctx_export->fullpath);

	rc = construct_handle(export, export->fs->root_ptr, &st, &handle);
	if (rc < 0) {
		status = daosfs2fsal_error(rc);
		goto error;
	}

	rc = DaosFileSystemGetAttr(handle->node_handle, &st);
	if (rc < 0)
		return daosfs2fsal_error(rc);

	op_ctx->fsal_export = &export->export;

	export->root = handle;
	export->export.up_ops = up_ops;

	return status;

 error:
	if (export) {
		gsh_free(export);
	}

	if (initialized)
		initialized = false;

	return status;
}

/**
 * @brief Initialize and register the FSAL
 *
 * This function initializes the FSAL module handle, being called
 * before any configuration or even mounting of a DAOSFS cluster.  It
 * exists solely to produce a properly constructed FSAL module
 * handle.
 */

MODULE_INIT void init(void)
{
	struct fsal_module *myself = &DAOSFSFSM.fsal;

	LogDebug(COMPONENT_FSAL,
		 "DAOSFS module registering.");

	/* register_fsal seems to expect zeroed memory. */
	memset(myself, 0, sizeof(*myself));

	if (register_fsal(myself, module_name, FSAL_MAJOR_VERSION,
			  FSAL_MINOR_VERSION, FSAL_ID_NO_PNFS) != 0) {
		/* The register_fsal function prints its own log
		   message if it fails */
		LogCrit(COMPONENT_FSAL,
			"DAOSFS module failed to register.");
	}

	/* Set up module operations */
	myself->m_ops.create_export = create_export;
	myself->m_ops.init_config = init_config;
	myself->m_ops.support_ex = support_ex;
}

/**
 * @brief Release FSAL resources
 *
 * This function unregisters the FSAL and frees its module handle.  The
 * FSAL also has an open instance of the daosfs library, so we also need to
 * release that.
 */

MODULE_FINI void finish(void)
{
	int ret;

	LogDebug(COMPONENT_FSAL,
		 "DAOSFS module finishing.");

	ret = unregister_fsal(&DAOSFSFSM.fsal);
	if (ret != 0) {
		LogCrit(COMPONENT_FSAL,
			"DAOSFS: unregister_fsal failed (%d)", ret);
	}

	/* release the library */
	if (DAOSFSFSM.daosfs) {
		LibDaosFileSystemFini(DAOSFSFSM.daosfs);
	}
}
