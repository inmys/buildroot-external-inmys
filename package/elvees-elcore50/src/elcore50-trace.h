/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Copyright 2022 RnD Center "ELVEES", JSC
 */

#ifndef _ELCORE50_TRACE_H
#define _ELCORE50_TRACE_H

#include "elcore50-job.h"
#include "elcore50-mapper.h"

/* We need to make these functions do nothing if CONFIG_EVENT_TRACING isn't
 * enabled, just like the actual trace event functions that the kernel
 * defines for us.
 */
#ifdef CONFIG_EVENT_TRACING
void trace_job_enqueue(void);
void trace_buffer_create(const char *type, size_t size);
void trace_buffer_release(const char *type, size_t size);
void trace_mmu_map(struct elcore50_buf_desc *buf);
void trace_syscall(const char *name, size_t arg0_mem_size,
		   size_t arg1_mem_size);
void trace_uptime(struct elcore50_core *core, const char *kernel_name);
void trace_buffer_sync(size_t size);
#else /* CONFIG_TRACE_EVENTS */
static inline void trace_job_enqueue(void)
{
}
static inline void trace_buffer_create(const char *type, size_t size)
{
}
static inline void trace_buffer_release(const char *type, size_t size)
{
}
static inline void trace_buffer_sync(size_t size)
{
}
static inline void trace_mmu_map(struct elcore50_buf_desc *buf)
{
}
static inline void trace_syscall(const char *name, size_t arg0_mem_size,
				 size_t arg1_mem_size)
{
}
static inline void trace_uptime(struct elcore50_core *core,
				const char *kernel_name)
{
}
#endif /* CONFIG_TRACE_EVENTS */

#endif /* _ELCORE50_TRACE_H */
