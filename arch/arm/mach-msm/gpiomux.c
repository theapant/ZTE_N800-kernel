/* Copyright (c) 2010, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <mach/gpiomux.h>
//jiangfeng
#include <linux/debugfs.h>
#include <linux/seq_file.h>
//jiangfeng, end

struct msm_gpiomux_rec {
	struct gpiomux_setting *sets[GPIOMUX_NSETTINGS];
	int ref;
};
static DEFINE_SPINLOCK(gpiomux_lock);
static struct msm_gpiomux_rec *msm_gpiomux_recs;
static struct gpiomux_setting *msm_gpiomux_sets;
static unsigned msm_gpiomux_ngpio;

int msm_gpiomux_write(unsigned gpio, enum msm_gpiomux_setting which,
	struct gpiomux_setting *setting, struct gpiomux_setting *old_setting)
{
	struct msm_gpiomux_rec *rec = msm_gpiomux_recs + gpio;
	unsigned set_slot = gpio * GPIOMUX_NSETTINGS + which;
	unsigned long irq_flags;
	struct gpiomux_setting *new_set;
	int status = 0;

	if (!msm_gpiomux_recs)
		return -EFAULT;

	if (gpio >= msm_gpiomux_ngpio)
		return -EINVAL;

	spin_lock_irqsave(&gpiomux_lock, irq_flags);

	if (old_setting) {
		if (rec->sets[which] == NULL)
			status = 1;
		else
			*old_setting =  *(rec->sets[which]);
	}

	if (setting) {
		msm_gpiomux_sets[set_slot] = *setting;
		rec->sets[which] = &msm_gpiomux_sets[set_slot];
	} else {
		rec->sets[which] = NULL;
	}

	new_set = rec->ref ? rec->sets[GPIOMUX_ACTIVE] :
		rec->sets[GPIOMUX_SUSPENDED];
	if (new_set)
		__msm_gpiomux_write(gpio, *new_set);

	spin_unlock_irqrestore(&gpiomux_lock, irq_flags);
	return status;
}
EXPORT_SYMBOL(msm_gpiomux_write);

int msm_gpiomux_get(unsigned gpio)
{
	struct msm_gpiomux_rec *rec = msm_gpiomux_recs + gpio;
	unsigned long irq_flags;

	if (!msm_gpiomux_recs)
		return -EFAULT;

	if (gpio >= msm_gpiomux_ngpio)
		return -EINVAL;

	spin_lock_irqsave(&gpiomux_lock, irq_flags);
	if (rec->ref++ == 0 && rec->sets[GPIOMUX_ACTIVE])
		__msm_gpiomux_write(gpio, *rec->sets[GPIOMUX_ACTIVE]);
	spin_unlock_irqrestore(&gpiomux_lock, irq_flags);
	return 0;
}
EXPORT_SYMBOL(msm_gpiomux_get);

int msm_gpiomux_put(unsigned gpio)
{
	struct msm_gpiomux_rec *rec = msm_gpiomux_recs + gpio;
	unsigned long irq_flags;

	if (!msm_gpiomux_recs)
		return -EFAULT;

	if (gpio >= msm_gpiomux_ngpio)
		return -EINVAL;

	spin_lock_irqsave(&gpiomux_lock, irq_flags);
	BUG_ON(rec->ref == 0);
	if (--rec->ref == 0 && rec->sets[GPIOMUX_SUSPENDED])
		__msm_gpiomux_write(gpio, *rec->sets[GPIOMUX_SUSPENDED]);
	spin_unlock_irqrestore(&gpiomux_lock, irq_flags);
	return 0;
}
EXPORT_SYMBOL(msm_gpiomux_put);

#if defined(CONFIG_DEBUG_FS)

void msm_gpiomux_dump(void)
{
	unsigned gpio;
	pr_info("%s: dump suspended info\n",__func__);
	for (gpio=0; gpio<msm_gpiomux_ngpio; gpio++) {
		struct msm_gpiomux_rec *rec = msm_gpiomux_recs + gpio;
		if (rec->sets[GPIOMUX_SUSPENDED]==NULL)
			pr_info("[%3d] null\n",gpio);
		else
			pr_info("[%3d] func:%d dir=%d drv=%d pull=%d\n",
					gpio,(unsigned)rec->sets[GPIOMUX_SUSPENDED]->func,
						 (unsigned)rec->sets[GPIOMUX_SUSPENDED]->dir,
						 (unsigned)rec->sets[GPIOMUX_SUSPENDED]->drv,
						 (unsigned)rec->sets[GPIOMUX_SUSPENDED]->pull);
	}
	pr_info("%s: dump active info\n",__func__);
	for (gpio=0; gpio<msm_gpiomux_ngpio; gpio++) {
		struct msm_gpiomux_rec *rec = msm_gpiomux_recs + gpio;
		if (rec->sets[GPIOMUX_ACTIVE]==NULL)
			pr_info("[%3d] null\n",gpio);
		else
			pr_info("[%3d] func:%d dir=%d drv=%d pull=%d\n",
					gpio,(unsigned)rec->sets[GPIOMUX_ACTIVE]->func,
						 (unsigned)rec->sets[GPIOMUX_ACTIVE]->dir,
						 (unsigned)rec->sets[GPIOMUX_ACTIVE]->drv,
						 (unsigned)rec->sets[GPIOMUX_ACTIVE]->pull);
	}
	
}

static int list_gpiomux_show(struct seq_file *m, void *unused)
{
	msm_gpiomux_dump();

	return 0;
}


static int list_gpiomux_open(struct inode *inode, struct file *file)
{
	return single_open(file, list_gpiomux_show, inode->i_private);
}


static const struct file_operations list_gpiomux_fops = {
	.open		= list_gpiomux_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};


int gpiomux_debug_init(void)
{
	int err = 0;	

	if (!debugfs_create_file("gpiomux_info", S_IRUGO, NULL,
				NULL, &list_gpiomux_fops))
		return -ENOMEM;
	
	return err;
}

#else
void gpiomux_debug_init(void) {}

#endif

int msm_gpiomux_init(size_t ngpio)
{
	if (!ngpio)
		return -EINVAL;

	if (msm_gpiomux_recs)
		return -EPERM;

	msm_gpiomux_recs = kzalloc(sizeof(struct msm_gpiomux_rec) * ngpio,
				   GFP_KERNEL);
	if (!msm_gpiomux_recs)
		return -ENOMEM;

	/* There is no need to zero this memory, as clients will be blindly
	 * installing settings on top of it.
	 */
	msm_gpiomux_sets = kmalloc(sizeof(struct gpiomux_setting) * ngpio *
		GPIOMUX_NSETTINGS, GFP_KERNEL);
	if (!msm_gpiomux_sets) {
		kfree(msm_gpiomux_recs);
		msm_gpiomux_recs = NULL;
		return -ENOMEM;
	}

	msm_gpiomux_ngpio = ngpio;

//jiangfeng
	gpiomux_debug_init();
//jiangfeng, end

	return 0;
}
EXPORT_SYMBOL(msm_gpiomux_init);

void msm_gpiomux_install(struct msm_gpiomux_config *configs, unsigned nconfigs)
{
	unsigned c, s;
	int rc;

	for (c = 0; c < nconfigs; ++c) {
		for (s = 0; s < GPIOMUX_NSETTINGS; ++s) {
			rc = msm_gpiomux_write(configs[c].gpio, s,
				configs[c].settings[s], NULL);
			if (rc)
				pr_err("%s: write failure: %d\n", __func__, rc);
		}
	}
}
EXPORT_SYMBOL(msm_gpiomux_install);
