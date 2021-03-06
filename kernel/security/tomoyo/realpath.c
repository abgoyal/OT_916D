

#include <linux/types.h>
#include <linux/mount.h>
#include <linux/mnt_namespace.h>
#include <linux/fs_struct.h>
#include <linux/hash.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include "common.h"

int tomoyo_encode(char *buffer, int buflen, const char *str)
{
	while (1) {
		const unsigned char c = *(unsigned char *) str++;

		if (tomoyo_is_valid(c)) {
			if (--buflen <= 0)
				break;
			*buffer++ = (char) c;
			if (c != '\\')
				continue;
			if (--buflen <= 0)
				break;
			*buffer++ = (char) c;
			continue;
		}
		if (!c) {
			if (--buflen <= 0)
				break;
			*buffer = '\0';
			return 0;
		}
		buflen -= 4;
		if (buflen <= 0)
			break;
		*buffer++ = '\\';
		*buffer++ = (c >> 6) + '0';
		*buffer++ = ((c >> 3) & 7) + '0';
		*buffer++ = (c & 7) + '0';
	}
	return -ENOMEM;
}

int tomoyo_realpath_from_path2(struct path *path, char *newname,
			       int newname_len)
{
	int error = -ENOMEM;
	struct dentry *dentry = path->dentry;
	char *sp;

	if (!dentry || !path->mnt || !newname || newname_len <= 2048)
		return -EINVAL;
	if (dentry->d_op && dentry->d_op->d_dname) {
		/* For "socket:[\$]" and "pipe:[\$]". */
		static const int offset = 1536;
		sp = dentry->d_op->d_dname(dentry, newname + offset,
					   newname_len - offset);
	} else {
		struct path ns_root = {.mnt = NULL, .dentry = NULL};

		spin_lock(&dcache_lock);
		/* go to whatever namespace root we are under */
		sp = __d_path(path, &ns_root, newname, newname_len);
		spin_unlock(&dcache_lock);
		/* Prepend "/proc" prefix if using internal proc vfs mount. */
		if (!IS_ERR(sp) && (path->mnt->mnt_flags & MNT_INTERNAL) &&
		    (path->mnt->mnt_sb->s_magic == PROC_SUPER_MAGIC)) {
			sp -= 5;
			if (sp >= newname)
				memcpy(sp, "/proc", 5);
			else
				sp = ERR_PTR(-ENOMEM);
		}
	}
	if (IS_ERR(sp))
		error = PTR_ERR(sp);
	else
		error = tomoyo_encode(newname, sp - newname, sp);
	/* Append trailing '/' if dentry is a directory. */
	if (!error && dentry->d_inode && S_ISDIR(dentry->d_inode->i_mode)
	    && *newname) {
		sp = newname + strlen(newname);
		if (*(sp - 1) != '/') {
			if (sp < newname + newname_len - 4) {
				*sp++ = '/';
				*sp = '\0';
			} else {
				error = -ENOMEM;
			}
		}
	}
	if (error)
		printk(KERN_WARNING "tomoyo_realpath: Pathname too long.\n");
	return error;
}

char *tomoyo_realpath_from_path(struct path *path)
{
	char *buf = kzalloc(sizeof(struct tomoyo_page_buffer), GFP_NOFS);

	BUILD_BUG_ON(sizeof(struct tomoyo_page_buffer)
		     <= TOMOYO_MAX_PATHNAME_LEN - 1);
	if (!buf)
		return NULL;
	if (tomoyo_realpath_from_path2(path, buf,
				       TOMOYO_MAX_PATHNAME_LEN - 1) == 0)
		return buf;
	kfree(buf);
	return NULL;
}

char *tomoyo_realpath(const char *pathname)
{
	struct path path;

	if (pathname && kern_path(pathname, LOOKUP_FOLLOW, &path) == 0) {
		char *buf = tomoyo_realpath_from_path(&path);
		path_put(&path);
		return buf;
	}
	return NULL;
}

char *tomoyo_realpath_nofollow(const char *pathname)
{
	struct path path;

	if (pathname && kern_path(pathname, 0, &path) == 0) {
		char *buf = tomoyo_realpath_from_path(&path);
		path_put(&path);
		return buf;
	}
	return NULL;
}

/* Memory allocated for non-string data. */
static atomic_t tomoyo_policy_memory_size;
/* Quota for holding policy. */
static unsigned int tomoyo_quota_for_policy;

bool tomoyo_memory_ok(void *ptr)
{
	int allocated_len = ptr ? ksize(ptr) : 0;
	atomic_add(allocated_len, &tomoyo_policy_memory_size);
	if (ptr && (!tomoyo_quota_for_policy ||
		    atomic_read(&tomoyo_policy_memory_size)
		    <= tomoyo_quota_for_policy)) {
		memset(ptr, 0, allocated_len);
		return true;
	}
	printk(KERN_WARNING "ERROR: Out of memory "
	       "for tomoyo_alloc_element().\n");
	if (!tomoyo_policy_loaded)
		panic("MAC Initialization failed.\n");
	return false;
}

void *tomoyo_commit_ok(void *data, const unsigned int size)
{
	void *ptr = kzalloc(size, GFP_NOFS);
	if (tomoyo_memory_ok(ptr)) {
		memmove(ptr, data, size);
		memset(data, 0, size);
		return ptr;
	}
	return NULL;
}

void tomoyo_memory_free(void *ptr)
{
	atomic_sub(ksize(ptr), &tomoyo_policy_memory_size);
	kfree(ptr);
}

struct list_head tomoyo_name_list[TOMOYO_MAX_HASH];

const struct tomoyo_path_info *tomoyo_get_name(const char *name)
{
	struct tomoyo_name_entry *ptr;
	unsigned int hash;
	int len;
	int allocated_len;
	struct list_head *head;

	if (!name)
		return NULL;
	len = strlen(name) + 1;
	hash = full_name_hash((const unsigned char *) name, len - 1);
	head = &tomoyo_name_list[hash_long(hash, TOMOYO_HASH_BITS)];
	if (mutex_lock_interruptible(&tomoyo_policy_lock))
		return NULL;
	list_for_each_entry(ptr, head, list) {
		if (hash != ptr->entry.hash || strcmp(name, ptr->entry.name))
			continue;
		atomic_inc(&ptr->users);
		goto out;
	}
	ptr = kzalloc(sizeof(*ptr) + len, GFP_NOFS);
	allocated_len = ptr ? ksize(ptr) : 0;
	if (!ptr || (tomoyo_quota_for_policy &&
		     atomic_read(&tomoyo_policy_memory_size) + allocated_len
		     > tomoyo_quota_for_policy)) {
		kfree(ptr);
		printk(KERN_WARNING "ERROR: Out of memory "
		       "for tomoyo_get_name().\n");
		if (!tomoyo_policy_loaded)
			panic("MAC Initialization failed.\n");
		ptr = NULL;
		goto out;
	}
	atomic_add(allocated_len, &tomoyo_policy_memory_size);
	ptr->entry.name = ((char *) ptr) + sizeof(*ptr);
	memmove((char *) ptr->entry.name, name, len);
	atomic_set(&ptr->users, 1);
	tomoyo_fill_path_info(&ptr->entry);
	list_add_tail(&ptr->list, head);
 out:
	mutex_unlock(&tomoyo_policy_lock);
	return ptr ? &ptr->entry : NULL;
}

void __init tomoyo_realpath_init(void)
{
	int i;

	BUILD_BUG_ON(TOMOYO_MAX_PATHNAME_LEN > PATH_MAX);
	for (i = 0; i < TOMOYO_MAX_HASH; i++)
		INIT_LIST_HEAD(&tomoyo_name_list[i]);
	INIT_LIST_HEAD(&tomoyo_kernel_domain.acl_info_list);
	tomoyo_kernel_domain.domainname = tomoyo_get_name(TOMOYO_ROOT_NAME);
	/*
	 * tomoyo_read_lock() is not needed because this function is
	 * called before the first "delete" request.
	 */
	list_add_tail_rcu(&tomoyo_kernel_domain.list, &tomoyo_domain_list);
	if (tomoyo_find_domain(TOMOYO_ROOT_NAME) != &tomoyo_kernel_domain)
		panic("Can't register tomoyo_kernel_domain");
}

int tomoyo_read_memory_counter(struct tomoyo_io_buffer *head)
{
	if (!head->read_eof) {
		const unsigned int policy
			= atomic_read(&tomoyo_policy_memory_size);
		char buffer[64];

		memset(buffer, 0, sizeof(buffer));
		if (tomoyo_quota_for_policy)
			snprintf(buffer, sizeof(buffer) - 1,
				 "   (Quota: %10u)",
				 tomoyo_quota_for_policy);
		else
			buffer[0] = '\0';
		tomoyo_io_printf(head, "Policy:  %10u%s\n", policy, buffer);
		tomoyo_io_printf(head, "Total:   %10u\n", policy);
		head->read_eof = true;
	}
	return 0;
}

int tomoyo_write_memory_quota(struct tomoyo_io_buffer *head)
{
	char *data = head->write_buf;
	unsigned int size;

	if (sscanf(data, "Policy: %u", &size) == 1)
		tomoyo_quota_for_policy = size;
	return 0;
}
