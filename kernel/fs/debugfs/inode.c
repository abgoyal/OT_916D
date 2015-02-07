

/* uncomment to get debug messages from the debug filesystem, ah the irony. */
/* #define DEBUG */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/kobject.h>
#include <linux/namei.h>
#include <linux/debugfs.h>
#include <linux/fsnotify.h>
#include <linux/string.h>
#include <linux/magic.h>
#include <linux/slab.h>

static struct vfsmount *debugfs_mount;
static int debugfs_mount_count;
static bool debugfs_registered;

static struct inode *debugfs_get_inode(struct super_block *sb, int mode, dev_t dev,
				       void *data, const struct file_operations *fops)

{
	struct inode *inode = new_inode(sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		switch (mode & S_IFMT) {
		default:
			init_special_inode(inode, mode, dev);
			break;
		case S_IFREG:
			inode->i_fop = fops ? fops : &debugfs_file_operations;
			inode->i_private = data;
			break;
		case S_IFLNK:
			inode->i_op = &debugfs_link_operations;
			inode->i_fop = fops;
			inode->i_private = data;
			break;
		case S_IFDIR:
			inode->i_op = &simple_dir_inode_operations;
			inode->i_fop = fops ? fops : &simple_dir_operations;
			inode->i_private = data;

			/* directory inodes start off with i_nlink == 2
			 * (for "." entry) */
			inc_nlink(inode);
			break;
		}
	}
	return inode; 
}

/* SMP-safe */
static int debugfs_mknod(struct inode *dir, struct dentry *dentry,
			 int mode, dev_t dev, void *data,
			 const struct file_operations *fops)
{
	struct inode *inode;
	int error = -EPERM;

	if (dentry->d_inode)
		return -EEXIST;

	inode = debugfs_get_inode(dir->i_sb, mode, dev, data, fops);
	if (inode) {
		d_instantiate(dentry, inode);
		dget(dentry);
		error = 0;
	}
	return error;
}

static int debugfs_mkdir(struct inode *dir, struct dentry *dentry, int mode,
			 void *data, const struct file_operations *fops)
{
	int res;

	mode = (mode & (S_IRWXUGO | S_ISVTX)) | S_IFDIR;
	res = debugfs_mknod(dir, dentry, mode, 0, data, fops);
	if (!res) {
		inc_nlink(dir);
		fsnotify_mkdir(dir, dentry);
	}
	return res;
}

static int debugfs_link(struct inode *dir, struct dentry *dentry, int mode,
			void *data, const struct file_operations *fops)
{
	mode = (mode & S_IALLUGO) | S_IFLNK;
	return debugfs_mknod(dir, dentry, mode, 0, data, fops);
}

static int debugfs_create(struct inode *dir, struct dentry *dentry, int mode,
			  void *data, const struct file_operations *fops)
{
	int res;

	mode = (mode & S_IALLUGO) | S_IFREG;
	res = debugfs_mknod(dir, dentry, mode, 0, data, fops);
	if (!res)
		fsnotify_create(dir, dentry);
	return res;
}

static inline int debugfs_positive(struct dentry *dentry)
{
	return dentry->d_inode && !d_unhashed(dentry);
}

static int debug_fill_super(struct super_block *sb, void *data, int silent)
{
	static struct tree_descr debug_files[] = {{""}};

	return simple_fill_super(sb, DEBUGFS_MAGIC, debug_files);
}

static int debug_get_sb(struct file_system_type *fs_type,
			int flags, const char *dev_name,
			void *data, struct vfsmount *mnt)
{
	return get_sb_single(fs_type, flags, data, debug_fill_super, mnt);
}

static struct file_system_type debug_fs_type = {
	.owner =	THIS_MODULE,
	.name =		"debugfs",
	.get_sb =	debug_get_sb,
	.kill_sb =	kill_litter_super,
};

static int debugfs_create_by_name(const char *name, mode_t mode,
				  struct dentry *parent,
				  struct dentry **dentry,
				  void *data,
				  const struct file_operations *fops)
{
	int error = 0;

	/* If the parent is not specified, we create it in the root.
	 * We need the root dentry to do this, which is in the super 
	 * block. A pointer to that is in the struct vfsmount that we
	 * have around.
	 */
	if (!parent)
		parent = debugfs_mount->mnt_sb->s_root;

	*dentry = NULL;
	mutex_lock(&parent->d_inode->i_mutex);
	*dentry = lookup_one_len(name, parent, strlen(name));
	if (!IS_ERR(*dentry)) {
		switch (mode & S_IFMT) {
		case S_IFDIR:
			error = debugfs_mkdir(parent->d_inode, *dentry, mode,
					      data, fops);
			break;
		case S_IFLNK:
			error = debugfs_link(parent->d_inode, *dentry, mode,
					     data, fops);
			break;
		default:
			error = debugfs_create(parent->d_inode, *dentry, mode,
					       data, fops);
			break;
		}
		dput(*dentry);
	} else
		error = PTR_ERR(*dentry);
	mutex_unlock(&parent->d_inode->i_mutex);

	return error;
}

struct dentry *debugfs_create_file(const char *name, mode_t mode,
				   struct dentry *parent, void *data,
				   const struct file_operations *fops)
{
	struct dentry *dentry = NULL;
	int error;

	pr_debug("debugfs: creating file '%s'\n",name);

	error = simple_pin_fs(&debug_fs_type, &debugfs_mount,
			      &debugfs_mount_count);
	if (error)
		goto exit;

	error = debugfs_create_by_name(name, mode, parent, &dentry,
				       data, fops);
	if (error) {
		dentry = NULL;
		simple_release_fs(&debugfs_mount, &debugfs_mount_count);
		goto exit;
	}
exit:
	return dentry;
}
EXPORT_SYMBOL_GPL(debugfs_create_file);

struct dentry *debugfs_create_dir(const char *name, struct dentry *parent)
{
	return debugfs_create_file(name, 
				   S_IFDIR | S_IRWXU | S_IRUGO | S_IXUGO,
				   parent, NULL, NULL);
}
EXPORT_SYMBOL_GPL(debugfs_create_dir);

struct dentry *debugfs_create_symlink(const char *name, struct dentry *parent,
				      const char *target)
{
	struct dentry *result;
	char *link;

	link = kstrdup(target, GFP_KERNEL);
	if (!link)
		return NULL;

	result = debugfs_create_file(name, S_IFLNK | S_IRWXUGO, parent, link,
				     NULL);
	if (!result)
		kfree(link);
	return result;
}
EXPORT_SYMBOL_GPL(debugfs_create_symlink);

static void __debugfs_remove(struct dentry *dentry, struct dentry *parent)
{
	int ret = 0;

	if (debugfs_positive(dentry)) {
		if (dentry->d_inode) {
			dget(dentry);
			switch (dentry->d_inode->i_mode & S_IFMT) {
			case S_IFDIR:
				ret = simple_rmdir(parent->d_inode, dentry);
				break;
			case S_IFLNK:
				kfree(dentry->d_inode->i_private);
				/* fall through */
			default:
				simple_unlink(parent->d_inode, dentry);
				break;
			}
			if (!ret)
				d_delete(dentry);
			dput(dentry);
		}
	}
}

void debugfs_remove(struct dentry *dentry)
{
	struct dentry *parent;
	
	if (!dentry)
		return;

	parent = dentry->d_parent;
	if (!parent || !parent->d_inode)
		return;

	mutex_lock(&parent->d_inode->i_mutex);
	__debugfs_remove(dentry, parent);
	mutex_unlock(&parent->d_inode->i_mutex);
	simple_release_fs(&debugfs_mount, &debugfs_mount_count);
}
EXPORT_SYMBOL_GPL(debugfs_remove);

void debugfs_remove_recursive(struct dentry *dentry)
{
	struct dentry *child;
	struct dentry *parent;

	if (!dentry)
		return;

	parent = dentry->d_parent;
	if (!parent || !parent->d_inode)
		return;

	parent = dentry;
	mutex_lock(&parent->d_inode->i_mutex);

	while (1) {
		/*
		 * When all dentries under "parent" has been removed,
		 * walk up the tree until we reach our starting point.
		 */
		if (list_empty(&parent->d_subdirs)) {
			mutex_unlock(&parent->d_inode->i_mutex);
			if (parent == dentry)
				break;
			parent = parent->d_parent;
			mutex_lock(&parent->d_inode->i_mutex);
		}
		child = list_entry(parent->d_subdirs.next, struct dentry,
				d_u.d_child);
 next_sibling:

		/*
		 * If "child" isn't empty, walk down the tree and
		 * remove all its descendants first.
		 */
		if (!list_empty(&child->d_subdirs)) {
			mutex_unlock(&parent->d_inode->i_mutex);
			parent = child;
			mutex_lock(&parent->d_inode->i_mutex);
			continue;
		}
		__debugfs_remove(child, parent);
		if (parent->d_subdirs.next == &child->d_u.d_child) {
			/*
			 * Try the next sibling.
			 */
			if (child->d_u.d_child.next != &parent->d_subdirs) {
				child = list_entry(child->d_u.d_child.next,
						   struct dentry,
						   d_u.d_child);
				goto next_sibling;
			}

			/*
			 * Avoid infinite loop if we fail to remove
			 * one dentry.
			 */
			mutex_unlock(&parent->d_inode->i_mutex);
			break;
		}
		simple_release_fs(&debugfs_mount, &debugfs_mount_count);
	}

	parent = dentry->d_parent;
	mutex_lock(&parent->d_inode->i_mutex);
	__debugfs_remove(dentry, parent);
	mutex_unlock(&parent->d_inode->i_mutex);
	simple_release_fs(&debugfs_mount, &debugfs_mount_count);
}
EXPORT_SYMBOL_GPL(debugfs_remove_recursive);

struct dentry *debugfs_rename(struct dentry *old_dir, struct dentry *old_dentry,
		struct dentry *new_dir, const char *new_name)
{
	int error;
	struct dentry *dentry = NULL, *trap;
	const char *old_name;

	trap = lock_rename(new_dir, old_dir);
	/* Source or destination directories don't exist? */
	if (!old_dir->d_inode || !new_dir->d_inode)
		goto exit;
	/* Source does not exist, cyclic rename, or mountpoint? */
	if (!old_dentry->d_inode || old_dentry == trap ||
	    d_mountpoint(old_dentry))
		goto exit;
	dentry = lookup_one_len(new_name, new_dir, strlen(new_name));
	/* Lookup failed, cyclic rename or target exists? */
	if (IS_ERR(dentry) || dentry == trap || dentry->d_inode)
		goto exit;

	old_name = fsnotify_oldname_init(old_dentry->d_name.name);

	error = simple_rename(old_dir->d_inode, old_dentry, new_dir->d_inode,
		dentry);
	if (error) {
		fsnotify_oldname_free(old_name);
		goto exit;
	}
	d_move(old_dentry, dentry);
	fsnotify_move(old_dir->d_inode, new_dir->d_inode, old_name,
		S_ISDIR(old_dentry->d_inode->i_mode),
		NULL, old_dentry);
	fsnotify_oldname_free(old_name);
	unlock_rename(new_dir, old_dir);
	dput(dentry);
	return old_dentry;
exit:
	if (dentry && !IS_ERR(dentry))
		dput(dentry);
	unlock_rename(new_dir, old_dir);
	return NULL;
}
EXPORT_SYMBOL_GPL(debugfs_rename);

bool debugfs_initialized(void)
{
	return debugfs_registered;
}
EXPORT_SYMBOL_GPL(debugfs_initialized);


static struct kobject *debug_kobj;

static int __init debugfs_init(void)
{
	int retval;

	debug_kobj = kobject_create_and_add("debug", kernel_kobj);
	if (!debug_kobj)
		return -EINVAL;

	retval = register_filesystem(&debug_fs_type);
	if (retval)
		kobject_put(debug_kobj);
	else
		debugfs_registered = true;

	return retval;
}

static void __exit debugfs_exit(void)
{
	debugfs_registered = false;

	simple_release_fs(&debugfs_mount, &debugfs_mount_count);
	unregister_filesystem(&debug_fs_type);
	kobject_put(debug_kobj);
}

core_initcall(debugfs_init);
module_exit(debugfs_exit);
MODULE_LICENSE("GPL");

