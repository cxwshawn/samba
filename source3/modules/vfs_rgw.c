#include "includes.h"
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
	char *rgw_uid;
	char *rgw_access_key;
	char *rgw_secret_key;
	struct rgw_fs *rgw_fs;
	struct rgw_mount_fs *next, *prev;
} *rgw_mount_fs;

static struct rgw_fs_module {
	char *conf_path;
	char *name;
	char *cluster;
	char *init_args;
	librgw_t rgw;
} rgw_fs_module;

/* Disk Operations */

static int vfs_rgw_connect(struct vfs_handle_struct *handle,
			       const char *service,
			       const char *user)
{

}

static void vfs_rgw_disconnect(struct vfs_handle_struct *handle)
{

}
static int vfs_rgw_statvfs(struct vfs_handle_struct *handle,
			       const char *path,
			       struct vfs_statvfs_struct *vfs_statvfs)
{

}

static int vfs_rgw_open(struct vfs_handle_struct *handle,
			struct smb_filename *smb_fname,
			files_struct *fsp, int flags, mode_t mode)
{}

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
