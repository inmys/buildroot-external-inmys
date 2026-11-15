// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2020 RnD Center "ELVEES", JSC
 */

#include <linux/slab.h>

#include "elcore50-job.h"
#include "elcore50-mmu.h"
#include "elcore50-trace.h"

struct e50_protected_region {
	u32 base_addr;
	bool is_cluster_specific;
	u32 size;
	struct userptr_mapper mapper;
};

#ifdef ELCORE50_MCOM03
static struct e50_protected_region protected_regions[] = {
	{ 0x1800000, 0, 0x800000, { 0 } }, // EPort
	{ 0x2800000, 0, 0x40000, { 0 } }, // Registers
	{ 0x2E00000, 0, 0x600000, { 0 } } // PRAM/XYRAM
};
#else
static struct e50_protected_region protected_regions[] = {
	{ 0x10000000, 0, 0x2000000, { 0 } }, // Registers
	{ 0x13000000, 1, 0x100000, { 0 } }, // QPRAM/QYRAM
	{ 0x17000000, 1, 0x40000, { 0 } } // QRegisters
};
#endif

static inline unsigned long elcore50_mmu_alloc_fixed(struct mmu_pool *pool,
						     size_t size,
						     unsigned long address)
{
	struct mmupool_data_fixed data;

	data.offset = address - ELCORE50_VADDR_START;
	return mmu_pool_alloc_algo(pool, size, mmu_pool_fixed_alloc, &data);
}

static inline void write_pte_entry(uint64_t *pte, dma_addr_t paddr,
				   enum pte_type type)
{
	uint64_t tmp;

	tmp = E50_PTE_V_MASK;
	tmp |= (paddr >> E50_PAGE_SHIFT) << E50_PTE_PADDR_SHIFT;
	tmp |= (type << E50_PTE_TYPE_SHIFT) & E50_PTE_TYPE_MASK;
	/* FIXME: this is a hack - rf#10482 */
	if (type > PTE_NEXTGL)
		tmp |= E50_PTE_D_MASK | E50_PTE_R_MASK;
	*pte = tmp;
	pr_debug("pte %p paddr %llx type=%i res=%llx", pte, paddr, type, tmp);
}

static struct page_entry *get_page_entry(struct elcore50_job_desc *job_desc,
					 uint64_t vaddr_mmu_dsp,
					 enum elcore50_page_size ps,
					 int is_argument, bool is_48bit)
{
	int i, j;
	dma_addr_t paddr;
	struct page *page;
	struct page_entry *p = job_desc->p_top, *p_cur;
	uint64_t *pte;
	uint64_t index;
	/* 48-bit addresses don't work with elcore50 */
	if (vaddr_mmu_dsp >> (is_48bit ? 48 : 32)) {
		dev_err(job_desc->dev, "Virtual address 0x%llx out of bounds\n",
			vaddr_mmu_dsp);
		return NULL;
	}
	for (j = VMMU_PAGE_MAX_LEVEL; j > ps; j--) {
		index = (vaddr_mmu_dsp >> (E50_PAGE_SHIFT + j * 9)) &
			GENMASK(8, 0);

		p_cur = p + index;
		if (p_cur->next_lvl) {
			p = p_cur->next_lvl;
		} else {
			p_cur->next_lvl = kvcalloc(E50_PTE_ENTRIES,
						   sizeof(struct page_entry),
						   GFP_KERNEL);
			if (!p_cur->next_lvl)
				return NULL;
			p_cur->next_lvl->parent_pte = p_cur;
			p_cur->table = p;

			/* Mark the new page table dirty */
			p_cur->dirty = 1;

			page = alloc_pages(GFP_KERNEL | __GFP_ZERO,
					   get_order(E50_PAGE_SIZE));
			if (!page)
				goto clean_next_lvl;
			pte = page_address(page);
			paddr = dma_map_single_attrs(job_desc->dev, pte,
						     E50_PAGE_SIZE,
						     DMA_TO_DEVICE,
						     DMA_ATTR_SKIP_CPU_SYNC);
			if (dma_mapping_error(job_desc->dev, paddr))
				goto clean_page;
			p_cur->dma_addr = paddr;
			write_pte_entry(p_cur->pte, paddr, PTE_NEXT);
			/* Mark current page table dirty */
			if (p->parent_pte)
				p->parent_pte->dirty = 1;

			p->num_non_zero_ptes += 1;
			p = p_cur->next_lvl;
			if (is_argument)
				list_add_tail(&p_cur->list_node,
					      &job_desc->pentries.args_list);
			else
				list_add_tail(&p_cur->list_node,
					      &job_desc->pentries.elfs_list);

			for (i = 0; i < E50_PTE_ENTRIES; i++)
				p[i].pte = &pte[i];
			continue;
clean_page:
			free_pages((unsigned long)pte,
				   get_order(E50_PAGE_SIZE));
clean_next_lvl:
			kvfree(p_cur->next_lvl);
			p_cur->next_lvl = NULL;
			return NULL;
		}
	}
	/* Increment ptes counter for leaf node. write_pte_entry() will be
	 * called in write_buffer_pages()
	 */
	p->num_non_zero_ptes += 1;
	/* move the pointer to the leaf node rather than leaf page start */
	index = (vaddr_mmu_dsp >> (E50_PAGE_SHIFT + j * 9)) & GENMASK(8, 0);
	p_cur = p + index;
	p_cur->table = p;
	if (is_argument)
		list_add_tail(&p_cur->list_node, &job_desc->pentries.args_list);
	else
		list_add_tail(&p_cur->list_node, &job_desc->pentries.elfs_list);
	p->parent_pte->dirty = 1;
	return p_cur;
}

static int write_buffer_pages(struct elcore50_job_desc *job_desc,
			      uint64_t buf_vaddr_mmu_dsp, enum pte_type type,
			      dma_addr_t buf_paddr, size_t buf_size,
			      bool is_48bit, struct userptr_mapper *mapper,
			      uint64_t vaddr_start_mmu_dsp)
{
	size_t page_size;
	enum elcore50_page_size ps;
	struct page_entry *p;

	pr_debug("buf_paddr=%llx, vaddr=%llx, size=%lx\n", buf_paddr,
		 buf_vaddr_mmu_dsp, buf_size);
	WARN_ON(buf_paddr & E50_INPAGE_MASK);
	WARN_ON(buf_vaddr_mmu_dsp & E50_INPAGE_MASK);
	WARN_ON(buf_size & E50_INPAGE_MASK);
	WARN_ON(mapper == NULL);
	while (buf_size > 0) {
		if (job_desc->hugepages) {
			if (!(buf_vaddr_mmu_dsp & (SZ_1G - 1)) &&
			    buf_size >= SZ_1G) {
				page_size = SZ_1G;
				ps = ELCORE50_PAGE_1G;
			} else if (!(buf_vaddr_mmu_dsp & (SZ_2M - 1)) &&
				   buf_size >= SZ_2M) {
				page_size = SZ_2M;
				ps = ELCORE50_PAGE_2M;
			} else {
				page_size = SZ_4K;
				ps = ELCORE50_PAGE_4K;
			}
		} else {
			page_size = SZ_4K;
			ps = ELCORE50_PAGE_4K;
		}
		p = get_page_entry(job_desc, buf_vaddr_mmu_dsp, ps,
				   mapper->is_argument, is_48bit);
		if (!p)
			return -ENOMEM;
		p->mapper = mapper;
		p->offset = buf_vaddr_mmu_dsp - vaddr_start_mmu_dsp;

		write_pte_entry(p->pte, buf_paddr, type);
		buf_vaddr_mmu_dsp += page_size;
		buf_paddr += page_size;
		buf_size -= page_size;
	}
	return 0;
}

static void mmu_free_page_entry(struct device *dev, struct page_entry *pentry)
{
	struct page_entry *table;

	while (pentry) {
		list_del(&pentry->list_node);
		*pentry->pte = 0;

		table = pentry->table;
		if (table->parent_pte)
			table->parent_pte->dirty = 1;
		table->num_non_zero_ptes--;

		if (table->num_non_zero_ptes != 0)
			break;
		if (!table->parent_pte)
			break;

		pentry = table->parent_pte;
		if (pentry->dma_addr) {
			dma_unmap_single_attrs(dev, pentry->dma_addr,
					       E50_PAGE_SIZE, DMA_TO_DEVICE,
					       DMA_ATTR_SKIP_CPU_SYNC);
			pentry->dma_addr = 0;
		}
		pentry->next_lvl = NULL;
		pentry->mapper = NULL;

		free_pages((unsigned long)table->pte, get_order(E50_PAGE_SIZE));
		kvfree(table);
	}
}

static void mmu_free_pentries(struct device *dev, struct list_head *pentry_list)
{
	struct page_entry *p_cur;

	while (!list_empty(pentry_list)) {
		p_cur = list_last_entry(pentry_list, struct page_entry,
					list_node);
		mmu_free_page_entry(dev, p_cur);
	}
}

static void mmu_free_pentries_for_mapper(struct device *dev,
					 struct list_head *pentry_list,
					 struct userptr_mapper *mapper)
{
	struct page_entry *p_cur, *found;

	do {
		found = NULL;
		list_for_each_entry_reverse(p_cur, pentry_list, list_node) {
			if (p_cur->mapper == mapper) {
				found = p_cur;
				break;
			}
		}
		if (found)
			mmu_free_page_entry(dev, found);
	} while (found);
}

static void sync_elcore_pte_table_for_device(struct device *dev,
					     struct page_entries *pentries)
{
	struct page_entry *p_cur;

	if (WARN_ON(!pentries))
		return;

	/* Sync elf page entries */
	list_for_each_entry(p_cur, &pentries->elfs_list, list_node) {
		if (p_cur->dirty && p_cur->dma_addr) {
			dma_sync_single_for_device(dev, p_cur->dma_addr,
						   E50_PAGE_SIZE,
						   DMA_TO_DEVICE);
			p_cur->dirty = 0;
		}
	}

	/* Sync argument page entries */
	list_for_each_entry(p_cur, &pentries->args_list, list_node) {
		if (p_cur->dirty && p_cur->dma_addr) {
			dma_sync_single_for_device(dev, p_cur->dma_addr,
						   E50_PAGE_SIZE,
						   DMA_TO_DEVICE);
			p_cur->dirty = 0;
		}
	}
}

static int mmu_map_buffer(struct elcore50_job_desc *job_desc,
			  uint64_t vaddr_mmu_dsp, struct elcore50_buf_desc *buf)
{
	/*
	 * the only userptr that specifies the vaddr is ELF, and addresses
	 * for it are reserved in the dsp_pool.
	 */
	/* TODO: sync the memory? create non-cacheable memory? */
	int rc = 0;
	enum pte_type type;
	uint64_t vaddr_start_mmu_dsp;
	int i;
	struct scatterlist *iter;
	struct userptr_mapper *mapper = buf->mapper;
	dma_addr_t dmastart;
	size_t dmalen, dma_offset;

	if (buf->wr_addr)
		*buf->wr_addr = vaddr_mmu_dsp + mapper->offset;

	switch (buf->type) {
	case ELCORE50_ELF_SECTION_CODE:
	case ELCORE50_ELF_SECTION_DATA:
	case ELCORE50_ELF_SECTION_DATA_CONST:
		type = PTE_SVRWXURWX;
		break;
	default:
		return -EINVAL;
	}
	vaddr_start_mmu_dsp = vaddr_mmu_dsp;
	for_each_sg(mapper->sgt->sgl, iter, mapper->sgt->nents, i) {
		dma_offset = sg_dma_address(iter) & E50_INPAGE_MASK;
		dmastart = sg_dma_address(iter) - dma_offset;
		dmalen = round_up(sg_dma_len(iter) + dma_offset, E50_PAGE_SIZE);
		pr_debug("dmastart=%llx len=%lx ofst=%lx\n", dmastart, dmalen,
			 dma_offset);
		rc = write_buffer_pages(
			job_desc, vaddr_mmu_dsp, type, dmastart, dmalen,
			buf->arg_type == ELCORE50_TYPE_DMA_MEMORY, mapper,
			vaddr_start_mmu_dsp);
		if (rc)
			return rc;
		vaddr_mmu_dsp += dmalen;
	}
	return rc;
}

static int mmu_map_elf(struct elcore50_job_desc *job_desc,
		       struct elcore50_buf_desc *elf)
{
	uint64_t vaddr_mmu_dsp;

	vaddr_mmu_dsp = elcore50_mmu_alloc_fixed(job_desc->dsp_pool,
						 elf->mapper->size_aligned,
						 elf->vaddr_mmu_dsp);
	if (!vaddr_mmu_dsp)
		return -ENOMEM;
	elf->vaddr_mmu_dsp = vaddr_mmu_dsp;
	elf->mmu_allocated = 1;
	elf->mapper->is_argument = 0;
	trace_mmu_map(elf);
	return mmu_map_buffer(job_desc, vaddr_mmu_dsp, elf);
}

static int mmu_map_buf(struct elcore50_job_inst_desc *job_inst,
		       struct elcore50_buf_desc *buf)
{
	uint64_t vaddr_mmu_dsp;
	struct mmu_pool *pool;

	if (buf->arg_type == ELCORE50_TYPE_DMA_MEMORY)
		pool = job_inst->dma_pool;
	else
		pool = job_inst->dsp_pool;
	if (buf->arg_type == ELCORE50_TYPE_NC_GLOBAL_MEMORY) {
		vaddr_mmu_dsp = elcore50_mmu_alloc_fixed(
			pool, buf->mapper->size_aligned,
			job_inst->nc_mem_current);
		job_inst->nc_mem_current += buf->mapper->size_aligned;
	} else
		vaddr_mmu_dsp = mmu_pool_alloc(pool, buf->mapper->size_aligned);
	buf->vaddr_mmu_dsp = vaddr_mmu_dsp;
	if (!vaddr_mmu_dsp)
		return -ENOMEM;
	buf->mmu_allocated = 1;
	buf->mapper->is_argument = 1;
	trace_mmu_map(buf);
	return mmu_map_buffer(job_inst->job_desc, vaddr_mmu_dsp, buf);
}

static void elcore50_mmu_free_fixed(struct elcore50_job_desc *job_desc)
{
	int cluster_idx, i;
	unsigned long addr;

	for (i = ARRAY_SIZE(protected_regions) - 1; i >= 0; i--) {
		for (cluster_idx = 0;
		     cluster_idx < elcore50_platform_info.cluster_cap;
		     cluster_idx++) {
			addr = protected_regions[i].base_addr;
			if (protected_regions[i].is_cluster_specific)
				addr += cluster_idx * protected_regions[i].size;
			else if (cluster_idx)
				break;

			mmu_pool_free(job_desc->dsp_pool, addr,
				      protected_regions[i].size);
		}
	}
}

static int elcore50_mmu_fill_fixed(struct elcore50_job_desc *job_desc)
{
	int cluster_idx, i, ret;
	unsigned long addr;
	dma_addr_t dmastart;
	struct userptr_mapper *mapper;
	size_t dmalen, dma_offset;

	for (i = 0; i < ARRAY_SIZE(protected_regions); ++i) {
		for (cluster_idx = 0;
		     cluster_idx < elcore50_platform_info.cluster_cap;
		     cluster_idx++) {
			addr = protected_regions[i].base_addr;
			if (protected_regions[i].is_cluster_specific)
				addr += cluster_idx * protected_regions[i].size;
			else if (cluster_idx)
				break;

			addr = elcore50_mmu_alloc_fixed(
				job_desc->dsp_pool, protected_regions[i].size,
				addr);
			if (!addr)
				goto fixed_alloc_err;

			dma_offset = addr & E50_INPAGE_MASK;
			dmastart = addr - dma_offset;
			dmalen =
				round_up(protected_regions[i].size + dma_offset,
					 E50_PAGE_SIZE);
			mapper = &protected_regions[i].mapper;
			ret = write_buffer_pages(job_desc, addr, PTE_SVRWXURWX,
						 dmastart, dmalen, 0, mapper,
						 addr);
			if (ret) {
				mmu_pool_free(job_desc->dsp_pool, addr,
					      protected_regions[i].size);
				goto fixed_alloc_err;
			}
		}
	}
	return 0;

fixed_alloc_err:
	while (i) {
		while (cluster_idx) {
			addr = protected_regions[i - 1].base_addr;
			if (protected_regions[i - 1].is_cluster_specific)
				addr += cluster_idx *
					protected_regions[i - 1].size;
			else if (cluster_idx) {
				cluster_idx--;
				continue;
			}

			mmu_pool_free(job_desc->dsp_pool, addr,
				      protected_regions[i - 1].size);
			cluster_idx--;
		}
		i--;
		cluster_idx = elcore50_platform_info.cluster_cap - 1;
	}
	return -ENOMEM;
}

static int elcore50_is_addr_xyram(struct elcore50_job_desc *job_desc,
				  struct elcore50_buf_desc *elf)
{
	unsigned long start, end;

	start = elf->vaddr_mmu_dsp;
	end = start + elf->mapper->size;

	if ((start >= PHYS_INTERNAL_DSP) &&
	    (end <= (PHYS_INTERNAL_INTERLEAVE_DSP + INTERNAL_DSP_SIZE))) {
		return 1;
	}
	return 0;
}

static int elcore50_mapper_vmap(struct userptr_mapper *mapper)
{
	if (!mapper->attach || !mapper->attach->dmabuf)
		return -EINVAL;

	if (mapper->vaddr)
		return 0;

	mapper->vaddr = dma_buf_vmap_compat(mapper->attach->dmabuf);
	if (!mapper->vaddr) {
		dev_err(mapper->dev, "Failed to vmap dmabuf\n");
		return -ENOMEM;
	}

	return 0;
}

/*
 * Allocate an internal-memory buffer descriptor (XYRAM or PRAM) mirroring
 * @elf and store it through @dst (either &job_desc->xyram or
 * &job_desc->pram).
 */
static int elcore50_prepare_internal(struct elcore50_job_desc *job_desc,
				     struct elcore50_buf_desc *elf,
				     struct elcore50_buf_desc **dst)
{
	struct userptr_mapper *mapper = elf->mapper;
	int ret;

	ret = elcore50_mapper_vmap(mapper);
	if (ret)
		return ret;

	*dst = kzalloc(sizeof(struct elcore50_buf_desc), GFP_KERNEL);
	if (!*dst)
		return -ENOMEM;
	(*dst)->mapper = elf->mapper;
	(*dst)->vaddr_mmu_dsp = elf->vaddr_mmu_dsp;
	return 0;
}

static int elcore50_is_addr_pram(struct elcore50_job_desc *job_desc,
				 struct elcore50_buf_desc *elf)
{
	unsigned long start, end;

	start = elf->vaddr_mmu_dsp;
	end = start + elf->mapper->size;

	if ((start >= PHYS_INTERNAL_PRAM_DSP) &&
	    (end <= (PHYS_INTERNAL_PRAM_DSP + INTERNAL_DSP_PRAM_SIZE))) {
		return 1;
	}
	return 0;
}

int elcore50_is_addr_pram_or_xyram(struct elcore50_job_desc *job_desc,
				   struct elcore50_buf_desc *elf)
{
	return elcore50_is_addr_pram(job_desc, elf) ||
	       elcore50_is_addr_xyram(job_desc, elf);
}

void elcore50_mmu_free(struct elcore50_job_desc *job_desc)
{
	int i;
	struct elcore50_buf_desc *elf;
	struct device *dev = job_desc->dev;

	__free_page(job_desc->reserve_page);

	elcore50_mmu_free_fixed(job_desc);

	dma_unmap_single_attrs(dev, job_desc->pt4_dma_addr, E50_PAGE_SIZE,
			       DMA_TO_DEVICE, DMA_ATTR_SKIP_CPU_SYNC);

	mmu_free_pentries(dev, &job_desc->pentries.args_list);
	mmu_free_pentries(dev, &job_desc->pentries.elfs_list);
	free_pages((unsigned long)job_desc->pt4, get_order(E50_PAGE_SIZE));

	for (i = 0; i < job_desc->num_elf_sections; i++) {
		elf = job_desc->elf[i];
		if (elcore50_is_addr_xyram(job_desc, elf))
			continue;
		if (elcore50_is_addr_pram(job_desc, elf))
			continue;
		mmu_pool_free(job_desc->dsp_pool, elf->vaddr_mmu_dsp,
			      elf->mapper->size_aligned);
	}
	kvfree(job_desc->p_top);
}

void elcore50_mmu_free_args(struct elcore50_job_inst_desc *job_inst)
{
	int i;
	struct elcore50_buf_desc *buf;
	struct phys_mapper *cursor, *store;
	struct device *dev = job_inst->job_desc->dev;
	struct page_entries *pentries = &job_inst->job_desc->pentries;

	list_for_each_entry_safe(cursor, store, &job_inst->pmapper_list,
				 queue_node) {
		elcore50_release_physmapper(job_inst, cursor);
	}

	list_for_each_entry_safe(cursor, store,
				 &job_inst->pmapper_reserved_list, queue_node) {
		elcore50_release_physmapper(job_inst, cursor);
	}

	mmu_free_pentries(dev, &pentries->args_list);

	for (i = 0; i < job_inst->argc; i++) {
		buf = job_inst->args[i];
		if (buf->arg_type == ELCORE50_TYPE_DMA_MEMORY)
			mmu_pool_free(job_inst->dma_pool, buf->vaddr_mmu_dsp,
				      buf->mapper->size_aligned);
		else
			mmu_pool_free(job_inst->dsp_pool, buf->vaddr_mmu_dsp,
				      buf->mapper->size_aligned);
	}
}

static void elcore50_add_prefetch_pages(struct elcore50_job_inst_desc *job_inst,
					u64 vaddr_mmu_dsp, size_t size)
{
	uint32_t addr;
	dma_addr_t paddr = page_to_phys(job_inst->job_desc->reserve_page);
	struct phys_mapper *pm;

	if (vaddr_mmu_dsp & (SZ_2M - 1)) {
		u64 before = vaddr_mmu_dsp - SZ_4K;
		// Map 4K-page at (vaddr_mmu_dsp - SZ_4K) for
		// backward cache prefetch
		if (!addr_in_mmu_pool_chunk(job_inst->dsp_pool, before,
					    SZ_4K)) {
			addr = elcore50_syscall_pmmap(job_inst, before, SZ_4K,
						      paddr, true, &pm);
			if (addr != before)
				pr_warn("prefetch before: got %X want %llX\n",
					addr, before);

			if (pm && pm->vaddr == before) {
				pm->prefetch_owner_vaddr = vaddr_mmu_dsp;
				pm->prefetch_owner_size = size;
			}
		}
	}

	if ((vaddr_mmu_dsp + size) & (SZ_2M - 1)) {
		u64 after = vaddr_mmu_dsp + size;

		if (!addr_in_mmu_pool_chunk(job_inst->dsp_pool, after, SZ_4K)) {
			// Map 4K-page at (vaddr_mmu_dsp + size) for
			// forward cache prefetch
			addr = elcore50_syscall_pmmap(job_inst, after, SZ_4K,
						      paddr, true, &pm);
			if (addr != after)
				pr_warn("prefetch after: got %X want %llX\n",
					addr, after);

			if (pm && pm->vaddr == after) {
				pm->prefetch_owner_vaddr = vaddr_mmu_dsp;
				pm->prefetch_owner_size = size;
			}
		}
	}
}

void elcore50_mmu_sync(struct elcore50_job_inst_desc *job_inst)
{
	int i;
	struct elcore50_job_desc *job_desc = job_inst->job_desc;
	struct device *dev = job_desc->dev;
	size_t size;
	u64 vaddr_mmu_dsp;

	// Add empty 4K pages to prevent cache prefetch error
	for (i = 0; i < job_desc->num_elf_sections; ++i) {
		vaddr_mmu_dsp = job_desc->elf[i]->vaddr_mmu_dsp;
		size = job_desc->elf[i]->mapper->size_aligned;

		elcore50_add_prefetch_pages(job_inst, vaddr_mmu_dsp, size);
	}

	for (i = 0; i < job_inst->argc; ++i) {
		if (job_inst->args[i]->arg_type == ELCORE50_TYPE_DMA_MEMORY)
			continue;
		vaddr_mmu_dsp = job_inst->args[i]->vaddr_mmu_dsp;
		size = job_inst->args[i]->mapper->size_aligned;

		elcore50_add_prefetch_pages(job_inst, vaddr_mmu_dsp, size);
	}

	dma_sync_single_for_device(dev, job_inst->job_desc->pt4_dma_addr,
				   E50_PAGE_SIZE, DMA_TO_DEVICE);
	sync_elcore_pte_table_for_device(dev, &job_desc->pentries);
}

int elcore50_mmu_fill(struct elcore50_job_desc *job_desc)
{
	int i, rc = 0;
	uint64_t *pt4;
	struct device *dev = job_desc->dev;
	struct page_entry *p_top;
	struct page *page;

	page = alloc_pages(GFP_KERNEL | __GFP_ZERO, get_order(E50_PAGE_SIZE));
	if (!page)
		return -ENOMEM;
	pt4 = page_address(page);
	job_desc->pt4 = pt4;
	job_desc->pt4_dma_addr = dma_map_single_attrs(
		dev, pt4, E50_PAGE_SIZE, DMA_TO_DEVICE, DMA_ATTR_SKIP_CPU_SYNC);
	if (dma_mapping_error(dev, job_desc->pt4_dma_addr)) {
		free_pages((unsigned long)pt4, get_order(E50_PAGE_SIZE));
		return -ENOMEM;
	}

	p_top = kvcalloc(E50_PTE_ENTRIES, sizeof(struct page_entry),
			 GFP_KERNEL);
	if (!p_top) {
		rc = -ENOMEM;
		goto clean_pt4;
	}
	/* Set to prevent p_top page releasing in mmu_free_pentries(). */
	p_top->num_non_zero_ptes = 1;
	job_desc->p_top = p_top;
	for (i = 0; i < E50_PTE_ENTRIES; i++) {
		INIT_LIST_HEAD(&p_top[i].list_node);
		p_top[i].pte = &pt4[i];
	}

	rc = elcore50_mmu_fill_fixed(job_desc);
	if (rc)
		goto clean_pt4;

	for (i = 0; i < job_desc->num_elf_sections; i++) {
		if (elcore50_is_addr_xyram(job_desc, job_desc->elf[i])) {
			if (elcore50_prepare_internal(job_desc,
						      job_desc->elf[i],
						      &job_desc->xyram))
				goto clean_elf;
			continue;
		}
		if (elcore50_is_addr_pram(job_desc, job_desc->elf[i])) {
			if (elcore50_prepare_internal(job_desc,
						      job_desc->elf[i],
						      &job_desc->pram))
				goto clean_elf;
			continue;
		}
		rc = mmu_map_elf(job_desc, job_desc->elf[i]);
		if (rc != 0)
			goto clean_elf;
	}

	job_desc->reserve_page =
		alloc_pages(GFP_KERNEL, get_order(E50_PAGE_SIZE));
	if (!job_desc->reserve_page)
		goto clean_elf;

	/* Here we can to sync ptes for ELF sections */
	sync_elcore_pte_table_for_device(dev, &job_desc->pentries);

	return 0;
clean_elf:
	for (i = 0; i < job_desc->num_elf_sections; i++) {
		if (job_desc->elf[i]->mmu_allocated)
			mmu_pool_free(job_desc->dsp_pool,
				      job_desc->elf[i]->vaddr_mmu_dsp,
				      job_desc->elf[i]->mapper->size_aligned);
	}
	elcore50_mmu_free_fixed(job_desc);
clean_pt4:
	mmu_free_pentries(dev, &job_desc->pentries.elfs_list);
	kvfree(p_top);
	dma_unmap_single_attrs(dev, (unsigned long)pt4, E50_PAGE_SIZE,
			       DMA_TO_DEVICE, DMA_ATTR_SKIP_CPU_SYNC);
	free_pages((unsigned long)pt4, get_order(E50_PAGE_SIZE));
	return rc;
}

int elcore50_mmu_fill_args(struct elcore50_job_inst_desc *job_inst)
{
	int i, rc = 0;
	struct elcore50_buf_desc *arg;
	struct userptr_mapper *mapper;
	uint64_t vaddr_mmu_dsp = 0;

	for (i = 0; i < job_inst->argc; i++) {
		arg = job_inst->args[i];
		if (arg->arg_type != ELCORE50_TYPE_NC_GLOBAL_MEMORY)
			continue;
		rc = mmu_map_buf(job_inst, arg);
		if (rc != 0)
			goto clean_args;
	}
	if (job_inst->nc_mem_current) {
		vaddr_mmu_dsp = elcore50_mmu_alloc_fixed(
			job_inst->dsp_pool,
			(1UL << 32) - job_inst->nc_mem_current,
			job_inst->nc_mem_current);
		if (!vaddr_mmu_dsp) {
			rc = -ENOMEM;
			goto clean_args;
		}
	}

	for (i = 0; i < job_inst->argc; i++) {
		arg = job_inst->args[i];
		if (arg->arg_type == ELCORE50_TYPE_NC_GLOBAL_MEMORY)
			continue;
		rc = mmu_map_buf(job_inst, arg);
		if (rc != 0)
			goto clean_args;
	}

	if (job_inst->nc_mem_current && vaddr_mmu_dsp) {
		mmu_pool_free(job_inst->dsp_pool, vaddr_mmu_dsp,
			      (1UL << 32) - job_inst->nc_mem_current);
	}
	return 0;
clean_args:
	for (i = 0; i < job_inst->argc; i++) {
		if (job_inst->args[i]->mmu_allocated) {
			arg = job_inst->args[i];
			mapper = arg->mapper;
			if (arg->arg_type == ELCORE50_TYPE_DMA_MEMORY)
				mmu_pool_free(job_inst->dma_pool,
					      arg->vaddr_mmu_dsp,
					      mapper->size_aligned);
			else
				mmu_pool_free(job_inst->dsp_pool,
					      arg->vaddr_mmu_dsp,
					      mapper->size_aligned);
		}
	}
	if (job_inst->nc_mem_current && vaddr_mmu_dsp) {
		mmu_pool_free(job_inst->dsp_pool, vaddr_mmu_dsp,
			      (1UL << 32) - job_inst->nc_mem_current);
	}
	return rc;
}

static bool check_protected_regions_overlapping(u64 paddr, size_t length)
{
	size_t cluster_idx, i;
	u64 region_start_addr, region_end_addr;

	for (i = 0; i < ARRAY_SIZE(protected_regions); ++i) {
		for (cluster_idx = 0;
		     cluster_idx < elcore50_platform_info.cluster_cap;
		     cluster_idx++) {
			region_start_addr = (u64)protected_regions[i].base_addr;
			if (protected_regions[i].is_cluster_specific)
				region_start_addr +=
					cluster_idx * protected_regions[i].size;
			else if (cluster_idx)
				break;
			region_end_addr =
				region_start_addr + protected_regions[i].size;

			if ((region_start_addr <= (paddr + length)) &&
			    (paddr < region_end_addr)) {
				/* Physical range overlaps protected region
				 * that can only be accessed by a physical
				 * address.
				 */
				return true;
			}
		}
	}
	return false;
}

uint32_t elcore50_syscall_pmmap(struct elcore50_job_inst_desc *job_inst,
				uint32_t pref_vaddr, size_t length,
				uint64_t paddr, bool is_reserved,
				struct phys_mapper **pmapper_out)
{
	struct elcore50_job_desc *job_desc = job_inst->job_desc;
	struct phys_mapper *pmapper;
	uint64_t vaddr_mmu_dsp = 0;
	dma_addr_t dmastart;
	size_t dmalen, dma_offset;
	int ret;

	if (pmapper_out)
		*pmapper_out = NULL;

	if (length & E50_INPAGE_MASK)
		return 0;

	ret = check_protected_regions_overlapping(paddr, length);
	if (ret)
		return 0;

	if (pref_vaddr)
		vaddr_mmu_dsp = elcore50_mmu_alloc_fixed(job_desc->dsp_pool,
							 length, pref_vaddr);
	if (!vaddr_mmu_dsp)
		vaddr_mmu_dsp = mmu_pool_alloc(job_desc->dsp_pool, length);

	if (!vaddr_mmu_dsp) {
		// TODO: Try to free space by removing prefetch reserved
		//       regions.
		return 0;
	}

	pmapper = kmalloc(sizeof(struct phys_mapper), GFP_KERNEL);
	if (!pmapper)
		goto pool_free_vaddr;

	pmapper->paddr = paddr;
	pmapper->size = length;
	pmapper->vaddr = vaddr_mmu_dsp;
	pmapper->mapper_stub.is_argument = 1;
	pmapper->prefetch_owner_vaddr = 0;
	pmapper->prefetch_owner_size = 0;

	dma_offset = paddr & E50_INPAGE_MASK;
	dmastart = paddr - dma_offset;
	dmalen = round_up(length + dma_offset, E50_PAGE_SIZE);
	ret = write_buffer_pages(job_desc, vaddr_mmu_dsp, PTE_SVRWXURWX,
				 dmastart, dmalen, 0, &pmapper->mapper_stub,
				 vaddr_mmu_dsp);
	if (ret)
		goto pmapper_kfree;

	if (is_reserved) {
		list_add_tail(&pmapper->queue_node,
			      &job_inst->pmapper_reserved_list);
	} else {
		list_add_tail(&pmapper->queue_node, &job_inst->pmapper_list);
		elcore50_add_prefetch_pages(job_inst, vaddr_mmu_dsp, length);
	}

	if (pmapper_out)
		*pmapper_out = pmapper;

	return vaddr_mmu_dsp;
pmapper_kfree:
	kfree(pmapper);
pool_free_vaddr:
	mmu_pool_free(job_desc->dsp_pool, vaddr_mmu_dsp, length);
	return 0;
}

void elcore50_release_physmapper(struct elcore50_job_inst_desc *job_inst,
				 struct phys_mapper *cursor)
{
	struct page_entries *pentries = &job_inst->job_desc->pentries;
	struct device *dev = job_inst->job_desc->dev;

	mmu_free_pentries_for_mapper(dev, &pentries->args_list,
				     &cursor->mapper_stub);
	mmu_pool_free(job_inst->dsp_pool, cursor->vaddr, cursor->size);
	list_del(&cursor->queue_node);
	kfree(cursor);
}

int elcore50_syscall_pmunmap(struct elcore50_job_inst_desc *job_inst,
			     uint32_t vaddr_mmu_dsp, size_t length)
{
	struct phys_mapper *cursor, *store;
	uint32_t prefetch_before = (uint32_t)(vaddr_mmu_dsp - SZ_4K);
	uint32_t prefetch_after = (uint32_t)((uint64_t)vaddr_mmu_dsp + length);
	bool found = false;

	list_for_each_entry_safe(cursor, store, &job_inst->pmapper_list,
				 queue_node) {
		if ((cursor->vaddr == vaddr_mmu_dsp) &&
		    (cursor->size == length)) {
			elcore50_release_physmapper(job_inst, cursor);
			found = true;
			break;
		}
	}

	if (!found)
		return -EINVAL;

	// Remove prefetch protected regions
	list_for_each_entry_safe(cursor, store,
				 &job_inst->pmapper_reserved_list, queue_node) {
		WARN_ON(cursor->size != SZ_4K);
		if (cursor->prefetch_owner_vaddr != vaddr_mmu_dsp ||
		    cursor->prefetch_owner_size != length)
			continue;

		if (cursor->vaddr == prefetch_before ||
		    cursor->vaddr == prefetch_after)
			elcore50_release_physmapper(job_inst, cursor);
	}
	return 0;
}

inline bool elcore50_vaddr_is_internal(u64 vaddr_mmu_dsp, size_t size)
{
	const uint32_t internal_end_addr =
		PHYS_INTERNAL_INTERLEAVE_DSP + INTERNAL_DSP_SIZE;

	if ((vaddr_mmu_dsp >= PHYS_INTERNAL_DSP) &&
	    (vaddr_mmu_dsp + size) <= internal_end_addr)
		return true;
	return false;
}

static struct userptr_mapper *
elcore50_vaddr_to_mapper_internal(struct page_entry *p, u64 vaddr_mmu_dsp,
				  u8 *out_inpage_offset, size_t *out_pte_offset)
{
	u64 index, i;
	u8 inpage_offset = 38;

	if (p == NULL) {
		WARN_ON(1);
		return NULL;
	}

	for (i = 3; i >= 0; i--) {
		index = (vaddr_mmu_dsp >> (E50_PAGE_SHIFT + i * 9)) &
			GENMASK(8, 0);
		p = p + index;
		if (p->next_lvl == NULL)
			break;
		p = p->next_lvl;
		inpage_offset -= 9;
	}

	if ((p == NULL) || (p->mapper == NULL)) {
		WARN_ON(1);
		return NULL;
	}

	if (out_inpage_offset)
		*out_inpage_offset = inpage_offset;

	if (out_pte_offset)
		*out_pte_offset = p->offset;

	return p->mapper;
}

struct userptr_mapper *elcore50_vaddr_to_mapper(struct elcore50_job_desc *desc,
						u64 vaddr_mmu_dsp)
{
	struct userptr_mapper *mapper;

	if (elcore50_vaddr_is_internal(vaddr_mmu_dsp, 0))
		mapper = desc->xyram->mapper;
	else
		mapper = elcore50_vaddr_to_mapper_internal(
			desc->p_top, vaddr_mmu_dsp, NULL, NULL);

	if (mapper && elcore50_mapper_vmap(mapper))
		return NULL;

	return mapper;
}

void *elcore50_vaddr_to_kernel(struct elcore50_job_desc *desc,
			       u64 vaddr_mmu_dsp)
{
	bool is_internal = elcore50_vaddr_is_internal(vaddr_mmu_dsp, 0);
	void *retval;
	u8 inpage_offset;
	size_t pte_offset = 0;
	struct userptr_mapper *mapper;

	if (is_internal)
		mapper = desc->xyram->mapper;
	else
		mapper = elcore50_vaddr_to_mapper_internal(desc->p_top,
							   vaddr_mmu_dsp,
							   &inpage_offset,
							   &pte_offset);
	if (!mapper)
		return NULL;

	if (elcore50_mapper_vmap(mapper))
		return NULL;

	retval = mapper->vaddr + pte_offset - mapper->offset;
	if (is_internal)
		retval += vaddr_mmu_dsp - PHYS_INTERNAL_DSP;
	else
		retval += vaddr_mmu_dsp & GENMASK(inpage_offset, 0);

	return retval;
}

/* Translate DSP virtual address to CPU virtual address */
void *elcore50_map_from_users(struct elcore50_job_inst_desc *desc,
			      u64 vaddr_mmu_dsp,
			      struct userptr_mapper **out_mapper,
			      size_t *offset, u64 *user_vaddr_cpu, size_t size)
{
	bool is_internal = elcore50_vaddr_is_internal(vaddr_mmu_dsp, size);
	u64 ret;
	void *retval;
	u8 inpage_offset;
	size_t pte_offset = 0;
	struct userptr_mapper *mapper;
	struct elcore50_buffer_priv *buf_priv;
	struct elcore50_core *core = desc->core;
	struct elcore50_job_desc *job_desc = desc->job_desc;

	WARN_ON(size == 0);

	if (is_internal)
		mapper = job_desc->xyram->mapper;
	else
		mapper = elcore50_vaddr_to_mapper_internal(job_desc->p_top,
							   vaddr_mmu_dsp,
							   &inpage_offset,
							   &pte_offset);
	if (!mapper)
		return NULL;

	if (mapper->dmabuf->ops != &elcore50_dmabuf_ops) {
		dev_err(core->drv_priv->dev,
			"Attempt to map external buffer\n");
		return NULL;
	}
	buf_priv = mapper->dmabuf->priv;

	*out_mapper = mapper;
	if (elcore50_mapper_vmap(mapper))
		return NULL;
	retval = mapper->vaddr + pte_offset - mapper->offset;
	if (is_internal)
		retval += vaddr_mmu_dsp - PHYS_INTERNAL_DSP;
	else
		retval += vaddr_mmu_dsp & GENMASK(inpage_offset, 0);
	*offset = retval - mapper->vaddr;
	if (buf_priv->buf_info.p == 0)
		*user_vaddr_cpu = 0;
	else
		*user_vaddr_cpu = buf_priv->buf_info.p + *offset;

	if (is_internal && (vaddr_mmu_dsp >= PHYS_INTERNAL_INTERLEAVE_DSP)) {
		retval -= INTERNAL_DSP_INTERLEAVE;

		if (*user_vaddr_cpu)
			*user_vaddr_cpu -= INTERNAL_DSP_INTERLEAVE;
	}

	if (is_internal)
		memcpy_fromio(retval, core->xyram + *offset, size);
	else {
		// Invalidate CPU caches
		ret = sync_buffer(size, *offset, mapper,
				  ELCORE50_BUF_SYNC_DIR_TO_CPU);
		if (ret)
			return NULL;
	}

	return retval;
}
