// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2020 RnD Center "ELVEES", JSC
 */

#include <linux/file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/vmalloc.h>

#include "elcore50-dmabuf.h"
#include "elcore50-trace.h"

/* MODULE_IMPORT_NS() switched from a bare token to a string literal in v6.13. */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 13, 0)
MODULE_IMPORT_NS(DMA_BUF);
#else
MODULE_IMPORT_NS("DMA_BUF");
#endif

/* Both frame vector manipulation functions are copied from videobuf2-memops.
 * The reason they are copied is that we didn't want to introduce a dependency
 * on v4l2 and videobuf2
 */

static struct elcore50_frame_vector *
elcore50_frame_vector_create(unsigned int nr_frames)
{
	struct elcore50_frame_vector *vec;
	int size = sizeof(struct elcore50_frame_vector) +
		   sizeof(void *) * nr_frames;

	if (WARN_ON_ONCE(nr_frames == 0))
		return NULL;
	/*
	 * This is absurdly high. It's here just to avoid strange effects when
	 * arithmetics overflows.
	 */
	if (WARN_ON_ONCE(nr_frames > INT_MAX / sizeof(void *) / 2))
		return NULL;
	/*
	 * Avoid higher order allocations, use vmalloc instead. It should
	 * be rare anyway.
	 */
	vec = kvmalloc(size, GFP_KERNEL);
	if (!vec)
		return NULL;
	vec->nr_allocated = nr_frames;
	vec->nr_frames = 0;
	return vec;
}

/*
 * All functions are copied from drivers/media/common/videobuf2/frame_vector.c
 * and include/media/frame_vector.h.
 * The reason they are copied is that we didn't want to introduce a dependency on USB subsystem.
 */

static int elcore50_get_vaddr_frames(unsigned long start,
				     unsigned int nr_frames, bool write,
				     struct elcore50_frame_vector *vec)
{
	int ret;
	unsigned int gup_flags = FOLL_LONGTERM;

	if (nr_frames == 0)
		return 0;

	if (WARN_ON_ONCE(nr_frames > vec->nr_allocated))
		nr_frames = vec->nr_allocated;

	start = untagged_addr(start);

	if (write)
		gup_flags |= FOLL_WRITE;

	ret = pin_user_pages_fast(start, nr_frames, gup_flags,
				  (struct page **)(vec->ptrs));
	vec->got_ref = true;
	vec->is_pfns = false;
	vec->nr_frames = ret;

	if (likely(ret > 0))
		return ret;

	vec->nr_frames = 0;
	return ret ? ret : -EFAULT;
}

/**
 * elcore50_frame_vector_to_pfns - convert frame vector to contain pfns
 * @vec:	frame vector to convert
 *
 * Convert @vec to contain array of pfns.
 */
static void elcore50_frame_vector_to_pfns(struct elcore50_frame_vector *vec)
{
	int i;
	unsigned long *nums;
	struct page **pages;

	if (vec->is_pfns)
		return;
	pages = (struct page **)(vec->ptrs);
	nums = (unsigned long *)pages;
	for (i = 0; i < vec->nr_frames; i++)
		nums[i] = page_to_pfn(pages[i]);
	vec->is_pfns = true;
}

static inline unsigned long *
elcore50_frame_vector_pfns(struct elcore50_frame_vector *vec)
{
	if (!vec->is_pfns)
		elcore50_frame_vector_to_pfns(vec);
	return (unsigned long *)(vec->ptrs);
}

static int elcore50_frame_vector_to_pages(struct elcore50_frame_vector *vec)
{
	int i;
	unsigned long *nums;
	struct page **pages;

	if (!vec->is_pfns)
		return 0;
	nums = elcore50_frame_vector_pfns(vec);
	for (i = 0; i < vec->nr_frames; i++)
		if (!pfn_valid(nums[i]))
			return -EINVAL;
	pages = (struct page **)nums;
	for (i = 0; i < vec->nr_frames; i++)
		pages[i] = pfn_to_page(nums[i]);
	vec->is_pfns = false;
	return 0;
}

static inline struct page **
elcore50_frame_vector_pages(struct elcore50_frame_vector *vec)
{
	if (vec->is_pfns) {
		int err = elcore50_frame_vector_to_pages(vec);

		if (err)
			return ERR_PTR(err);
	}
	return (struct page **)(vec->ptrs);
}

static void elcore50_put_vaddr_frames(struct elcore50_frame_vector *vec)
{
	struct page **pages;

	if (!vec->got_ref)
		goto out;
	pages = elcore50_frame_vector_pages(vec);
	/*
	 * elcore50_frame_vector_pages() might needed to do a conversion when
	 * elcore50_get_vaddr_frames() got pages but vec was later converted to pfns.
	 * But it shouldn't really fail to convert pfns back...
	 */
	if (WARN_ON(IS_ERR(pages)))
		goto out;

	unpin_user_pages(pages, vec->nr_frames);
	vec->got_ref = false;
out:
	vec->nr_frames = 0;
}

static void elcore50_frame_vector_destroy(struct elcore50_frame_vector *vec)
{
	/* Make sure elcore50_put_vaddr_frames() got called properly... */
	VM_BUG_ON(vec->nr_frames > 0);
	kvfree(vec);
}

static struct elcore50_frame_vector *
elcore50_create_framevec(unsigned long start, unsigned long length, bool write)
{
	int ret;
	unsigned long first, last;
	unsigned long nr;
	struct elcore50_frame_vector *vec;

	first = start >> PAGE_SHIFT;
	last = (start + length - 1) >> PAGE_SHIFT;
	nr = last - first + 1;
	vec = elcore50_frame_vector_create(nr);
	if (!vec)
		return ERR_PTR(-ENOMEM);

	ret = elcore50_get_vaddr_frames(start & PAGE_MASK, nr, write, vec);
	if (ret < 0)
		goto out_destroy;
	/* We accept only complete set of PFNs */
	if (ret != nr) {
		ret = -EFAULT;
		goto out_release;
	}
	return vec;
out_release:
	elcore50_put_vaddr_frames(vec);
out_destroy:
	elcore50_frame_vector_destroy(vec);
	return ERR_PTR(ret);
}

static void elcore50_destroy_framevec(struct elcore50_frame_vector *vec)
{
	elcore50_put_vaddr_frames(vec);
	elcore50_frame_vector_destroy(vec);
}

static int elcore50_allocate_coherent_memory(struct elcore50_buffer_priv *priv)
{
	int rc;

	priv->buf_info.size = ALIGN(priv->buf_info.size, PAGE_SIZE);
	priv->vaddr = dma_alloc_coherent(priv->dev, priv->buf_info.size,
					 &priv->paddr, GFP_KERNEL);
	if (!priv->vaddr)
		return -ENOMEM;

	rc = sg_alloc_table(&priv->sgt, 1, GFP_KERNEL);
	if (rc)
		goto free_coherent;
	priv->sgt.sgl[0].length = priv->buf_info.size;
	sg_dma_len(priv->sgt.sgl) = priv->buf_info.size;
	priv->sgt.sgl[0].dma_address = priv->paddr;

	return 0;
free_coherent:
	dma_free_coherent(priv->dev, priv->buf_info.size, priv->vaddr,
			  priv->paddr);
	return rc;
}

static inline unsigned int
elcore50_frame_vector_count(struct elcore50_frame_vector *vec)
{
	return vec->nr_frames;
}

static int elcore50_get_sg_from_uptr(struct elcore50_buffer_priv *priv)
{
	int ret;

	priv->size = priv->buf_info.size;
	priv->vec = elcore50_create_framevec(priv->buf_info.p, priv->size, 1);
	if (IS_ERR(priv->vec))
		return PTR_ERR(priv->vec);

	priv->pages = elcore50_frame_vector_pages(priv->vec);
	if (IS_ERR(priv->pages)) {
		ret = PTR_ERR(priv->pages);
		goto err_pfnvec;
	}
	priv->num_pages = elcore50_frame_vector_count(priv->vec);

	ret = sg_alloc_table_from_pages(&priv->sgt, priv->pages,
					priv->num_pages,
					offset_in_page(priv->buf_info.p),
					priv->buf_info.size, GFP_KERNEL);
	if (ret)
		goto err_pfnvec;

	return 0;
err_pfnvec:
	elcore50_destroy_framevec(priv->vec);
	return ret;
}

static void elcore50_put_sg_from_uptr(struct elcore50_buffer_priv *priv)
{
	int i = priv->num_pages;

	sg_free_table(&priv->sgt);
	while (--i >= 0)
		set_page_dirty_lock(priv->pages[i]);
	elcore50_destroy_framevec(priv->vec);
}

static void elcore50_free_coherent_memory(struct elcore50_buffer_priv *priv)
{
	dma_free_coherent(priv->dev, priv->buf_info.size, priv->vaddr,
			  priv->paddr);
	sg_free_table(&priv->sgt);
}

static int elcore50_allocate_buffer(struct elcore50_buffer_priv *priv)
{
	switch (priv->buf_info.type) {
	case ELCORE50_CACHED_BUFFER_FROM_UPTR:
		return elcore50_get_sg_from_uptr(priv);
	case ELCORE50_NONCACHED_BUFFER:
		return elcore50_allocate_coherent_memory(priv);
	default:
		return -EINVAL;
	}
}

static void elcore50_free_buffer(struct elcore50_buffer_priv *priv)
{
	switch (priv->buf_info.type) {
	case ELCORE50_CACHED_BUFFER_FROM_UPTR:
		return elcore50_put_sg_from_uptr(priv);
	case ELCORE50_NONCACHED_BUFFER:
		return elcore50_free_coherent_memory(priv);
	default:
		return;
	}
}

static struct sg_table *elcore50_map_dmabuf(struct dma_buf_attachment *attach,
					    enum dma_data_direction dir)
{
	struct elcore50_buffer_priv *priv = attach->dmabuf->priv;
	int rc;

	switch (priv->buf_info.type) {
	case ELCORE50_CACHED_BUFFER_FROM_UPTR:
		rc = dma_map_sg(attach->dev, priv->sgt.sgl,
				priv->sgt.orig_nents, dir);
		if (rc <= 0) {
			rc = rc ? rc : -EIO;
			goto free_sgt;
		}
		break;
	case ELCORE50_NONCACHED_BUFFER:
		break;
	default:
		rc = -EINVAL;
		goto free_sgt;
	}

	return &priv->sgt;

free_sgt:
	return ERR_PTR(rc);
}

static void elcore50_unmap_dmabuf(struct dma_buf_attachment *attach,
				  struct sg_table *sgt,
				  enum dma_data_direction dir)
{
	struct elcore50_buffer_priv *priv = attach->dmabuf->priv;

	if (priv->buf_info.type == ELCORE50_CACHED_BUFFER_FROM_UPTR)
		dma_unmap_sg_attrs(attach->dev, sgt->sgl, sgt->orig_nents, dir,
				   DMA_ATTR_SKIP_CPU_SYNC);
}

static void *elcore50_dmabuf_map(struct dma_buf *dmabuf, unsigned long page_num)
{
	struct elcore50_buffer_priv *priv = dmabuf->priv;

	if (!priv->vaddr)
		priv->vaddr = vm_map_ram(priv->pages, priv->num_pages, -1);

	return priv->vaddr ? priv->vaddr + page_num * PAGE_SIZE : NULL;
}

static int elcore50_dmabuf_mmap(struct dma_buf *dmabuf,
				struct vm_area_struct *vma)
{
	struct elcore50_buffer_priv *priv = dmabuf->priv;
	unsigned long uaddr;
	unsigned long usize;
	int ret, i = 0;

	WARN_ON(vma->vm_pgoff != 0);

	switch (priv->buf_info.type) {
	case ELCORE50_CACHED_BUFFER_FROM_UPTR:
		uaddr = vma->vm_start;
		usize = vma->vm_end - vma->vm_start;
		do {
			if (i >= priv->num_pages)
				return -EFAULT;
			ret = vm_insert_page(vma, uaddr, priv->pages[i++]);
			if (ret) {
				dev_err(priv->dev,
					"Remapping memory, error: %d\n", ret);
				return ret;
			}

			uaddr += PAGE_SIZE;
			usize -= PAGE_SIZE;
		} while (usize > 0);
		break;
	case ELCORE50_NONCACHED_BUFFER:
		vma->vm_pgoff = 0;
		ret = dma_mmap_coherent(priv->dev, vma, priv->vaddr,
					priv->paddr, priv->buf_info.size);
		if (ret) {
			pr_err("Remapping memory failed, error: %d\n", ret);
			return ret;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void elcore50_dmabuf_release(struct dma_buf *dmabuf)
{
	struct elcore50_buffer_priv *priv = dmabuf->priv;

	switch (priv->buf_info.type) {
	case ELCORE50_CACHED_BUFFER_FROM_UPTR:
		trace_buffer_release("cached", priv->buf_info.size);
		break;
	case ELCORE50_NONCACHED_BUFFER:
		trace_buffer_release("noncached", priv->buf_info.size);
		break;
	}

	elcore50_free_buffer(priv);
	kfree(priv);
}

static int elcore50_dmabuf_vmap(struct dma_buf *dmabuf, struct iosys_map *map)
{
	struct elcore50_buffer_priv *priv = dmabuf->priv;
	size_t offset;
	void *vaddr;

	offset = priv->sgt.sgl[0].offset;

	vaddr = elcore50_dmabuf_map(dmabuf, 0);
	if (!vaddr)
		return -ENOMEM;
	vaddr += offset;

	iosys_map_set_vaddr(map, vaddr);

	return 0;
}

static void elcore50_dmabuf_vunmap(struct dma_buf *dmabuf,
				   struct iosys_map *map)
{
	struct elcore50_buffer_priv *priv = dmabuf->priv;

	if (priv->buf_info.type == ELCORE50_NONCACHED_BUFFER)
		return;

	WARN_ON(((unsigned long)map->vaddr & PAGE_MASK) !=
		(unsigned long)priv->vaddr);

	vm_unmap_ram(priv->vaddr, priv->num_pages);
}

const struct dma_buf_ops elcore50_dmabuf_ops = {
	.map_dma_buf = elcore50_map_dmabuf,
	.unmap_dma_buf = elcore50_unmap_dmabuf,
	.release = elcore50_dmabuf_release,
	.mmap = elcore50_dmabuf_mmap,
	.vmap = elcore50_dmabuf_vmap,
	.vunmap = elcore50_dmabuf_vunmap
};

int elcore50_create_buffer(struct device *dev, void __user *arg)
{
	struct elcore50_buf buf_info;
	struct elcore50_buffer_priv *priv;
	struct dma_buf *dmabuf;
	int ret;
	DEFINE_DMA_BUF_EXPORT_INFO(exp_info);

	priv = kzalloc(sizeof(struct elcore50_buffer_priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	ret = copy_from_user(&buf_info, arg, sizeof(struct elcore50_buf));
	if (ret) {
		ret = -EACCES;
		goto err_bufpriv;
	}
	priv->buf_info = buf_info;
	priv->dev = dev;

	switch (buf_info.type) {
	case ELCORE50_CACHED_BUFFER_FROM_UPTR:
		trace_buffer_create("cached", buf_info.size);
		break;
	case ELCORE50_NONCACHED_BUFFER:
		trace_buffer_create("noncached", buf_info.size);
		break;
	}

	ret = elcore50_allocate_buffer(priv);
	if (ret)
		goto err_bufpriv;

	exp_info.ops = &elcore50_dmabuf_ops;
	exp_info.size = priv->buf_info.size;
	exp_info.flags = O_RDWR;
	exp_info.priv = priv;

	dmabuf = dma_buf_export(&exp_info);
	if (IS_ERR(dmabuf)) {
		ret = PTR_ERR(dmabuf);
		elcore50_free_buffer(priv);
		goto err_bufpriv;
	}
	dmabuf->owner = THIS_MODULE;

	ret = get_unused_fd_flags(O_RDWR | O_CLOEXEC);
	if (ret < 0)
		goto dmabuf_put;
	priv->buf_info.dmabuf_fd = ret;

	ret = copy_to_user(arg, &priv->buf_info, sizeof(struct elcore50_buf));
	if (ret) {
		put_unused_fd(priv->buf_info.dmabuf_fd);
		ret = -EFAULT;
		goto dmabuf_put;
	}

	fd_install(priv->buf_info.dmabuf_fd, dmabuf->file);
	return 0;
dmabuf_put:
	dma_buf_put(dmabuf);
err_bufpriv:
	kfree(priv);
	return ret;
}

void *dma_buf_vmap_compat(struct dma_buf *dmabuf)
{
	struct iosys_map map = IOSYS_MAP_INIT_VADDR(NULL);
	int ret;

	ret = dma_buf_vmap_unlocked(dmabuf, &map);
	if (ret)
		return NULL;
	return map.vaddr;
}
