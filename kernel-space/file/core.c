/* SPDX-License-Identifier: GPL-2.0-only */
#include "file.h"
#include "handshake.h"
#include "watchdog.h"
#include <linux/binfmts.h>
#include <linux/dcache.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/gfp.h>
#include <linux/list.h>
#include <linux/namei.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>

static struct rb_root file_perm_tree = RB_ROOT;
static DEFINE_RWLOCK(file_perm_tree_lock);

static inline bool file_perm_node_cmp(const struct file_perm_node *a,
				      const struct file_perm_node *b)
{
	return a->fsid == b->fsid ? a->ino < b->ino : a->fsid < b->fsid;
}

static int file_perm_tree_insert_or_update(fsid_t fsid, ino_t ino,
					   file_perm_t perm)
{
	struct rb_node **new, *parent;
	struct file_perm_node *data;

	data = kzalloc(sizeof(struct file_perm_node), GFP_KERNEL);
	if (!data)
		return -ENOMEM;
	data->fsid = fsid;
	data->ino = ino;
	data->perm = perm;

	write_lock(&file_perm_tree_lock);
	new = &file_perm_tree.rb_node;
	parent = NULL;
	while (*new) {
		struct file_perm_node *this;

		this = container_of(*new, struct file_perm_node, node);
		parent = *new;
		if (file_perm_node_cmp(data, this))
			new = &((*new)->rb_left);
		else if (file_perm_node_cmp(this, data))
			new = &((*new)->rb_right);
		else {
			this->perm = perm;
			kfree(data);
			goto updateout;
		}
	}
	rb_link_node(&data->node, parent, new);
	rb_insert_color(&data->node, &file_perm_tree);

updateout:
	write_unlock(&file_perm_tree_lock);
	return 0;
}

static file_perm_t file_perm_tree_search(fsid_t fsid, ino_t ino)
{
	struct rb_node *node = file_perm_tree.rb_node;
	const struct file_perm_node tmp = { .fsid = fsid, .ino = ino };
	file_perm_t perm = INVAILD_PERM;

	read_lock(&file_perm_tree_lock);
	while (node) {
		struct file_perm_node *this;
		this = container_of(node, struct file_perm_node, node);

		if (file_perm_node_cmp(&tmp, this)) {
			node = node->rb_left;
		} else if (file_perm_node_cmp(this, &tmp)) {
			node = node->rb_right;
		} else {
			perm = this->perm;
			break;
		}
	}
	read_unlock(&file_perm_tree_lock);
	return perm;
}

static void file_perm_tree_delete(fsid_t fsid, ino_t ino)
{
	struct rb_node *node = file_perm_tree.rb_node;
	const struct file_perm_node tmp = { .fsid = fsid, .ino = ino };

	read_lock(&file_perm_tree_lock);
	while (node) {
		struct file_perm_node *this;
		this = container_of(node, struct file_perm_node, node);

		if (file_perm_node_cmp(&tmp, this)) {
			node = node->rb_left;
		} else if (file_perm_node_cmp(this, &tmp)) {
			node = node->rb_right;
		} else {
			rb_erase(&this->node, &file_perm_tree);
			kfree(this);
			break;
		}
	}
	read_unlock(&file_perm_tree_lock);
}

static int file_perm_tree_clear(void)
{
	struct file_perm_node *data;
	write_lock(&file_perm_tree_lock);
	while (!RB_EMPTY_ROOT(&file_perm_tree)) {
		data = container_of(rb_first(&file_perm_tree),
				    struct file_perm_node, node);
		rb_erase(&data->node, &file_perm_tree);
		kfree(data);
	}
	write_unlock(&file_perm_tree_lock);
	return 0;
}

static file_perm_t file_perm_get(const fsid_t fsid, const ino_t ino)
{
	return file_perm_tree_search(fsid, ino);
}

static int file_perm_set(const fsid_t fsid, ino_t ino, file_perm_t perm)
{
	if (fsid == BAD_FSID || ino == BAD_INO)
		return -EINVAL;
	return file_perm_tree_insert_or_update(fsid, ino, perm);
}

int file_perm_set_path(const char *path, file_perm_t perm)
{
	unsigned long fsid, ino;
	file_id_get(path, &fsid, &ino);
	return file_perm_set(fsid, ino, perm);
}

static int file_perm_data_fill(char *path, struct file_perm_data *data)
{
	data->path = adjust_path(path);
	file_id_get(path, &data->fsid, &data->ino);
	data->this_perm = file_perm_get(data->fsid, data->ino);
	data->marked_perm = INVAILD_PERM;
	return 0;
}

static int read_protect_check(struct file_perm_data *data)
{
	const int is_forbidden = data->this_perm & READ_PROTECT_FLAG;
	const int is_audited = data->this_perm & READ_AUDIT_FLAG;
	if (is_forbidden) {
		data->marked_perm = READ_PROTECT_FLAG;
		file_protect_report_to_userspace(data);
	}
	if (is_audited) {
		data->marked_perm = READ_AUDIT_FLAG;
		file_protect_report_to_userspace(data);
	}
	return is_forbidden;
}

static int write_protect_check(struct file_perm_data *data)
{
	const int is_forbidden = data->this_perm & WRITE_PROTECT_FLAG;
	const int is_audited = data->this_perm & WRITE_AUDIT_FLAG;
	if (is_forbidden) {
		data->marked_perm = WRITE_PROTECT_FLAG;
		file_protect_report_to_userspace(data);
	}
	if (is_audited) {
		data->marked_perm = WRITE_AUDIT_FLAG;
		file_protect_report_to_userspace(data);
	}
	return is_forbidden;
}

static int read_write_protect_check(struct file_perm_data *data)
{
	const int is_forbidden = data->this_perm & RDWR_PROTECT_FLAG;
	const int is_audited = data->this_perm & RDWR_AUDIT_FLAG;
	if (is_forbidden) {
		data->marked_perm = RDWR_PROTECT_FLAG;
		file_protect_report_to_userspace(data);
	}
	if (is_audited) {
		data->marked_perm = RDWR_AUDIT_FLAG;
		file_protect_report_to_userspace(data);
	}
	return is_forbidden;
}

static int unlink_protect_check(struct file_perm_data *data)
{
	const int is_forbidden = data->this_perm & UNLINK_PROTECT_FLAG;
	const int is_audited = data->this_perm & UNLINK_AUDIT_FLAG;
	if (is_forbidden) {
		data->marked_perm = UNLINK_PROTECT_FLAG;
		file_protect_report_to_userspace(data);
	}
	if (is_audited) {
		data->marked_perm = UNLINK_AUDIT_FLAG;
		file_protect_report_to_userspace(data);
	}
	return is_forbidden;
}

static int rename_protect_check(struct file_perm_data *data)
{
	const int is_forbidden = data->this_perm & RENAME_PROTECT_FLAG;
	const int is_audited = data->this_perm & RENAME_AUDIT_FLAG;
	if (is_forbidden) {
		data->marked_perm = RENAME_PROTECT_FLAG;
		file_protect_report_to_userspace(data);
	}
	if (is_audited) {
		data->marked_perm = RENAME_AUDIT_FLAG;
		file_protect_report_to_userspace(data);
	}
	return is_forbidden;
}

static int parent_write_protect_check(struct file_perm_data *data)
{
	int is_forbidden;
	struct file_perm_data parent;
	char *path = get_parent_path_alloc(data->path);
	file_perm_data_fill(path, &parent);
	is_forbidden = write_protect_check(&parent);

	kfree(path);
	return is_forbidden;
}

static int file_exist(struct file_perm_data *data)
{
	return data->ino > BAD_INO;
}

static int real_path_from_symlink(char *filename, char *real)
{
	char *ptr;
	struct path path;
	int error = 0;
	int failed = 1;

	error = kern_path(filename, LOOKUP_FOLLOW, &path);
	if (!error) {
		ptr = d_path(&path, real, PATH_MAX);
		if (!IS_ERR(ptr)) {
			strcpy(real, ptr);
			failed = 0;
		}
		path_put(&path);
	}
	if (failed)
		strcpy(real, filename);

	return 0;
}

static int protect_check_with_flags(struct file_perm_data *data,
				    const int flags)
{
	int is_forbidden = 0;

	switch (flags & READ_WRITE_MASK) {
	case O_RDONLY:
		is_forbidden = read_protect_check(data);
		break;

	case O_WRONLY:
		is_forbidden = write_protect_check(data);
		break;

	case O_RDWR:
		is_forbidden = read_write_protect_check(data);
		break;
	}

	return is_forbidden;
}

static int sys_open_helper(int dirfd, char __user *pathname, int flags,
			   struct file_perm_data *data)
{
	int is_forbidden = 0;
	char *path = NULL;
	char *real = NULL;

	if (!conn_check_living())
		goto out;

	if (hackernel_trusted_proccess())
		goto out;

	path = get_absolute_path_alloc(dirfd, pathname);
	real = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!path || !real)
		goto out;

	real_path_from_symlink(path, real);

	file_perm_data_fill(real, data);
	is_forbidden = protect_check_with_flags(data, flags);
	if (is_forbidden)
		goto out;

	if (!(flags & O_CREAT))
		goto out;

	if (file_exist(data)) {
		goto out;
	}

	is_forbidden = parent_write_protect_check(data);

out:
	kfree(path);
	kfree(real);
	return is_forbidden;
}

static int sys_unlink_helper(int dirfd, char __user *pathname,
			     struct file_perm_data *data)
{
	int is_forbidden = 0;
	char *path = NULL;

	if (!conn_check_living())
		goto out;

	if (hackernel_trusted_proccess())
		goto out;

	path = get_absolute_path_alloc(dirfd, pathname);
	if (!path)
		goto out;

	file_perm_data_fill(path, data);
	is_forbidden = unlink_protect_check(data);
	if (is_forbidden)
		goto out;

	is_forbidden = parent_write_protect_check(data);
	if (is_forbidden)
		goto out;

	/* 节点删除前移除树中记录,防止inode重用导致误判 */
	if (data->this_perm != INVAILD_PERM)
		file_perm_tree_delete(data->fsid, data->ino);

out:
	kfree(path);
	return is_forbidden;
}

static int sys_rename_helper(int srcfd, char __user *srcpath, int dstfd,
			     char __user *dstpath, struct file_perm_data *data)
{
	int is_forbidden = 0;
	char *src = NULL;
	char *dst = NULL;

	if (!conn_check_living())
		goto out;

	if (hackernel_trusted_proccess())
		goto out;

	src = get_absolute_path_alloc(srcfd, srcpath);
	if (!src)
		goto out;
	file_perm_data_fill(src, data);
	is_forbidden = rename_protect_check(data);
	if (is_forbidden)
		goto out;

	is_forbidden = parent_write_protect_check(data);
	if (is_forbidden)
		goto out;

	dst = get_absolute_path_alloc(dstfd, dstpath);
	if (!dst)
		goto out;

	file_perm_data_fill(dst, data);
	is_forbidden = unlink_protect_check(data);
	if (is_forbidden)
		goto out;

	is_forbidden = parent_write_protect_check(data);
	if (is_forbidden)
		goto out;

out:
	kfree(src);
	kfree(dst);
	return is_forbidden;
}

HOOK_DEFINE2(open, char __user *, filename, int, flags, umode_t, mode)
{
	struct file_perm_data data;
	if (sys_open_helper(AT_FDCWD, filename, flags, &data))
		return -EPERM;

	return 0;
}

HOOK_DEFINE4(openat, int, dfd, char __user *, filename, int, flags, umode_t,
	     mode)
{
	struct file_perm_data data;
	if (sys_open_helper(dfd, filename, flags, &data))
		return -EPERM;
	return 0;
}

HOOK_DEFINE1(unlink, char __user *, pathname)
{
	struct file_perm_data data;
	if (sys_unlink_helper(AT_FDCWD, pathname, &data))
		return -EPERM;
	return 0;
}

HOOK_DEFINE3(unlinkat, int, dfd, char __user *, pathname, int, flag)
{
	struct file_perm_data data;
	if (sys_unlink_helper(dfd, pathname, &data))
		return -EPERM;
	return 0;
}

HOOK_DEFINE2(rename, char __user *, oldname, char __user *, newname)
{
	struct file_perm_data data;
	if (sys_rename_helper(AT_FDCWD, oldname, AT_FDCWD, newname, &data))
		return -EPERM;
	return 0;
}

HOOK_DEFINE4(renameat, int, olddfd, char __user *, oldname, int, newdfd,
	     char __user *, newname)
{
	struct file_perm_data data;
	if (sys_rename_helper(olddfd, oldname, newdfd, newname, &data))
		return -EPERM;
	return 0;
}

HOOK_DEFINE5(renameat2, int, olddfd, char __user *, oldname, int, newdfd,
	     char __user *, newname, unsigned int, flags)
{
	struct file_perm_data data;
	if (sys_rename_helper(olddfd, oldname, newdfd, newname, &data))
		return -EPERM;
	return 0;
}

HOOK_DEFINE2(mkdir, char __user *, pathname, umode_t, mode)
{
	struct file_perm_data data;
	if (sys_open_helper(AT_FDCWD, pathname, O_CREAT, &data))
		return -EPERM;
	return 0;
}

HOOK_DEFINE3(mkdirat, int, dfd, char __user *, pathname, umode_t, mode)
{
	struct file_perm_data data;
	if (sys_open_helper(dfd, pathname, O_CREAT, &data))
		return -EPERM;
	return 0;
}

HOOK_DEFINE1(rmdir, char __user *, pathname)
{
	struct file_perm_data data;

	if (sys_unlink_helper(AT_FDCWD, pathname, &data))
		return -EPERM;
	return 0;
}

HOOK_DEFINE2(link, char __user *, oldname, char __user *, newname)
{
	struct file_perm_data data;
	if (sys_open_helper(AT_FDCWD, newname, O_CREAT, &data))
		return -EPERM;
	return 0;
}

HOOK_DEFINE5(linkat, int, olddfd, char __user *, oldname, int, newdfd,
	     char __user *, newname, int, flags)
{
	struct file_perm_data data;
	if (sys_open_helper(newdfd, newname, O_CREAT, &data))
		return -EPERM;
	return 0;
}

HOOK_DEFINE2(symlink, char __user *, oldname, char __user *, newname)
{
	struct file_perm_data data;
	if (sys_open_helper(AT_FDCWD, newname, O_CREAT, &data))
		return -EPERM;
	return 0;
}

HOOK_DEFINE3(symlinkat, char __user *, oldname, int, newdfd, char __user *,
	     newname)
{
	struct file_perm_data data;
	if (sys_open_helper(newdfd, newname, O_CREAT, &data))
		return -EPERM;
	return 0;
}

HOOK_DEFINE3(mknod, char __user *, filename, umode_t, mode, unsigned, dev)
{
	struct file_perm_data data;
	if (sys_open_helper(AT_FDCWD, filename, O_CREAT, &data))
		return -EPERM;
	return 0;
}

HOOK_DEFINE4(mknodat, int, dfd, char __user *, filename, umode_t, mode,
	     unsigned int, dev)
{
	struct file_perm_data data;
	if (sys_open_helper(dfd, filename, O_CREAT, &data))
		return -EPERM;
	return 0;
}

int file_protect_enable(void)
{
	REG_HOOK(open);
	REG_HOOK(openat);
	REG_HOOK(unlink);
	REG_HOOK(unlinkat);
	REG_HOOK(rename);
	REG_HOOK(renameat);
	REG_HOOK(renameat2);
	REG_HOOK(mkdir);
	REG_HOOK(mkdirat);
	REG_HOOK(rmdir);
	REG_HOOK(link);
	REG_HOOK(linkat);
	REG_HOOK(symlink);
	REG_HOOK(symlinkat);
	REG_HOOK(mknod);
	REG_HOOK(mknodat);
	return 0;
}

int file_protect_disable(void)
{
	UNREG_HOOK(open);
	UNREG_HOOK(openat);
	UNREG_HOOK(unlink);
	UNREG_HOOK(unlinkat);
	UNREG_HOOK(rename);
	UNREG_HOOK(renameat);
	UNREG_HOOK(renameat2);
	UNREG_HOOK(mkdir);
	UNREG_HOOK(mkdirat);
	UNREG_HOOK(rmdir);
	UNREG_HOOK(link);
	UNREG_HOOK(linkat);
	UNREG_HOOK(symlink);
	UNREG_HOOK(symlinkat);
	UNREG_HOOK(mknod);
	UNREG_HOOK(mknodat);
	file_perm_tree_clear();
	return 0;
}

int file_protect_init(void)
{
	return 0;
}

int file_protect_destory(void)
{
	return file_protect_disable();
}
