// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2022 RnD Center "ELVEES", JSC
 */

#include "elcore50-trace.h"

#define CREATE_TRACE_POINTS
#include "elcore50-trace-events.h"

#ifdef CONFIG_EVENT_TRACING

void trace_buffer_create(const char *type, size_t size)
{
	trace_elcore50_buf_create(type, size);
}

void trace_buffer_release(const char *type, size_t size)
{
	trace_elcore50_buf_release(type, size);
}

void trace_buffer_sync(size_t size)
{
	trace_elcore50_buf_sync(size);
}

void trace_syscall(const char *name, size_t arg0_mem_size, size_t arg1_mem_size)
{
	trace_elcore50_syscall(name, arg0_mem_size, arg1_mem_size);
}

void trace_mmu_map(struct elcore50_buf_desc *buf)
{
	trace_elcore50_mmu_map(buf);
}

void trace_uptime(struct elcore50_core *core, const char *kernel_name)
{
	trace_elcore50_uptime(core, kernel_name);
}

#endif
