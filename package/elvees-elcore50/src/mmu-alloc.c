// SPDX-License-Identifier: GPL-2.0
/*
 * This is a stripped-to-bits version of lib/genalloc.c
 * Copyright 2005 (C) Jes Sorensen <jes@trained-monkey.org>
 * Copyright 2018-2020 RnD Center "ELVEES", JSC
 */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/bitmap.h>
#include <linux/rculist.h>
#include <linux/interrupt.h>
#include <linux/module.h>

#include "mmu-alloc.h"

static inline size_t chunk_size(const struct mmu_pool_chunk *chunk)
{
	return chunk->end_addr - chunk->start_addr + 1;
}

static int set_bits_ll(unsigned long *addr, unsigned long mask_to_set)
{
	unsigned long val, nval;

	nval = *addr;
	do {
		val = nval;
		if (val & mask_to_set)
			return -EBUSY;
		cpu_relax();
	} while ((nval = cmpxchg(addr, val, val | mask_to_set)) != val);

	return 0;
}

static int clear_bits_ll(unsigned long *addr, unsigned long mask_to_clear)
{
	unsigned long val, nval;

	nval = *addr;
	do {
		val = nval;
		if ((val & mask_to_clear) != mask_to_clear)
			return -EBUSY;
		cpu_relax();
	} while ((nval = cmpxchg(addr, val, val & ~mask_to_clear)) != val);

	return 0;
}

/*
 * bitmap_set_ll - set the specified number of bits at the specified position
 * @map: pointer to a bitmap
 * @start: a bit position in @map
 * @nr: number of bits to set
 *
 * Set @nr bits start from @start in @map lock-lessly. Several users
 * can set/clear the same bitmap simultaneously without lock. If two
 * users set the same bit, one user will return remain bits, otherwise
 * return 0.
 */
static int bitmap_set_ll(unsigned long *map, int start, int nr)
{
	unsigned long *p = map + BIT_WORD(start);
	const int size = start + nr;
	int bits_to_set = BITS_PER_LONG - (start % BITS_PER_LONG);
	unsigned long mask_to_set = BITMAP_FIRST_WORD_MASK(start);

	while (nr - bits_to_set >= 0) {
		if (set_bits_ll(p, mask_to_set))
			return nr;
		nr -= bits_to_set;
		bits_to_set = BITS_PER_LONG;
		mask_to_set = ~0UL;
		p++;
	}
	if (nr) {
		mask_to_set &= BITMAP_LAST_WORD_MASK(size);
		if (set_bits_ll(p, mask_to_set))
			return nr;
	}

	return 0;
}

/*
 * bitmap_clear_ll - clear specified number of bits at the specified position
 * @map: pointer to a bitmap
 * @start: a bit position in @map
 * @nr: number of bits to set
 *
 * Clear @nr bits start from @start in @map lock-lessly. Several users
 * can set/clear the same bitmap simultaneously without lock. If two
 * users clear the same bit, one user will return remain bits,
 * otherwise return 0.
 */
static int bitmap_clear_ll(unsigned long *map, int start, int nr)
{
	unsigned long *p = map + BIT_WORD(start);
	const int size = start + nr;
	int bits_to_clear = BITS_PER_LONG - (start % BITS_PER_LONG);
	unsigned long mask_to_clear = BITMAP_FIRST_WORD_MASK(start);

	while (nr - bits_to_clear >= 0) {
		if (clear_bits_ll(p, mask_to_clear))
			return nr;
		nr -= bits_to_clear;
		bits_to_clear = BITS_PER_LONG;
		mask_to_clear = ~0UL;
		p++;
	}
	if (nr) {
		mask_to_clear &= BITMAP_LAST_WORD_MASK(size);
		if (clear_bits_ll(p, mask_to_clear))
			return nr;
	}

	return 0;
}

/**
 * mmu_pool_create - create a new special memory pool
 * @min_alloc_order: log base 2 of number of bytes each bitmap bit represents
 * @nid: node id of the node the pool structure should be allocated on, or -1
 *
 * Create a new special memory pool that can be used to manage special purpose
 * memory not managed by the regular kmalloc/kfree interface.
 */
struct mmu_pool *mmu_pool_create(int min_alloc_order, int nid)
{
	struct mmu_pool *pool;

	pool = kmalloc_node(sizeof(struct mmu_pool), GFP_KERNEL, nid);
	if (pool != NULL) {
		spin_lock_init(&pool->lock);
		INIT_LIST_HEAD(&pool->chunks);
		pool->min_alloc_order = min_alloc_order;
		pool->algo = mmu_pool_first_fit;
		pool->data = NULL;
		pool->name = NULL;
	}
	return pool;
}
EXPORT_SYMBOL(mmu_pool_create);

/**
 * mmu_pool_add - add a new chunk of special memory to the pool
 * @pool: pool to add new memory chunk to
 * @addr: starting address of memory chunk to add to pool
 * @size: size in bytes of the memory chunk to add to pool
 * @nid: node id of the node the chunk structure and bitmap should be
 *       allocated on, or -1
 *
 * Add a new chunk of special memory to the specified pool.
 *
 * Returns 0 on success or a -ve errno on failure.
 */
int mmu_pool_add(struct mmu_pool *pool, unsigned long addr, size_t size,
		 int nid)
{
	struct mmu_pool_chunk *chunk;
	int nbits = size >> pool->min_alloc_order;
	int nbytes = sizeof(struct mmu_pool_chunk) +
		     BITS_TO_LONGS(nbits) * sizeof(long);

	chunk = kzalloc_node(nbytes, GFP_KERNEL, nid);
	if (unlikely(chunk == NULL))
		return -ENOMEM;

	chunk->start_addr = addr;
	chunk->end_addr = addr + size - 1;
	atomic64_set(&chunk->avail, size);

	spin_lock(&pool->lock);
	list_add_rcu(&chunk->next_chunk, &pool->chunks);
	spin_unlock(&pool->lock);

	return 0;
}
EXPORT_SYMBOL(mmu_pool_add);

/**
 * mmu_pool_destroy - destroy a special memory pool
 * @pool: pool to destroy
 *
 * Destroy the specified special memory pool. Verifies that there are no
 * outstanding allocations.
 */
void mmu_pool_destroy(struct mmu_pool *pool)
{
	struct list_head *_chunk, *_next_chunk;
	struct mmu_pool_chunk *chunk;
	int order = pool->min_alloc_order;
	int bit, end_bit;

	list_for_each_safe(_chunk, _next_chunk, &pool->chunks) {
		chunk = list_entry(_chunk, struct mmu_pool_chunk, next_chunk);
		list_del(&chunk->next_chunk);

		end_bit = chunk_size(chunk) >> order;
		bit = find_next_bit(chunk->bits, end_bit, 0);
		WARN_ON_ONCE(bit < end_bit);

		kfree(chunk);
	}
	kfree_const(pool->name);
	kfree(pool);
}
EXPORT_SYMBOL(mmu_pool_destroy);

/**
 * mmu_pool_alloc - allocate special memory from the pool
 * @pool: pool to allocate from
 * @size: number of bytes to allocate from the pool
 *
 * Allocate the requested number of bytes from the specified pool.
 * Uses the pool allocation function (with first-fit algorithm by default).
 * Can not be used in NMI handler on architectures without
 * NMI-safe cmpxchg implementation.
 */
unsigned long mmu_pool_alloc(struct mmu_pool *pool, size_t size)
{
	return mmu_pool_alloc_algo(pool, size, pool->algo, pool->data);
}
EXPORT_SYMBOL(mmu_pool_alloc);

/**
 * mmu_pool_alloc_algo - allocate special memory from the pool
 * @pool: pool to allocate from
 * @size: number of bytes to allocate from the pool
 * @algo: algorithm passed from caller
 * @data: data passed to algorithm
 *
 * Allocate the requested number of bytes from the specified pool.
 * Uses the pool allocation function (with first-fit algorithm by default).
 * Can not be used in NMI handler on architectures without
 * NMI-safe cmpxchg implementation.
 */
unsigned long mmu_pool_alloc_algo(struct mmu_pool *pool, size_t size,
				  mmupool_algo_t algo, void *data)
{
	struct mmu_pool_chunk *chunk;
	unsigned long addr = 0;
	int order = pool->min_alloc_order;
	int nbits, start_bit, end_bit, remain;

#ifndef CONFIG_ARCH_HAVE_NMI_SAFE_CMPXCHG
	BUG_ON(in_nmi());
#endif

	if (size == 0)
		return 0;

	nbits = (size + (1UL << order) - 1) >> order;
	rcu_read_lock();
	list_for_each_entry_rcu(chunk, &pool->chunks, next_chunk) {
		if (size > atomic64_read(&chunk->avail))
			continue;

		start_bit = 0;
		end_bit = chunk_size(chunk) >> order;
retry:
		start_bit = algo(chunk->bits, end_bit, start_bit, nbits, data,
				 pool);
		if (start_bit >= end_bit)
			continue;
		remain = bitmap_set_ll(chunk->bits, start_bit, nbits);
		if (remain) {
			remain = bitmap_clear_ll(chunk->bits, start_bit,
						 nbits - remain);
			BUG_ON(remain);
			goto retry;
		}

		addr = chunk->start_addr + ((unsigned long)start_bit << order);
		size = nbits << order;
		atomic64_sub(size, &chunk->avail);
		break;
	}
	rcu_read_unlock();
	return addr;
}
EXPORT_SYMBOL(mmu_pool_alloc_algo);

/**
 * mmu_pool_free - free allocated special memory back to the pool
 * @pool: pool to free to
 * @addr: starting address of memory to free back to pool
 * @size: size in bytes of memory to free
 *
 * Free previously allocated special memory back to the specified
 * pool.  Can not be used in NMI handler on architectures without
 * NMI-safe cmpxchg implementation.
 */
void mmu_pool_free(struct mmu_pool *pool, unsigned long addr, size_t size)
{
	struct mmu_pool_chunk *chunk;
	int order = pool->min_alloc_order;
	int start_bit, nbits, remain;

#ifndef CONFIG_ARCH_HAVE_NMI_SAFE_CMPXCHG
	BUG_ON(in_nmi());
#endif

	nbits = (size + (1UL << order) - 1) >> order;
	rcu_read_lock();
	list_for_each_entry_rcu(chunk, &pool->chunks, next_chunk) {
		if (addr >= chunk->start_addr && addr <= chunk->end_addr) {
			BUG_ON(addr + size - 1 > chunk->end_addr);
			start_bit = (addr - chunk->start_addr) >> order;
			remain = bitmap_clear_ll(chunk->bits, start_bit, nbits);
			BUG_ON(remain);
			size = nbits << order;
			atomic64_add(size, &chunk->avail);
			rcu_read_unlock();
			return;
		}
	}
	rcu_read_unlock();
	BUG();
}
EXPORT_SYMBOL(mmu_pool_free);

static bool addr_in_chunk(struct mmu_pool *pool, struct mmu_pool_chunk *chunk,
			  unsigned long start, size_t size)
{
	size_t i, start_bit, bits_number;
	unsigned long *p;

	if (ilog2(size) < pool->min_alloc_order) {
		WARN_ON(1);
		bits_number = 1;
	} else
		bits_number = ilog2(size) - pool->min_alloc_order + 1;

	start_bit = (start - chunk->start_addr) >> pool->min_alloc_order;

	for (i = 0; i < bits_number; ++i) {
		p = chunk->bits + BIT_WORD(start_bit);

		if ((*p & BIT(start_bit % BITS_PER_LONG)) == 0)
			return false;
		start_bit++;
	}

	return true;
}

bool addr_in_mmu_pool_chunk(struct mmu_pool *pool, unsigned long start,
			    size_t size)
{
	bool found = false;
	unsigned long end = start + size - 1;
	struct mmu_pool_chunk *chunk;

	rcu_read_lock();
	list_for_each_entry_rcu(chunk, &(pool)->chunks, next_chunk) {
		if (start >= chunk->start_addr && start <= chunk->end_addr) {
			if (end <= chunk->end_addr) {
				if (addr_in_chunk(pool, chunk, start, size)) {
					found = true;
					break;
				}
			}
		}
	}
	rcu_read_unlock();
	return found;
}
EXPORT_SYMBOL(addr_in_mmu_pool_chunk);

/**
 * mmu_pool_first_fit - find the first available region
 * of memory matching the size requirement (no alignment constraint)
 * @map: The address to base the search on
 * @size: The bitmap size in bits
 * @start: The bitnumber to start searching at
 * @nr: The number of zeroed bits we're looking for
 * @data: additional data - unused
 * @pool: pool to find the fit region memory from
 */
unsigned long mmu_pool_first_fit(unsigned long *map, unsigned long size,
				 unsigned long start, unsigned int nr,
				 void *data, struct mmu_pool *pool)
{
	return bitmap_find_next_zero_area(map, size, start, nr, 0);
}
EXPORT_SYMBOL(mmu_pool_first_fit);

/**
 * mmu_pool_fixed_alloc - reserve a specific region
 * @map: The address to base the search on
 * @size: The bitmap size in bits
 * @start: The bitnumber to start searching at
 * @nr: The number of zeroed bits we're looking for
 * @data: data for alignment
 * @pool: pool to get order from
 */
unsigned long mmu_pool_fixed_alloc(unsigned long *map, unsigned long size,
				   unsigned long start, unsigned int nr,
				   void *data, struct mmu_pool *pool)
{
	struct mmupool_data_fixed *fixed_data;
	int order;
	unsigned long offset_bit;
	unsigned long start_bit;

	fixed_data = data;
	order = pool->min_alloc_order;
	offset_bit = fixed_data->offset >> order;
	if (WARN_ON(fixed_data->offset & ((1UL << order) - 1)))
		return size;

	start_bit = bitmap_find_next_zero_area(map, size, start + offset_bit,
					       nr, 0);
	if (start_bit != offset_bit)
		start_bit = size;
	return start_bit;
}
EXPORT_SYMBOL(mmu_pool_fixed_alloc);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Memalloc library for ELcore50 MMU");
