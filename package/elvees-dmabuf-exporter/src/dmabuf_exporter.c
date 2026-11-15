// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2022 RnD Center "ELVEES", JSC
 */

#include <linux/dma-buf.h>
#include <linux/highmem.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/vmalloc.h>

#include "dmabuf_exporter.h"

struct dmaexp_alloc {
	unsigned int num_pages;
	struct page **pages;
};

struct dmaexp_file {
	struct dmaexp_alloc *alloc;
	struct dma_buf *dmabuf;
};

static struct miscdevice dmaexp_dev;

static int dmaexp_attach(struct dma_buf *dmabuf,
			 struct dma_buf_attachment *attach)
{
	return 0;
}

static void dmaexp_detach(struct dma_buf *dmabuf,
			  struct dma_buf_attachment *attach)
{
}

static struct sg_table *dmaexp_map_dma_buf(struct dma_buf_attachment *attach,
					   enum dma_data_direction dir)
{
	struct dmaexp_file *file_priv = attach->dmabuf->priv;
	struct dmaexp_alloc *alloc = file_priv->alloc;
	unsigned int i;
	int err;

	struct sg_table *sgt;
	struct scatterlist *iter;

	sgt = kmalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);

	err = sg_alloc_table(sgt, alloc->num_pages, GFP_KERNEL);
	if (err) {
		kfree(sgt);
		return ERR_PTR(err);
	}

	for_each_sg(sgt->sgl, iter, alloc->num_pages, i) {
		sg_set_page(iter, alloc->pages[i], PAGE_SIZE, 0);
	}

	if (!dma_map_sg(attach->dev, sgt->sgl, sgt->nents, dir)) {
		dev_err(dmaexp_dev.this_device, "%s: failed to map dmabuf\n",
			__func__);
		sg_free_table(sgt);
		kfree(sgt);
		return ERR_PTR(-ENOMEM);
	}

	return sgt;
}

static void dmaexp_unmap_dma_buf(struct dma_buf_attachment *attach,
				 struct sg_table *sgt,
				 enum dma_data_direction dir)
{
	dma_unmap_sg(attach->dev, sgt->sgl, sgt->nents, dir);
	sg_free_table(sgt);
	kfree(sgt);
}

static void dmaexp_release(struct dma_buf *dmabuf)
{
	struct dmaexp_file *file_priv = dmabuf->priv;
	struct dmaexp_alloc *alloc = file_priv->alloc;
	unsigned int i;

	for (i = 0; i < alloc->num_pages; i++)
		__free_page(alloc->pages[i]);
	kfree(alloc->pages);
	kfree(alloc);
	kfree(file_priv);
}

static int dmaexp_begin_cpu_access(struct dma_buf *dmabuf,
				   enum dma_data_direction dir)
{
	struct dmaexp_file *file_priv = dmabuf->priv;
	struct dmaexp_alloc *alloc = file_priv->alloc;
	unsigned int i;

	for (i = 0; i < alloc->num_pages; i++) {
		dma_sync_single_for_cpu(dmaexp_dev.this_device,
					page_to_phys(alloc->pages[i]),
					PAGE_SIZE, dir);
	}
	return 0;
}

static int dmaexp_end_cpu_access(struct dma_buf *dmabuf,
				 enum dma_data_direction dir)
{
	struct dmaexp_file *file_priv = dmabuf->priv;
	struct dmaexp_alloc *alloc = file_priv->alloc;
	unsigned int i;

	for (i = 0; i < alloc->num_pages; i++) {
		dma_sync_single_for_device(dmaexp_dev.this_device,
					   page_to_phys(alloc->pages[i]),
					   PAGE_SIZE, dir);
	}
	return 0;
}

static int dmaexp_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct dmaexp_file *file_priv = dmabuf->priv;
	struct dmaexp_alloc *alloc = file_priv->alloc;
	unsigned long addr = vma->vm_start;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned int i, n = size >> PAGE_SHIFT;

	if (!alloc) {
		dev_err(dmaexp_dev.this_device, "%s: no memory to map\n",
			__func__);
		return -EINVAL;
	}

	for (i = 0; i < n; i++) {
		int ret = remap_pfn_range(vma, addr,
					  page_to_pfn(alloc->pages[i]),
					  PAGE_SIZE, vma->vm_page_prot);
		if (ret) {
			dev_err(dmaexp_dev.this_device,
				"%s: failed to remap dmabuf: %d\n", __func__,
				ret);
			return ret;
		}
		addr += PAGE_SIZE;
	}

	return 0;
}

static int dmaexp_vmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct dmaexp_file *file_priv = dmabuf->priv;
	struct dmaexp_alloc *alloc = file_priv->alloc;
	void *vaddr = vmap(alloc->pages, alloc->num_pages, VM_MAP, PAGE_KERNEL);

	if (!vaddr)
		return -ENOMEM;

	iosys_map_set_vaddr(map, vaddr);

	return 0;
}

static void dmaexp_vunmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	vunmap(map->vaddr);
}

static const struct dma_buf_ops dmaexp_ops = {
	.attach = dmaexp_attach,
	.detach = dmaexp_detach,
	.map_dma_buf = dmaexp_map_dma_buf,
	.unmap_dma_buf = dmaexp_unmap_dma_buf,
	.release = dmaexp_release,
	.begin_cpu_access = dmaexp_begin_cpu_access,
	.end_cpu_access = dmaexp_end_cpu_access,
	.mmap = dmaexp_mmap,
	.vmap = dmaexp_vmap,
	.vunmap = dmaexp_vunmap
};

static int dmaexp_ioctl_create(struct dmaexp_file *file_priv,
			       unsigned long size)
{
	int ret;
	unsigned int i;
	struct dmaexp_alloc *alloc;
	struct dma_buf *dmabuf;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	if (size == 0)
		return -EINVAL;

	alloc = kzalloc(sizeof(*alloc), GFP_KERNEL);
	if (!alloc)
		return -ENOMEM;

	alloc->num_pages = DIV_ROUND_UP(size, PAGE_SIZE);
	alloc->pages = kcalloc(alloc->num_pages, sizeof(struct page *),
			       GFP_KERNEL);
	if (!alloc->pages) {
		kfree(alloc);
		return -ENOMEM;
	}
	for (i = 0; i < alloc->num_pages; i++) {
		alloc->pages[i] = alloc_page(GFP_KERNEL);
		if (!alloc->pages[i]) {
			dev_err(dmaexp_dev.this_device,
				"%s: failed to alloc page structure",
				__func__);
			ret = -ENOMEM;
			goto err_free_pages;
		}
	}

	exp_info.ops = &dmaexp_ops;
	exp_info.size = alloc->num_pages << PAGE_SHIFT;
	exp_info.flags = O_RDWR;
	exp_info.priv = file_priv;

	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		dev_err(dmaexp_dev.this_device, "%s: failed to export dmabuf",
			__func__);
		ret = PTR_ERR(dmabuf);
		goto err_free_pages;
	}

	file_priv->alloc = alloc;
	file_priv->dmabuf = dmabuf;

	return 0;

err_free_pages:
	for (i = 0; i < alloc->num_pages; i++) {
		if (!alloc->pages[i])
			break;
		__free_page(alloc->pages[i]);
	}
	kfree(alloc->pages);
	kfree(alloc);

	return ret;
}

static int dmaexp_ioctl_export(struct dmaexp_file *file_priv,
			       unsigned long flags)
{
	int fd;

	if (!file_priv->dmabuf)
		return -EINVAL;

	get_dma_buf(file_priv->dmabuf);
	fd = dma_buf_fd(file_priv->dmabuf, flags);
	if (fd < 0)
		dma_buf_put(file_priv->dmabuf);

	return fd;
}

static int dmaexp_fops_open(struct inode *inode, struct file *file)
{
	struct dmaexp_file *file_priv;

	file_priv = kzalloc(sizeof(*file_priv), GFP_KERNEL);
	if (!file_priv)
		return -ENOMEM;

	file->private_data = file_priv;

	return 0;
}

static int dmaexp_fops_release(struct inode *inode, struct file *file)
{
	struct dmaexp_file *file_priv = file->private_data;

	if (file_priv->dmabuf)
		dma_buf_put(file_priv->dmabuf);
	else
		kfree(file_priv);

	return 0;
}

static long dmaexp_fops_ioctl(struct file *file, unsigned int cmd,
			      unsigned long arg)
{
	struct dmaexp_file *file_priv = file->private_data;
	long ret = 0;

	switch (cmd) {
	case DMABUF_IOCTL_CREATE:
		ret = dmaexp_ioctl_create(file_priv, arg);
		break;

	case DMABUF_IOCTL_EXPORT:
		ret = dmaexp_ioctl_export(file_priv, arg);
		break;

	default:
		ret = -ENOTTY;
		break;
	}
	return ret;
}

static const struct file_operations dmaexp_fops = {
	.owner = THIS_MODULE,
	.open = dmaexp_fops_open,
	.release = dmaexp_fops_release,
	.unlocked_ioctl = dmaexp_fops_ioctl
};

static int __init dmaexp_init(void)
{
	int res;

	dmaexp_dev.minor = MISC_DYNAMIC_MINOR;
	dmaexp_dev.name = "dmabuf";
	dmaexp_dev.fops = &dmaexp_fops;
	res = misc_register(&dmaexp_dev);
	if (res) {
		pr_err("Failed to register %s device\n", dmaexp_dev.name);
		return res;
	}
	dev_info(dmaexp_dev.this_device, "/dev/%s ready\n", dmaexp_dev.name);

	return 0;
}

static void __exit dmaexp_exit(void)
{
	misc_deregister(&dmaexp_dev);
}

MODULE_DESCRIPTION("DMA buffer exporter");
MODULE_AUTHOR("RnD Center ELVEES, JSC");
MODULE_LICENSE("GPL");

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 13, 0)
MODULE_IMPORT_NS("DMA_BUF");
#else
MODULE_IMPORT_NS(DMA_BUF);
#endif

module_init(dmaexp_init);
module_exit(dmaexp_exit);
