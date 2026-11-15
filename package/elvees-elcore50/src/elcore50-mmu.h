/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2020 RnD Center "ELVEES", JSC
 */
#ifndef _LINUX_ELCORE50_MMU_H
#define _LINUX_ELCORE50_MMU_H

#include "elcore50-job-instance.h"
#include "mmu-alloc.h"

enum elcore50_page_size {
	ELCORE50_PAGE_4K = 0,
	ELCORE50_PAGE_2M = 1,
	ELCORE50_PAGE_1G = 2,
	ELCORE50_PAGE_512G = 3
};

enum pte_type {
	PTE_NEXT = 0,
	PTE_NEXTGL = 1,
	PTE_SVROURX = 2,
	PTE_SVRWURWX = 3,
	PTE_SVROURO = 4,
	PTE_SVRWURW = 5,
	PTE_SVRXURX = 6,
	PTE_SVRWXURWX = 7,
	PTE_SVRO = 8,
	PTE_SVRW = 9,
	PTE_SVRX = 10,
	PTE_SVRWX = 11,
	PTE_SVROGL = 12,
	PTE_SVRWGL = 13,
	PTE_SVRXGL = 14,
	PTE_SVRWXGL = 15
};

struct page_entry {
	uint64_t *pte;
	struct page_entry *table;
	struct page_entry *next_lvl;
	struct page_entry *parent_pte;
	dma_addr_t dma_addr;
	struct userptr_mapper *mapper;
	size_t offset;
	size_t num_non_zero_ptes;

	/* if page entry is synchronized for device */
	int dirty;

	/* list of allocated page entries*/
	struct list_head list_node;
};

int elcore50_mmu_fill(struct elcore50_job_desc *job_desc);
int elcore50_is_addr_pram_or_xyram(struct elcore50_job_desc *job_desc,
				   struct elcore50_buf_desc *elf);
void elcore50_mmu_free(struct elcore50_job_desc *job_desc);
int elcore50_mmu_fill_args(struct elcore50_job_inst_desc *job_inst);
void elcore50_mmu_free_args(struct elcore50_job_inst_desc *job_inst);
void elcore50_mmu_sync(struct elcore50_job_inst_desc *job_inst);
uint32_t elcore50_syscall_pmmap(struct elcore50_job_inst_desc *job_inst,
				uint32_t pref_vaddr, size_t length,
				uint64_t paddr, bool is_reserved,
				struct phys_mapper **pmapper_out);
int elcore50_syscall_pmunmap(struct elcore50_job_inst_desc *job_inst,
			     uint32_t vaddr_mmu_dsp, size_t length);
void elcore50_release_physmapper(struct elcore50_job_inst_desc *job_inst,
				 struct phys_mapper *cursor);
struct userptr_mapper *elcore50_vaddr_to_mapper(struct elcore50_job_desc *desc,
						u64 vaddr_mmu_dsp);
void *elcore50_vaddr_to_kernel(struct elcore50_job_desc *desc,
			       u64 vaddr_mmu_dsp);
void *elcore50_map_from_users(struct elcore50_job_inst_desc *desc,
			      u64 vaddr_mmu_dsp,
			      struct userptr_mapper **out_mapper,
			      size_t *offset, u64 *user_vaddr_cpu, size_t size);
#endif
