

#include <linux/capability.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/highmem.h>
#include <linux/firmware.h>
#include <linux/slab.h>

#define to_dev(obj) container_of(obj, struct device, kobj)

MODULE_AUTHOR("Manuel Estrada Sainz");
MODULE_DESCRIPTION("Multi purpose firmware loading support");
MODULE_LICENSE("GPL");

/* Builtin firmware support */

#ifdef CONFIG_FW_LOADER

extern struct builtin_fw __start_builtin_fw[];
extern struct builtin_fw __end_builtin_fw[];

static bool fw_get_builtin_firmware(struct firmware *fw, const char *name)
{
	struct builtin_fw *b_fw;

	for (b_fw = __start_builtin_fw; b_fw != __end_builtin_fw; b_fw++) {
		if (strcmp(name, b_fw->name) == 0) {
			fw->size = b_fw->size;
			fw->data = b_fw->data;
			return true;
		}
	}

	return false;
}

static bool fw_is_builtin_firmware(const struct firmware *fw)
{
	struct builtin_fw *b_fw;

	for (b_fw = __start_builtin_fw; b_fw != __end_builtin_fw; b_fw++)
		if (fw->data == b_fw->data)
			return true;

	return false;
}

#else /* Module case - no builtin firmware support */

static inline bool fw_get_builtin_firmware(struct firmware *fw, const char *name)
{
	return false;
}

static inline bool fw_is_builtin_firmware(const struct firmware *fw)
{
	return false;
}
#endif

enum {
	FW_STATUS_LOADING,
	FW_STATUS_DONE,
	FW_STATUS_ABORT,
};

static int loading_timeout = 60;	/* In seconds */

static DEFINE_MUTEX(fw_lock);

struct firmware_priv {
	struct completion completion;
	struct bin_attribute attr_data;
	struct firmware *fw;
	unsigned long status;
	struct page **pages;
	int nr_pages;
	int page_array_size;
	struct timer_list timeout;
	bool nowait;
	char fw_id[];
};

static void
fw_load_abort(struct firmware_priv *fw_priv)
{
	set_bit(FW_STATUS_ABORT, &fw_priv->status);
	wmb();
	complete(&fw_priv->completion);
}

static ssize_t
firmware_timeout_show(struct class *class,
		      struct class_attribute *attr,
		      char *buf)
{
	return sprintf(buf, "%d\n", loading_timeout);
}

static ssize_t
firmware_timeout_store(struct class *class,
			struct class_attribute *attr,
			const char *buf, size_t count)
{
	loading_timeout = simple_strtol(buf, NULL, 10);
	if (loading_timeout < 0)
		loading_timeout = 0;
	return count;
}

static struct class_attribute firmware_class_attrs[] = {
	__ATTR(timeout, S_IWUSR | S_IRUGO,
		firmware_timeout_show, firmware_timeout_store),
	__ATTR_NULL
};

static void fw_dev_release(struct device *dev)
{
	struct firmware_priv *fw_priv = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < fw_priv->nr_pages; i++)
		__free_page(fw_priv->pages[i]);
	kfree(fw_priv->pages);
	kfree(fw_priv);
	kfree(dev);

	module_put(THIS_MODULE);
}

static int firmware_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct firmware_priv *fw_priv = dev_get_drvdata(dev);

	if (add_uevent_var(env, "FIRMWARE=%s", fw_priv->fw_id))
		return -ENOMEM;
	if (add_uevent_var(env, "TIMEOUT=%i", loading_timeout))
		return -ENOMEM;
	if (add_uevent_var(env, "ASYNC=%d", fw_priv->nowait))
		return -ENOMEM;

	return 0;
}

static struct class firmware_class = {
	.name		= "firmware",
	.class_attrs	= firmware_class_attrs,
	.dev_uevent	= firmware_uevent,
	.dev_release	= fw_dev_release,
};

static ssize_t firmware_loading_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct firmware_priv *fw_priv = dev_get_drvdata(dev);
	int loading = test_bit(FW_STATUS_LOADING, &fw_priv->status);
	return sprintf(buf, "%d\n", loading);
}

static void firmware_free_data(const struct firmware *fw)
{
	int i;
	vunmap(fw->data);
	if (fw->pages) {
		for (i = 0; i < PFN_UP(fw->size); i++)
			__free_page(fw->pages[i]);
		kfree(fw->pages);
	}
}

/* Some architectures don't have PAGE_KERNEL_RO */
#ifndef PAGE_KERNEL_RO
#define PAGE_KERNEL_RO PAGE_KERNEL
#endif
static ssize_t firmware_loading_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct firmware_priv *fw_priv = dev_get_drvdata(dev);
	int loading = simple_strtol(buf, NULL, 10);
	int i;

	switch (loading) {
	case 1:
		mutex_lock(&fw_lock);
		if (!fw_priv->fw) {
			mutex_unlock(&fw_lock);
			break;
		}
		firmware_free_data(fw_priv->fw);
		memset(fw_priv->fw, 0, sizeof(struct firmware));
		/* If the pages are not owned by 'struct firmware' */
		for (i = 0; i < fw_priv->nr_pages; i++)
			__free_page(fw_priv->pages[i]);
		kfree(fw_priv->pages);
		fw_priv->pages = NULL;
		fw_priv->page_array_size = 0;
		fw_priv->nr_pages = 0;
		set_bit(FW_STATUS_LOADING, &fw_priv->status);
		mutex_unlock(&fw_lock);
		break;
	case 0:
		if (test_bit(FW_STATUS_LOADING, &fw_priv->status)) {
			vunmap(fw_priv->fw->data);
			fw_priv->fw->data = vmap(fw_priv->pages,
						 fw_priv->nr_pages,
						 0, PAGE_KERNEL_RO);
			if (!fw_priv->fw->data) {
				dev_err(dev, "%s: vmap() failed\n", __func__);
				goto err;
			}
			/* Pages are now owned by 'struct firmware' */
			fw_priv->fw->pages = fw_priv->pages;
			fw_priv->pages = NULL;

			fw_priv->page_array_size = 0;
			fw_priv->nr_pages = 0;
			complete(&fw_priv->completion);
			clear_bit(FW_STATUS_LOADING, &fw_priv->status);
			break;
		}
		/* fallthrough */
	default:
		dev_err(dev, "%s: unexpected value (%d)\n", __func__, loading);
		/* fallthrough */
	case -1:
	err:
		fw_load_abort(fw_priv);
		break;
	}

	return count;
}

static DEVICE_ATTR(loading, 0644, firmware_loading_show, firmware_loading_store);

static ssize_t
firmware_data_read(struct file *filp, struct kobject *kobj,
		   struct bin_attribute *bin_attr, char *buffer, loff_t offset,
		   size_t count)
{
	struct device *dev = to_dev(kobj);
	struct firmware_priv *fw_priv = dev_get_drvdata(dev);
	struct firmware *fw;
	ssize_t ret_count;

	mutex_lock(&fw_lock);
	fw = fw_priv->fw;
	if (!fw || test_bit(FW_STATUS_DONE, &fw_priv->status)) {
		ret_count = -ENODEV;
		goto out;
	}
	if (offset > fw->size) {
		ret_count = 0;
		goto out;
	}
	if (count > fw->size - offset)
		count = fw->size - offset;

	ret_count = count;

	while (count) {
		void *page_data;
		int page_nr = offset >> PAGE_SHIFT;
		int page_ofs = offset & (PAGE_SIZE-1);
		int page_cnt = min_t(size_t, PAGE_SIZE - page_ofs, count);

		page_data = kmap(fw_priv->pages[page_nr]);

		memcpy(buffer, page_data + page_ofs, page_cnt);

		kunmap(fw_priv->pages[page_nr]);
		buffer += page_cnt;
		offset += page_cnt;
		count -= page_cnt;
	}
out:
	mutex_unlock(&fw_lock);
	return ret_count;
}

static int
fw_realloc_buffer(struct firmware_priv *fw_priv, int min_size)
{
	int pages_needed = ALIGN(min_size, PAGE_SIZE) >> PAGE_SHIFT;

	/* If the array of pages is too small, grow it... */
	if (fw_priv->page_array_size < pages_needed) {
		int new_array_size = max(pages_needed,
					 fw_priv->page_array_size * 2);
		struct page **new_pages;

		new_pages = kmalloc(new_array_size * sizeof(void *),
				    GFP_KERNEL);
		if (!new_pages) {
			fw_load_abort(fw_priv);
			return -ENOMEM;
		}
		memcpy(new_pages, fw_priv->pages,
		       fw_priv->page_array_size * sizeof(void *));
		memset(&new_pages[fw_priv->page_array_size], 0, sizeof(void *) *
		       (new_array_size - fw_priv->page_array_size));
		kfree(fw_priv->pages);
		fw_priv->pages = new_pages;
		fw_priv->page_array_size = new_array_size;
	}

	while (fw_priv->nr_pages < pages_needed) {
		fw_priv->pages[fw_priv->nr_pages] =
			alloc_page(GFP_KERNEL | __GFP_HIGHMEM);

		if (!fw_priv->pages[fw_priv->nr_pages]) {
			fw_load_abort(fw_priv);
			return -ENOMEM;
		}
		fw_priv->nr_pages++;
	}
	return 0;
}

static ssize_t
firmware_data_write(struct file* filp, struct kobject *kobj,
		    struct bin_attribute *bin_attr, char *buffer,
		    loff_t offset, size_t count)
{
	struct device *dev = to_dev(kobj);
	struct firmware_priv *fw_priv = dev_get_drvdata(dev);
	struct firmware *fw;
	ssize_t retval;

	if (!capable(CAP_SYS_RAWIO))
		return -EPERM;

	mutex_lock(&fw_lock);
	fw = fw_priv->fw;
	if (!fw || test_bit(FW_STATUS_DONE, &fw_priv->status)) {
		retval = -ENODEV;
		goto out;
	}
	retval = fw_realloc_buffer(fw_priv, offset + count);
	if (retval)
		goto out;

	retval = count;

	while (count) {
		void *page_data;
		int page_nr = offset >> PAGE_SHIFT;
		int page_ofs = offset & (PAGE_SIZE - 1);
		int page_cnt = min_t(size_t, PAGE_SIZE - page_ofs, count);

		page_data = kmap(fw_priv->pages[page_nr]);

		memcpy(page_data + page_ofs, buffer, page_cnt);

		kunmap(fw_priv->pages[page_nr]);
		buffer += page_cnt;
		offset += page_cnt;
		count -= page_cnt;
	}

	fw->size = max_t(size_t, offset, fw->size);
out:
	mutex_unlock(&fw_lock);
	return retval;
}

static struct bin_attribute firmware_attr_data_tmpl = {
	.attr = {.name = "data", .mode = 0644},
	.size = 0,
	.read = firmware_data_read,
	.write = firmware_data_write,
};

static void
firmware_class_timeout(u_long data)
{
	struct firmware_priv *fw_priv = (struct firmware_priv *) data;
	fw_load_abort(fw_priv);
}

static int fw_register_device(struct device **dev_p, const char *fw_name,
			      struct device *device)
{
	int retval;
	struct firmware_priv *fw_priv =
		kzalloc(sizeof(*fw_priv) + strlen(fw_name) + 1 , GFP_KERNEL);
	struct device *f_dev = kzalloc(sizeof(*f_dev), GFP_KERNEL);

	*dev_p = NULL;

	if (!fw_priv || !f_dev) {
		dev_err(device, "%s: kmalloc failed\n", __func__);
		retval = -ENOMEM;
		goto error_kfree;
	}

	strcpy(fw_priv->fw_id, fw_name);
	init_completion(&fw_priv->completion);
	fw_priv->attr_data = firmware_attr_data_tmpl;
	fw_priv->timeout.function = firmware_class_timeout;
	fw_priv->timeout.data = (u_long) fw_priv;
	init_timer(&fw_priv->timeout);

	dev_set_name(f_dev, "%s", dev_name(device));
	f_dev->parent = device;
	f_dev->class = &firmware_class;
	dev_set_drvdata(f_dev, fw_priv);
	dev_set_uevent_suppress(f_dev, 1);
	retval = device_register(f_dev);
	if (retval) {
		dev_err(device, "%s: device_register failed\n", __func__);
		put_device(f_dev);
		return retval;
	}
	*dev_p = f_dev;
	return 0;

error_kfree:
	kfree(f_dev);
	kfree(fw_priv);
	return retval;
}

static int fw_setup_device(struct firmware *fw, struct device **dev_p,
			   const char *fw_name, struct device *device,
			   int uevent, bool nowait)
{
	struct device *f_dev;
	struct firmware_priv *fw_priv;
	int retval;

	*dev_p = NULL;
	retval = fw_register_device(&f_dev, fw_name, device);
	if (retval)
		goto out;

	/* Need to pin this module until class device is destroyed */
	__module_get(THIS_MODULE);

	fw_priv = dev_get_drvdata(f_dev);

	fw_priv->nowait = nowait;

	fw_priv->fw = fw;
	sysfs_bin_attr_init(&fw_priv->attr_data);
	retval = sysfs_create_bin_file(&f_dev->kobj, &fw_priv->attr_data);
	if (retval) {
		dev_err(device, "%s: sysfs_create_bin_file failed\n", __func__);
		goto error_unreg;
	}

	retval = device_create_file(f_dev, &dev_attr_loading);
	if (retval) {
		dev_err(device, "%s: device_create_file failed\n", __func__);
		goto error_unreg;
	}

	if (uevent)
		dev_set_uevent_suppress(f_dev, 0);
	*dev_p = f_dev;
	goto out;

error_unreg:
	device_unregister(f_dev);
out:
	return retval;
}

static int
_request_firmware(const struct firmware **firmware_p, const char *name,
		 struct device *device, int uevent, bool nowait)
{
	struct device *f_dev;
	struct firmware_priv *fw_priv;
	struct firmware *firmware;
	int retval;

	if (!firmware_p)
		return -EINVAL;

	*firmware_p = firmware = kzalloc(sizeof(*firmware), GFP_KERNEL);
	if (!firmware) {
		dev_err(device, "%s: kmalloc(struct firmware) failed\n",
			__func__);
		retval = -ENOMEM;
		goto out;
	}

	if (fw_get_builtin_firmware(firmware, name)) {
		dev_dbg(device, "firmware: using built-in firmware %s\n", name);
		return 0;
	}

	if (uevent)
		dev_dbg(device, "firmware: requesting %s\n", name);

	retval = fw_setup_device(firmware, &f_dev, name, device,
				 uevent, nowait);
	if (retval)
		goto error_kfree_fw;

	fw_priv = dev_get_drvdata(f_dev);

	if (uevent) {
		if (loading_timeout > 0) {
			fw_priv->timeout.expires = jiffies + loading_timeout * HZ;
			add_timer(&fw_priv->timeout);
		}

		kobject_uevent(&f_dev->kobj, KOBJ_ADD);
		wait_for_completion(&fw_priv->completion);
		set_bit(FW_STATUS_DONE, &fw_priv->status);
		del_timer_sync(&fw_priv->timeout);
	} else
		wait_for_completion(&fw_priv->completion);

	mutex_lock(&fw_lock);
	if (!fw_priv->fw->size || test_bit(FW_STATUS_ABORT, &fw_priv->status)) {
		retval = -ENOENT;
		release_firmware(fw_priv->fw);
		*firmware_p = NULL;
	}
	fw_priv->fw = NULL;
	mutex_unlock(&fw_lock);
	device_unregister(f_dev);
	goto out;

error_kfree_fw:
	kfree(firmware);
	*firmware_p = NULL;
out:
	return retval;
}

int
request_firmware(const struct firmware **firmware_p, const char *name,
                 struct device *device)
{
        int uevent = 1;
        return _request_firmware(firmware_p, name, device, uevent, false);
}

void release_firmware(const struct firmware *fw)
{
	if (fw) {
		if (!fw_is_builtin_firmware(fw))
			firmware_free_data(fw);
		kfree(fw);
	}
}

/* Async support */
struct firmware_work {
	struct work_struct work;
	struct module *module;
	const char *name;
	struct device *device;
	void *context;
	void (*cont)(const struct firmware *fw, void *context);
	int uevent;
};

static int
request_firmware_work_func(void *arg)
{
	struct firmware_work *fw_work = arg;
	const struct firmware *fw;
	int ret;
	if (!arg) {
		WARN_ON(1);
		return 0;
	}
	ret = _request_firmware(&fw, fw_work->name, fw_work->device,
		fw_work->uevent, true);

	fw_work->cont(fw, fw_work->context);

	module_put(fw_work->module);
	kfree(fw_work);
	return ret;
}

int
request_firmware_nowait(
	struct module *module, int uevent,
	const char *name, struct device *device, gfp_t gfp, void *context,
	void (*cont)(const struct firmware *fw, void *context))
{
	struct task_struct *task;
	struct firmware_work *fw_work = kmalloc(sizeof (struct firmware_work),
						gfp);

	if (!fw_work)
		return -ENOMEM;
	if (!try_module_get(module)) {
		kfree(fw_work);
		return -EFAULT;
	}

	*fw_work = (struct firmware_work) {
		.module = module,
		.name = name,
		.device = device,
		.context = context,
		.cont = cont,
		.uevent = uevent,
	};

	task = kthread_run(request_firmware_work_func, fw_work,
			    "firmware/%s", name);

	if (IS_ERR(task)) {
		fw_work->cont(NULL, fw_work->context);
		module_put(fw_work->module);
		kfree(fw_work);
		return PTR_ERR(task);
	}
	return 0;
}

static int __init firmware_class_init(void)
{
	return class_register(&firmware_class);
}

static void __exit firmware_class_exit(void)
{
	class_unregister(&firmware_class);
}

fs_initcall(firmware_class_init);
module_exit(firmware_class_exit);

EXPORT_SYMBOL(release_firmware);
EXPORT_SYMBOL(request_firmware);
EXPORT_SYMBOL(request_firmware_nowait);
