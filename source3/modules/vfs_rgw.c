
#include "smbd/smbd.h"

#include <include/rados/librgw.h>
#include <include/rados/rgw_file.h>

#undef DBGC_CLASS
#define DBGC_CLASS DBGC_VFS

#ifndef LIBRGW_VERSION
#define LIBRGW_VERSION(maj, min, extra) ((maj << 16) + (min << 8) + extra)
#define LIBRGW_VERSION_CODE LIBRGW_VERSION(0, 0, 0)
#endif

/*
 * there may mount many rgw fs 
 */
static struct rgw_mount_fs {
	char *connectpath;
	char *rgw_uid;
	char *rgw_access_key;
	char *rgw_secret_key;
	struct rgw_fs *rgw_fs;
	int ref;
	struct rgw_mount_fs *next, *prev;
} *rgw_mount_fs;

static struct rgw_fs_module {
	char *conf_path;
	char *name;
	char *cluster;
	char *init_args;
	librgw_t rgw;
} *rgw_fs_module;

static int rgw_set_preopened(const char *connectpath, const char *rgw_uid,
	const char *rgw_access_key,
	const char *rgw_secret_key, struct rgw_fs *fs)
{
	struct rgw_mount_fs *entry = NULL;

	entry = talloc_zero(NULL, struct rgw_mount_fs);
	if (!entry) {
		errno = ENOMEM;
		return -1;
	}

	entry->connectpath = talloc_strdup(entry, connectpath);
	if (entry->connectpath == NULL) {
		talloc_free(entry);
		errno = ENOMEM;
		return -1;
	}

	entry->fs = fs;
	entry->ref = 1;

	DLIST_ADD(rgw_mount_fs, entry);

	return 0;
}

static struct rgw_fs *rgw_find_preopened(const char *connectpath)
{
	struct rgw_mount_fs *entry = NULL;

	for (entry = rgw_mount_fs; entry; entry = entry->next) {
		if (strcmp(entry->connectpath, connectpath) == 0)
		{
			entry->ref++;
			return entry->fs;
		}
	}

	return NULL;
}

static bool rgw_last_preopened() {
	int entry_count = 0;
	struct rgw_mount_fs *entry = NULL;

	for (entry = rgw_mount_fs; entry; entry = entry->next) {
		entry_count++;
		if (entry_count > 1) {
			return false;
		}
	}
	if (entry->ref > 1) {
		return false;
	}
	return true;
}

static void rgw_clear_preopened(struct rgw_fs *fs)
{
	struct rgw_mount_fs *entry = NULL;

	for (entry = rgw_mount_fs; entry; entry = entry->next) {
		if (entry->fs == fs) {
			if (--entry->ref)
				return;

			DLIST_REMOVE(rgw_mount_fs, entry);

			rgw_umount(entry->rgw_fs, RGW_UMOUNT_FLAG_NONE);
			talloc_free(entry);
		}
	}
}

/* Disk Operations */
static int vfs_rgw_module_load_params(struct vfs_handle_struct *handle)
{
	if (!rgw_fs_module) {
		return -1;
	}
	rgw_fs_module->conf_path = lp_parm_const_string(SNUM(handle->conn), "rgw", "config_file", NULL);
	rgw_fs_module->name = lp_parm_const_string(SNUM(handle->conn), "rgw", "name", NULL);
	rgw_fs_module->cluster = lp_parm_const_string(SNUM(handle->conn, "rgw", "cluster", NULL));
	rgw_fs_module->init_args = lp_parm_const_string(SNUM(handle->conn, "rgw", "init_args", NULL));
	return 0;
}

static int vfs_rgw_preinit()
{
	int rc = 0;
	char *conf_path = NULL;
	char *inst_name = NULL;
	char *cluster = NULL;

	int argc = 1;
	char *argv[5] = { "vfs_rgw", NULL, NULL, NULL, NULL };
	int clen;

	if (!rgw_fs_module) {
		rgw_fs_module = talloc_zero(NULL, struct rgw_fs_module);
		if (!rgw_fs_module) {
			return -1;
		}
		vfs_rgw_load_params(handle);

		if (rgw_fs_module->conf_path) {
			clen = strlen(rgw_fs_module.conf_path) + 8;
			conf_path = (char *) talloc_zero(talloc_tos(), clen);
			sprintf(conf_path, "--conf=%s",
				rgw_fs_module->conf_path);
			argv[argc] = conf_path;
			++argc;
		}

		if (rgw_fs_module->name) {
			clen = strlen(rgw_fs_module->name) + 8;
			inst_name = (char *) talloc_zero(talloc_tos(), clen);
			sprintf(inst_name, "--name=%s", rgw_fs_module->name);
			argv[argc] = inst_name;
			++argc;
		}

		if (rgw_fs_module->cluster) {
			clen = strlen(rgw_fs_module->cluster) + 8;
			cluster = (char *) talloc_zero(talloc_tos(), clen);
			sprintf(cluster, "--cluster=%s",
				rgw_fs_module->cluster);
			argv[argc] = cluster;
			++argc;
		}

		if (rgw_fs_module->init_args) {
			argv[argc] = rgw_fs_module->init_args;
			++argc;
		}

		rc = librgw_create(&rgw_fs_module->rgw, argc, argv);
		if (rc != 0) {
			DEBUG(0, ("RGW module: librgw init failed (%d)\n", rc));
		}
	}
	return rc;
}

static int vfs_rgw_connect(struct vfs_handle_struct *handle,
			       const char *service,
			       const char *user)
{
	/* Return code */
	int rc = 0;

	rc = vfs_rgw_preinit();
	if (rc) {
		DEBUG(0, ("vfs_rgw_preinit failed (%d)", rc));
	}
	struct rgw_fs * fs = rgw_find_preopened(handle->conn->connectpath);
	if (fs) {
		goto done;
	}
	char *rgw_user_id = lp_parm_const_string(SNUM(handle->conn), "rgw", "uid", NULL);
	char *rgw_access_key = lp_parm_const_string(SNUM(handle->conn), "rgw", "access_key", NULL);
	char *rgw_secret_key = lp_parm_const_string(SNUM(handle->conn), "rgw", "secret_key", NULL);

	rc = rgw_mount(rgw_fs_module->rgw,
			rgw_user_id,
			rgw_access_key,
			rgw_secret_key,
			&fs,
			RGW_MOUNT_FLAG_NONE);
	if (rc) {
		DEBUG(0, ("Unable to mount RGW cluster for %s.", handle->conn->connectpath));
		goto done;
	}
	rc = rgw_set_preopened(handle->conn->connectpath, rgw_user_id, rgw_access_key,
				rgw_secret_key, fs);
	if (rc) {
		DEBUG(0, ("Failed to register path %s", handle->conn->connectpath));
		goto done;
	}
done:
	if (ret < 0) {
		if (fs)
			rgw_umount(fs, RGW_UMOUNT_FLAG_NONE);
	} else {
		handle->data = fs;
	}
	return rc;
}

static void vfs_rgw_disconnect(struct vfs_handle_struct *handle)
{
	//TODO: judge whether there has been not existing fs
	bool last_one = false;
	last_one = rgw_last_preopened();
	struct rgw_fs * fs = handle->data;
	rgw_clear_preopened(fs);
	if (last_one) {
		/* release the library */
		if (rgw_fs_module.rgw) {
			librgw_shutdown(rgw_fs_module.rgw);
		}
		talloc_free(rgw_fs_module);	
	}
}

static int vfs_rgw_statvfs(struct vfs_handle_struct *handle,
			       const char *path,
			       struct vfs_statvfs_struct *vfs_statvfs)
{

}

static int vfs_rgw_open(struct vfs_handle_struct *handle,
			struct smb_filename *smb_fname,
			files_struct *fsp, int flags, mode_t mode)
{
	//first seek to the real parent rgw_file_handle
	//such as a/b/c.txt
	//first find a's file handle under root,
	//then find b's file handle under a's,
	//then open c.txt file under b's handle.

	if (flags & O_DIRECTORY) {
		// glfd = glfs_opendir(handle->data, smb_fname->base_name);
	} else if (flags & O_CREAT) {
		glfd = glfs_creat(handle->data, smb_fname->base_name, flags,
				  mode);
	} else {

		glfd = glfs_open(handle->data, smb_fname->base_name, flags);
	}

	if (glfd == NULL) {
		return -1;
	}
	p_tmp = (glfs_fd_t **)VFS_ADD_FSP_EXTENSION(handle, fsp,
							  glfs_fd_t *, NULL);
	*p_tmp = glfd;
	/* An arbitrary value for error reporting, so you know its us. */
	return 13371337;
}

static int vfs_rgw_close(struct vfs_handle_struct *handle,
			     files_struct *fsp)
{
}

static int vfs_rgw_mkdir(struct vfs_handle_struct *handle,
			     const struct smb_filename *smb_fname,
			     mode_t mode)
{}

static struct dirent *vfs_rgw_readdir(struct vfs_handle_struct *handle,
					  DIR *dirp, SMB_STRUCT_STAT *sbuf)
{}

static int vfs_rgw_rename(struct vfs_handle_struct *handle,
			      const struct smb_filename *smb_fname_src,
			      const struct smb_filename *smb_fname_dst)
{}

static int vfs_rgw_unlink(struct vfs_handle_struct *handle,
			      const struct smb_filename *smb_fname)
{}

static ssize_t vfs_rgw_read(struct vfs_handle_struct *handle,
				files_struct *fsp, void *data, size_t n)
{}

static ssize_t vfs_rgw_write(struct vfs_handle_struct *handle,
				 files_struct *fsp, const void *data, size_t n)
{}

static ssize_t vfs_rgw_getxattr(struct vfs_handle_struct *handle,
				    const char *path, const char *name,
				    void *value, size_t size)
{}

static int vfs_rgw_setxattr(struct vfs_handle_struct *handle,
				const char *path, const char *name,
				const void *value, size_t size, int flags)
{}

static struct vfs_fn_pointers rgw_fns = {

	/* Disk Operations */

	.connect_fn = vfs_rgw_connect,
	.disconnect_fn = vfs_rgw_disconnect,
	// .disk_free_fn = vfs_rgw_disk_free,
	// .get_quota_fn = vfs_rgw_get_quota,
	// .set_quota_fn = vfs_rgw_set_quota,
	.statvfs_fn = vfs_rgw_statvfs,
	// .fs_capabilities_fn = vfs_rgw_fs_capabilities,

	// .get_dfs_referrals_fn = NULL,

	/* Directory Operations */

	// .opendir_fn = vfs_rgw_opendir,
	// .fdopendir_fn = vfs_rgw_fdopendir,
	.readdir_fn = vfs_rgw_readdir,
	// .seekdir_fn = vfs_rgw_seekdir,
	// .telldir_fn = vfs_rgw_telldir,
	// .rewind_dir_fn = vfs_rgw_rewinddir,
	.mkdir_fn = vfs_rgw_mkdir,
	// .rmdir_fn = vfs_rgw_rmdir,
	// .closedir_fn = vfs_rgw_closedir,
	// .init_search_op_fn = vfs_rgw_init_search_op,

	/* File Operations */

	.open_fn = vfs_rgw_open,
	.create_file_fn = NULL,
	.close_fn = vfs_rgw_close,
	.read_fn = vfs_rgw_read,
	// .pread_fn = vfs_rgw_pread,
	// .pread_send_fn = vfs_rgw_pread_send,
	// .pread_recv_fn = vfs_rgw_recv,
	.write_fn = vfs_rgw_write,
	// .pwrite_fn = vfs_rgw_pwrite,
	// .pwrite_send_fn = vfs_rgw_pwrite_send,
	// .pwrite_recv_fn = vfs_rgw_recv,
	// .lseek_fn = vfs_rgw_lseek,
	// .sendfile_fn = vfs_rgw_sendfile,
	// .recvfile_fn = vfs_rgw_recvfile,
	.rename_fn = vfs_rgw_rename,
	// .fsync_fn = vfs_rgw_fsync,
	// .fsync_send_fn = vfs_rgw_fsync_send,
	// .fsync_recv_fn = vfs_rgw_fsync_recv,

	// .stat_fn = vfs_rgw_stat,
	// .fstat_fn = vfs_rgw_fstat,
	// .lstat_fn = vfs_rgw_lstat,
	// .get_alloc_size_fn = vfs_rgw_get_alloc_size,
	.unlink_fn = vfs_rgw_unlink,

	// .chmod_fn = vfs_rgw_chmod,
	// .fchmod_fn = vfs_rgw_fchmod,
	// .chown_fn = vfs_rgw_chown,
	// .fchown_fn = vfs_rgw_fchown,
	// .lchown_fn = vfs_rgw_lchown,
	// .chdir_fn = vfs_rgw_chdir,
	// .getwd_fn = vfs_rgw_getwd,
	// .ntimes_fn = vfs_rgw_ntimes,
	// .ftruncate_fn = vfs_rgw_ftruncate,
	// .fallocate_fn = vfs_rgw_fallocate,
	// .lock_fn = vfs_rgw_lock,
	// .kernel_flock_fn = vfs_rgw_kernel_flock,
	// .linux_setlease_fn = vfs_rgw_linux_setlease,
	// .getlock_fn = vfs_rgw_getlock,
	// .symlink_fn = vfs_rgw_symlink,
	// .readlink_fn = vfs_rgw_readlink,
	// .link_fn = vfs_rgw_link,
	// .mknod_fn = vfs_rgw_mknod,
	// .realpath_fn = vfs_rgw_realpath,
	// .chflags_fn = vfs_rgw_chflags,
	// .file_id_create_fn = NULL,
	// .copy_chunk_send_fn = NULL,
	// .copy_chunk_recv_fn = NULL,
	// .streaminfo_fn = NULL,
	// .get_real_filename_fn = vfs_rgw_get_real_filename,
	// .connectpath_fn = vfs_rgw_connectpath,

	// .brl_lock_windows_fn = NULL,
	// .brl_unlock_windows_fn = NULL,
	// .brl_cancel_windows_fn = NULL,
	// .strict_lock_fn = NULL,
	// .strict_unlock_fn = NULL,
	// .translate_name_fn = NULL,
	// .fsctl_fn = NULL,

	// /* NT ACL Operations */
	// .fget_nt_acl_fn = NULL,
	// .get_nt_acl_fn = NULL,
	// .fset_nt_acl_fn = NULL,
	// .audit_file_fn = NULL,

	// /* Posix ACL Operations */
	// .chmod_acl_fn = NULL,	/* passthrough to default */
	// .fchmod_acl_fn = NULL,	/* passthrough to default */
	// .sys_acl_get_file_fn = posixacl_xattr_acl_get_file,
	// .sys_acl_get_fd_fn = posixacl_xattr_acl_get_fd,
	// .sys_acl_blob_get_file_fn = posix_sys_acl_blob_get_file,
	// .sys_acl_blob_get_fd_fn = posix_sys_acl_blob_get_fd,
	// .sys_acl_set_file_fn = posixacl_xattr_acl_set_file,
	// .sys_acl_set_fd_fn = posixacl_xattr_acl_set_fd,
	// .sys_acl_delete_def_file_fn = posixacl_xattr_acl_delete_def_file,

	/* EA Operations */
	.getxattr_fn = vfs_rgw_getxattr,
	// .fgetxattr_fn = vfs_rgw_fgetxattr,
	// .listxattr_fn = vfs_rgw_listxattr,
	// .flistxattr_fn = vfs_rgw_flistxattr,
	// .removexattr_fn = vfs_rgw_removexattr,
	// .fremovexattr_fn = vfs_rgw_fremovexattr,
	.setxattr_fn = vfs_rgw_setxattr,
	// .fsetxattr_fn = vfs_rgw_fsetxattr,

	/* AIO Operations */
	// .aio_force_fn = vfs_rgw_aio_force,

	/* Durable handle Operations */
	// .durable_cookie_fn = NULL,
	// .durable_disconnect_fn = NULL,
	// .durable_reconnect_fn = NULL,
};


NTSTATUS vfs_ceph_init(void);
NTSTATUS vfs_ceph_init(void)
{
	return smb_register_vfs(SMB_VFS_INTERFACE_VERSION,
				"rgw", &rgw_fns);
}
