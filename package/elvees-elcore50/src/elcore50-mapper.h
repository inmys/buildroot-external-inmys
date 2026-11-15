/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2020 RnD Center "ELVEES", JSC
 */
#ifndef _LINUX_ELCORE50_MAPPER_H
#define _LINUX_ELCORE50_MAPPER_H

#include <linux/version.h>

#include "elcore50-dmabuf.h"

/*
 * fd_file() was introduced in v6.12 to hide the internals of struct fd;
 * before that the file pointer was accessed directly as fd.file.
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 12, 0)
#define fd_file(f) ((f).file)
#endif

/**
 * struct userptr_mapper
 * @size: Actual buffer size
 * @size_aligned: Combined size of all 4K pages containing buffer
 * @offset: Offset from 4K boundary to actual buffer start address
 */
struct userptr_mapper {
	struct dma_buf_attachment *attach;
	struct sg_table *sgt;
	struct dma_buf *dmabuf;
	struct device *dev;
	void *vaddr;
	size_t size;
	size_t size_aligned;
	unsigned int offset;

	int is_argument;
};

int sync_buffer(size_t size, size_t buf_offset, struct userptr_mapper *mapper,
		enum elcore50_buf_sync_dir dir);
int elcore50_create_mapper(struct device *dev, void __user *arg);
int elcore50_sync_buffer(struct file *file, void __user *arg);

#endif
