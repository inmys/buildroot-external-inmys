/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Basic general purpose allocator for managing special purpose
 * memory, for example, memory that is not managed by the regular
 * kmalloc/kfree interface.  Uses for this includes on-device special
 * memory, uncached memory etc.
 *
 * Copyright 2005 (C) Jes Sorensen <jes@trained-monkey.org>
 * Copyright 2018-2019 RnD Center "ELVEES", JSC
 * It is safe to use the allocator in NMI handlers and other special
 * unblockable contexts that could otherwise deadlock on locks.  This
 * is implemented by using atomic operations and retries on any
 * conflicts.  The disadvantage is that there may be livelocks in
 * extreme cases.  For better scalability, one allocator can be used
 * for each CPU.
 *
 * The lockless operation only works if there is enough memory
 * available.  If new memory is added to the pool a lock has to be
 * still taken.  So any user relying on locklessness has to ensure
 * that sufficient memory is preallocated.
 *
 * The basic atomic operation of this allocator is cmpxchg on long.
 * On architectures that don't have NMI-safe cmpxchg implementation,
 * the allocator can NOT be used in NMI handler.  So code uses the
 * allocator in NMI handler should depend on
 * CONFIG_ARCH_HAVE_NMI_SAFE_CMPXCHG.
 */

#ifndef __MMU_ALLOC_H__
#define __MMU_ALLOC_H__

#include <linux/types.h>
#include <linux/spinlock_types.h>

struct device;
struct device_node;
struct mmu_pool;

/**
 * typedef mmupool_algo_t: Allocation callback function type definition
 * @map: Pointer to bitmap
 * @size: The bitmap size in bits
 * @start: The bitnumber to start searching at
 * @nr: The number of zeroed bits we're looking for
 * @data: optional additional data used by the callback
 * @pool: the pool being allocated from
 */
typedef unsigned long (*mmupool_algo_t)(unsigned long *map, unsigned long size,
					unsigned long start, unsigned int nr,
					void *data, struct mmu_pool *pool);

/*
 *  General purpose special memory pool descriptor.
 */
struct mmu_pool {
	spinlock_t lock;
	struct list_head chunks; /* list of chunks in this pool */
	int min_alloc_order; /* minimum allocation order */

	mmupool_algo_t algo; /* allocation function */
	void *data;

	const char *name;
};

/*
 *  General purpose special memory pool chunk descriptor.
 */
struct mmu_pool_chunk {
	struct list_head next_chunk; /* next chunk in pool */
	atomic64_t avail;
	unsigned long start_addr; /* start address of memory chunk */
	unsigned long end_addr; /* end address of memory chunk (incl) */
	unsigned long bits[0]; /* bitmap for allocating memory chunk */
};

/*
 *  mmu_pool data descriptor for mmu_pool_first_fit_align.
 */
struct mmupool_data_align {
	int align; /* alignment by bytes for starting address */
};

/*
 *  mmu_pool data descriptor for mmu_pool_fixed_alloc.
 */
struct mmupool_data_fixed {
	unsigned long offset; /* The offset of the specific region */
};

extern struct mmu_pool *mmu_pool_create(int min_alloc_order, int nid);
extern int mmu_pool_add(struct mmu_pool *pool, unsigned long addr, size_t size,
			int nid);
extern void mmu_pool_destroy(struct mmu_pool *pool);
extern unsigned long mmu_pool_alloc(struct mmu_pool *pool, size_t size);
extern unsigned long mmu_pool_alloc_algo(struct mmu_pool *pool, size_t size,
					 mmupool_algo_t algo, void *data);
extern void mmu_pool_free(struct mmu_pool *pool, unsigned long addr,
			  size_t size);

extern unsigned long mmu_pool_first_fit(unsigned long *map, unsigned long size,
					unsigned long start, unsigned int nr,
					void *data, struct mmu_pool *pool);

extern unsigned long mmu_pool_fixed_alloc(unsigned long *map,
					  unsigned long size,
					  unsigned long start, unsigned int nr,
					  void *data, struct mmu_pool *pool);

bool addr_in_mmu_pool(struct mmu_pool *pool, unsigned long start, size_t size);
bool addr_in_mmu_pool_chunk(struct mmu_pool *pool, unsigned long start,
			    size_t size);

#endif
