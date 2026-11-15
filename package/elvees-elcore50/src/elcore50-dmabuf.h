/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2020 RnD Center "ELVEES", JSC
 */
#ifndef _LINUX_ELCORE50_DMABUF_H
#define _LINUX_ELCORE50_DMABUF_H

#include <linux/dma-buf.h>
#include <linux/scatterlist.h>
#include <linux/version.h>

#include "elcore50-core.h"

/* Structure is copied from include/media/frame_vector.h */

/* Container for pinned pfns / pages in frame_vector.c */
struct elcore50_frame_vector {
	unsigned int nr_allocated; /* Number of frames we have space for */
	unsigned int nr_frames; /* Number of frames stored in ptrs array */
	bool got_ref; /* Did we pin pages by getting page ref? */
	bool is_pfns; /* Does array contain pages or pfns? */
	void *ptrs[]; /* Array of pinned pfns / pages. Use
				 * pfns_vector_pages() or pfns_vector_pfns()
				 * for access */
};

struct elcore50_buffer_priv {
	/* common part */
	struct elcore50_buf buf_info;
	struct dma_buf dmabuf;
	struct device *dev;
	void *vaddr;

	/* noncached part */
	dma_addr_t paddr;

	/* cached part */
	unsigned int num_pages;
	struct page **pages;
	struct elcore50_frame_vector *vec;
	struct sg_table sgt;
	size_t size;
};

int elcore50_create_buffer(struct device *dev, void __user *arg);
void *dma_buf_vmap_compat(struct dma_buf *buf);

#endif
