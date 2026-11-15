// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2020 RnD Center "ELVEES", JSC
 */

#include <linux/anon_inodes.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "elcore50-trace.h"
#include "elcore50-mapper.h"

static int elcore50_mapper_release(struct inode *inode, struct file *file)
{
	struct userptr_mapper *mapper = file->private_data;

	if (mapper->attach) {
		if (mapper->vaddr) {
			struct iosys_map map =
				IOSYS_MAP_INIT_VADDR(mapper->vaddr);
			dma_buf_vunmap_unlocked(mapper->attach->dmabuf, &map);
			mapper->vaddr = NULL;
		}

		if (mapper->sgt) {
			dma_buf_unmap_attachment_unlocked(
				mapper->attach, mapper->sgt, DMA_BIDIRECTIONAL);
			mapper->sgt = NULL;
		}

		dma_buf_detach(mapper->dmabuf, mapper->attach);
		dma_buf_put(mapper->dmabuf);

		mapper->attach = NULL;
		mapper->dmabuf = NULL;
	}

	kfree(mapper);
	return 0;
}

const struct file_operations elcore50_mapper_ops = {
	.release = elcore50_mapper_release,
	.owner = THIS_MODULE,
};

int sync_buffer(size_t size, size_t buf_offset, struct userptr_mapper *mapper,
		enum elcore50_buf_sync_dir dir)
{
	size_t first_sgl = 0;
	size_t nents = 0;
	struct sg_table *sgt;
	struct scatterlist *sg_first, *sg_last;
	size_t remain_length = 0, first_sgl_length = 0;
	size_t offset = 0, offset_next = 0;
	dma_addr_t first_sgl_addr = 0;

	if (size == 0) {
		WARN_ON(1);
		return 0;
	}

	if (buf_offset + size > mapper->size) {
		WARN_ON(1);
		return -EINVAL;
	}

	sgt = mapper->sgt;
	for_each_sg(sgt->sgl, sg_last, sgt->orig_nents, nents) {
		offset_next = offset + sg_dma_len(sg_last);
		if (offset <= buf_offset && buf_offset < offset_next) {
			first_sgl = nents;
			sg_first = sg_last;
			first_sgl_addr =
				sg_first->dma_address + buf_offset - offset;
			if (buf_offset + size <= offset_next)
				first_sgl_length = size;
			else
				first_sgl_length = offset_next - buf_offset;
		}
		if (buf_offset + size <= offset_next) {
			nents -= first_sgl;
			remain_length = buf_offset + size - offset;
			WARN_ON(remain_length == 0);
			break;
		}
		offset = offset_next;
	}

	switch (dir) {
	case ELCORE50_BUF_SYNC_DIR_TO_CPU:
		// Sync the first sgl
		dma_sync_single_for_cpu(mapper->dev, first_sgl_addr,
					first_sgl_length, DMA_FROM_DEVICE);
		if (nents == 0)
			break;
		// Sync the last sgl
		dma_sync_single_for_cpu(mapper->dev, sg_last->dma_address,
					remain_length, DMA_FROM_DEVICE);
		if (nents == 1)
			break;
		// Sync the intermediate sgls
		dma_sync_sg_for_cpu(mapper->dev, sg_next(sg_first), nents - 1,
				    DMA_FROM_DEVICE);
		break;
	case ELCORE50_BUF_SYNC_DIR_TO_DEVICE:
		// Sync the first sgl
		dma_sync_single_for_device(mapper->dev, first_sgl_addr,
					   first_sgl_length, DMA_TO_DEVICE);
		if (nents == 0)
			break;
		// Sync the last sgl
		dma_sync_single_for_device(mapper->dev, sg_last->dma_address,
					   remain_length, DMA_TO_DEVICE);
		if (nents == 1)
			break;
		// Sync the intermediate sgls
		dma_sync_sg_for_device(mapper->dev, sg_next(sg_first),
				       nents - 1, DMA_TO_DEVICE);
		break;
	case ELCORE50_BUF_SYNC_BIDIRECTIONAL:
		// Sync the first sgl
		dma_sync_single_for_device(mapper->dev, first_sgl_addr,
					   first_sgl_length, DMA_BIDIRECTIONAL);
		if (nents == 0)
			break;
		// Sync the last sgl
		dma_sync_single_for_device(mapper->dev, sg_last->dma_address,
					   remain_length, DMA_BIDIRECTIONAL);
		if (nents == 1)
			break;
		// Sync the intermediate sgls
		dma_sync_sg_for_device(mapper->dev, sg_next(sg_first),
				       nents - 1, DMA_BIDIRECTIONAL);
		break;
	default:
		WARN_ON(1);
		break;
	}
	return 0;
}

int elcore50_create_mapper(struct device *dev, void __user *arg)
{
	struct elcore50_buf buf_info;
	struct dma_buf *dmabuf;
	struct userptr_mapper *mapper;
	int ret;

	ret = copy_from_user(&buf_info, arg, sizeof(struct elcore50_buf));
	if (ret)
		return -EACCES;

	mapper = kzalloc(sizeof(struct userptr_mapper), GFP_KERNEL);
	if (!mapper)
		return -ENOMEM;

	dmabuf = dma_buf_get(buf_info.dmabuf_fd);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		goto mapper_free;
	}
	mapper->dmabuf = dmabuf;
	mapper->vaddr = NULL;
	mapper->dev = dev;

	mapper->attach = dma_buf_attach(dmabuf, dev);
	if (IS_ERR(mapper->attach)) {
		ret = PTR_ERR(mapper->attach);
		goto dmabuf_put;
	}

	// Cache flushing inside this function, since ops->map_dma_buf() will
	// be called
	mapper->sgt = dma_buf_map_attachment_unlocked(mapper->attach,
						      DMA_BIDIRECTIONAL);

	if (IS_ERR(mapper->sgt)) {
		ret = PTR_ERR(mapper->sgt);
		goto dmabuf_detach;
	}

	mapper->size = dmabuf->size;
	mapper->offset = mapper->sgt->sgl[0].offset & E50_INPAGE_MASK;
	mapper->size_aligned =
		round_up(mapper->offset + mapper->size, E50_PAGE_SIZE);

	ret = anon_inode_getfd("elcore50_buf_ops", &elcore50_mapper_ops, mapper,
			       0);
	if (ret < 0)
		goto dmabuf_unmap_attachment;
	/* Overriding dmabuf fd to new fd */
	buf_info.mapper_fd = ret;

	ret = copy_to_user(arg, &buf_info, sizeof(struct elcore50_buf));
	if (ret) {
		ret = -EACCES;
		goto dmabuf_put_fd;
	}

	return 0;
dmabuf_put_fd:
	put_unused_fd(buf_info.mapper_fd);
dmabuf_unmap_attachment:
	dma_buf_unmap_attachment_unlocked(mapper->attach, mapper->sgt,
					  DMA_BIDIRECTIONAL);
	mapper->sgt = NULL;
dmabuf_detach:
	dma_buf_detach(dmabuf, mapper->attach);
	mapper->attach = NULL;
dmabuf_put:
	dma_buf_put(dmabuf);
mapper_free:
	kfree(mapper);
	return ret;
}

int elcore50_sync_buffer(struct file *file, void __user *arg)
{
	struct elcore50_buf_sync buf_sync;
	struct fd fd;
	int ret;

	ret = copy_from_user(&buf_sync, arg, sizeof(struct elcore50_buf_sync));
	if (ret)
		return -EACCES;

	fd = fdget(buf_sync.mapper_fd);
	if (!fd_file(fd) || fd_file(fd)->f_op != &elcore50_mapper_ops) {
		fdput(fd);
		return -EINVAL;
	}

	if (buf_sync.size)
		ret = sync_buffer(buf_sync.size, buf_sync.offset,
				  fd_file(fd)->private_data, buf_sync.dir);

	fdput(fd);
	return ret;
}
