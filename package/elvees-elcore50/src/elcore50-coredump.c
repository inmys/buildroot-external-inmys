// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2026 RnD Center "ELVEES", JSC
 */

#ifdef ELCORE50_DUMP
#include "elcore50-core.h"
#include "elcore50-job-instance.h"
#include "elcore50-mmu.h"
#include "elcore50-coredump.h"

void elcore50_dump_registers(struct elcore50_core *core, struct registers *regs)
{
	u32 i;

	/* RF */
	for (i = 0; i < 32; i++)
		regs->rf[i] = elcore50_read64(core, DSP_R0 + 8 * i);

	/* PCU0 */
	regs->dcsr = elcore50_read(core, DSP_DCSR);
	regs->idr = elcore50_read(core, DSP_IDR);

	/* PCU2 */
	regs->pc = elcore50_read(core, DSP_PC);
	regs->ivar = elcore50_read(core, DSP_IVAR);
	regs->irtar = elcore50_read(core, DSP_IRTAR);
	regs->evar = elcore50_read(core, DSP_EVAR);
	regs->ertar = elcore50_read(core, DSP_ERTAR);
	regs->dvar = elcore50_read(core, DSP_DVAR);
	regs->drtar = elcore50_read(core, DSP_DRTAR);

	/* PCU1 */
	regs->tmr = elcore50_read64(core, DSP_TMR);
	regs->tmrc = elcore50_read64(core, DSP_TMRC);
	regs->tic_cntr = elcore50_read64(core, DSP_TIC_CNTR);
	regs->instr_cntr = elcore50_read64(core, DSP_INSTR_CNTR);
	regs->cmd_cntr = elcore50_read64(core, DSP_CMD_CNTR);

	/* PCU3 */
	for (i = 0; i < 8; i++)
		regs->p[i] = elcore50_read(core, DSP_P0 + 4 * i);

	for (i = 0; i < 8; i++)
		regs->vp[i] = elcore50_read64(core, DSP_VP0 + 8 * i);

	/* PCU4 */
	regs->csp = elcore50_read(core, DSP_CSP);
	for (i = 0; i < 4; i++) {
		regs->lc[i] = elcore50_read(core, DSP_LC0 + 4 * i);
		regs->lb[i] = elcore50_read(core, DSP_LB0 + 8 * i);
		regs->la[i] = elcore50_read(core, DSP_LA0 + 8 * i);
	}

	/* PCU6 */
	regs->irq_index = elcore50_read(core, DSP_IRQ_INDEX);
	regs->irqr = elcore50_read(core, DSP_IRQR);
	regs->imaskr = elcore50_read(core, DSP_IMASKR);
	regs->dqstr = elcore50_read(core, DSP_DQSTR);
	regs->dmaskr = elcore50_read(core, DSP_DMASKR);
	regs->index = elcore50_read(core, DBG_INDEX);
	regs->uipc = elcore50_read(core, DSP_UIPC);
	regs->bapc = elcore50_read64(core, DSP_BAPC);

	/* PCU10 */
	regs->m[0] = elcore50_read(core, DSP_M0);
	regs->m[1] = elcore50_read(core, DSP_M1);

	/* PCU12 */
	regs->ctrl = elcore50_read(core, DSP_CTRL);
	regs->invctrl = elcore50_read(core, DSP_INVCTRL);
	regs->invaddr = elcore50_read(core, DSP_INVADDR);
	regs->asid = elcore50_read(core, DSP_ASID);
	regs->cregions = elcore50_read(core, DSP_CREGIONS);
	regs->mregions = elcore50_read(core, DSP_MREGIONS);
	regs->swaitreg[0] = elcore50_read(core, DSP_SWAITREG0);
	regs->swaitreg[1] = elcore50_read(core, DSP_SWAITREG1);
	regs->vwaitreg[0] = elcore50_read(core, DSP_VWAITREG0);
	regs->vwaitreg[1] = elcore50_read(core, DSP_VWAITREG1);
	regs->mbarreg = elcore50_read(core, DSP_MBARREG);

	/* Caches */
	regs->L0dc_ctrl = elcore50_read(core, L0DC_CTRL);
	regs->L1ic_ctrl = elcore50_read(core, L1IC_CTRL);
	regs->L1dc_ctrl = elcore50_read(core, L1DC_CTRL);
	regs->L2_ctrl = elcore50_read(core, L2_CTRL);

	regs->L1dc_rcnt = elcore50_read(core, L1DC_RCNT);
	regs->L1dc_rmcnt = elcore50_read(core, L1DC_RMCNT);
	regs->L1dc_pcnt = elcore50_read(core, L1DC_PCNT);
	regs->L1dc_wcnt = elcore50_read(core, L1DC_WCNT);
	regs->L1dc_whcnt = elcore50_read(core, L1DC_WHCNT);
	regs->L1dc_L2rcnt = elcore50_read(core, L1DC_L2RCNT);
	regs->L1dc_L2rrcnt = elcore50_read(core, L1DC_L2RRCNT);
	regs->L1dc_L2wcnt = elcore50_read(core, L1DC_L2WCNT);
	regs->L1dc_ecnt = elcore50_read(core, L1DC_ECNT);
	regs->L1dc_ucnt = elcore50_read(core, L1DC_UCNT);
	regs->L1dc_rscnt = elcore50_read(core, L1DC_RSCNT);

	regs->L2_rrec = elcore50_read(core, L2_READ_REC);
	regs->L2_rsent = elcore50_read(core, L2_READ_SENT);
	regs->L2_wrec = elcore50_read(core, L2_WRITE_REC);
	regs->L2_rhit = elcore50_read(core, L2_READ_HIT);
	regs->L2_rmiss = elcore50_read(core, L2_READ_MISS);
	regs->L2_ruc = elcore50_read(core, L2_READ_UC);
	regs->L2_whit = elcore50_read(core, L2_WRITE_HIT);
	regs->L2_wmiss = elcore50_read(core, L2_WRITE_MISS);
	regs->L2_wuc = elcore50_read(core, L2_WRITE_UC);
	regs->L2_evict = elcore50_read(core, L2_EVICT);
	regs->L2_flush = elcore50_read(core, L2_FLUSH);

	regs->L2_araxi = elcore50_read(core, L2_ARAXI);
	regs->L2_raxi = elcore50_read(core, L2_RAXI);
	regs->L2_awaxi = elcore50_read(core, L2_AWAXI);
	regs->L2_waxi = elcore50_read(core, L2_WAXI);
	regs->L2_bwaxi = elcore50_read(core, L2_BWAXI);

	/* VF */
	int reg_addr, j;

	reg_addr = DSP_VF_START;
	for (i = 0; i < 64; ++i, reg_addr += 0x40) {
		for (j = 0; j < 4; ++j)
			regs->vfl[i][j] =
				elcore50_read64(core, reg_addr + j * 8);
		for (j = 0; j < 4; ++j)
			regs->vfh[i][j] =
				elcore50_read64(core, reg_addr + (j + 4) * 8);
	}

	/* VDMA */
	int offset;

	for (i = 0; i < 4; i++) {
		offset = i * VDMA_CHANNELS_NEXT;
		regs->vdma_ae_l[i] = elcore50_read(core, VDMA_AE_L + offset);
		regs->vdma_ae_h[i] = elcore50_read(core, VDMA_AE_H + offset);
		regs->vdma_ai_l[i] = elcore50_read(core, VDMA_AI_L + offset);
		regs->vdma_ai_h[i] = elcore50_read(core, VDMA_AI_H + offset);
		regs->vdma_offset_e[i] =
			elcore50_read(core, VDMA_OFFSET_E + offset);
		regs->vdma_offset_i[i] =
			elcore50_read(core, VDMA_OFFSET_I + offset);
		regs->vdma_abcnt[i] = elcore50_read(core, VDMA_ABCNT + offset);
		regs->vdma_run[i] = elcore50_read(core, VDMA_RUN + offset);
		regs->vdma_done[i] = elcore50_read(core, VDMA_DONE + offset);
		regs->vdma_init[i] = elcore50_read(core, VDMA_INIT + offset);
		regs->vdma_irq[i] = elcore50_read(core, VDMA_IRQ + offset);
		regs->vdma_irqm[i] = elcore50_read(core, VDMA_IRQM + offset);
		regs->vdma_irqt[i] = elcore50_read(core, VDMA_IRQT + offset);
		regs->vdma_cfg[i] = elcore50_read(core, VDMA_CFG + offset);
		regs->vdma_csr[i] = elcore50_read(core, VDMA_CSR + offset);
		regs->vdma_init_l[i] =
			elcore50_read(core, VDMA_INIT_L + offset);
		regs->vdma_init_h[i] =
			elcore50_read(core, VDMA_INIT_H + offset);
	}

	/* VMMU */
	regs->ptw_pba_l = elcore50_read(core, VMMU_PTW_PBA_L);
	regs->ptw_pba_h = elcore50_read(core, VMMU_PTW_PBA_H);
	regs->ptw_cfg = elcore50_read(core, VMMU_PTW_CFG);
	regs->tlb_ctrl = elcore50_read(core, VMMU_TLB_CTRL);

	for (i = 0; i < VMMU_TLBS; ++i) {
		elcore50_write(i, core, VMMU_TLBXCPT_NUM);
		regs->tlbxcpt[i][0] = elcore50_read(core, VMMU_TLBXCPT_NUM);
		regs->tlbxcpt[i][1] = elcore50_read(core, VMMU_TLBXCPT_ADDR);
		regs->tlbxcpt[i][2] = elcore50_read(core, VMMU_TLBXCPT_TYPE);
	}
}

int elcore50_coredump_reg(struct elcore50_core *core, void *arg)
{
	int ret = 0;
	struct registers *regs = kmalloc(sizeof(struct registers), GFP_KERNEL);

	if (!regs)
		return -ENOMEM;

	elcore50_dump_registers(core, regs);

	if (copy_to_user(arg, regs, sizeof(struct registers)))
		ret = -EACCES;

	kfree(regs);
	return ret;
}

int elcore50_coredump_info(struct elcore50_job_inst_desc *desc, void *arg)
{
	struct elcore50_coredump_info info;

	if (desc->core == NULL)
		return -EPERM;

	elcore50_get_platform_info(desc->core, &info.dev_info);
	strcpy(info.job_name, desc->name);

	if (copy_to_user(arg, &info, sizeof(info)))
		return -EACCES;

	return 0;
}

int elcore50_coredump_argc(struct elcore50_job_inst_desc *desc, void *arg)
{
	if (copy_to_user(arg, &desc->raw_argc, sizeof(desc->raw_argc)))
		return -EACCES;

	return 0;
}

int elcore50_coredump_arg_info(struct elcore50_job_inst_desc *desc, void *arg)
{
	struct elcore50_buf_desc *arg_buf;
	struct userptr_mapper *mapper;
	struct elcore50_coredump_arg arg_info;
	int pointer_idx = 0, local_idx = 0, i;
	size_t copy_size;

	if (copy_from_user(&arg_info, arg, sizeof(arg_info)))
		return -EFAULT;

	if (arg_info.index < 0 || arg_info.index >= desc->raw_argc)
		return -EINVAL;

	struct elcore50_job_arg_saved *raw = &desc->raw_args[arg_info.index];

	arg_info.arg_type = raw->raw.type;
	arg_info.vaddr_mmu_dsp = 0;
	arg_info.arg_size_aligned = 0;
	arg_info.bytes_written = 0;

	for (i = 0; i < arg_info.index; i++) {
		enum elcore50_job_arg_type t = desc->raw_args[i].raw.type;

		if (t == ELCORE50_TYPE_GLOBAL_MEMORY ||
		    t == ELCORE50_TYPE_NC_GLOBAL_MEMORY ||
		    t == ELCORE50_TYPE_DMA_MEMORY)
			pointer_idx++;
		else if (t == ELCORE50_TYPE_LOCAL_MEMORY)
			local_idx++;
	}

	switch (raw->raw.type) {
	case ELCORE50_TYPE_BASIC:
		arg_info.arg_size_aligned = raw->raw.basic.size;
		if (arg_info.arg_value && arg_info.arg_value_capacity) {
			copy_size =
				min_t(size_t, arg_info.arg_value_capacity,
				      min_t(size_t, raw->raw.basic.size, 8));
			if (copy_to_user((char __user *)arg_info.arg_value,
					 raw->basic_value, copy_size))
				return -EACCES;
			arg_info.bytes_written = copy_size;
		}
		break;

	case ELCORE50_TYPE_LOCAL_MEMORY:
		arg_info.arg_size_aligned = raw->raw.local_memory.size;
		if (desc->local_args_addr)
			arg_info.vaddr_mmu_dsp =
				desc->local_args_addr[local_idx];
		break;

	case ELCORE50_TYPE_GLOBAL_MEMORY:
	case ELCORE50_TYPE_NC_GLOBAL_MEMORY:
	case ELCORE50_TYPE_DMA_MEMORY:
		arg_buf = desc->args[pointer_idx];
		mapper = arg_buf->mapper;
		if (arg_info.arg_value && arg_info.arg_value_capacity) {
			if (!mapper->vaddr) {
				struct device *core_dev = desc->core->dev;
				void *vaddr = dma_buf_vmap_compat(
					mapper->attach->dmabuf);

				if (IS_ERR_OR_NULL(vaddr)) {
					dev_err(core_dev,
						"dma_buf_vmap failed: %ld\n",
						PTR_ERR(vaddr));
					return IS_ERR(vaddr) ? PTR_ERR(vaddr) :
							       -EFAULT;
				}
				mapper->vaddr = vaddr;
			}
			copy_size = min_t(size_t, arg_info.arg_value_capacity,
					  mapper->size);
			if (copy_to_user((char __user *)arg_info.arg_value,
					 mapper->vaddr, copy_size))
				return -EACCES;
			arg_info.bytes_written = copy_size;
		}
		arg_info.vaddr_mmu_dsp = arg_buf->vaddr_mmu_dsp;
		arg_info.arg_size_aligned = mapper->size_aligned;
		break;
	default:
		return -EINVAL;
	}

	if (copy_to_user(arg, &arg_info, sizeof(arg_info)))
		return -EACCES;

	return 0;
}

int elcore50_coredump_internal(struct elcore50_job_inst_desc *desc, u32 cmd,
			       void *arg)
{
	void __iomem *mem;
	size_t data_size;
	u8 *data;

	if (cmd == ELCORE50_IOC_COREDUMP_PRAM) {
		mem = desc->core->pram;
		data_size = INTERNAL_DSP_PRAM_SIZE;
	} else if (cmd == ELCORE50_IOC_COREDUMP_XYRAM) {
		mem = desc->core->xyram;
		data_size = DOUBLED_INTERNAL_DSP_SIZE;
	} else
		return -EINVAL;

	data = kvmalloc(data_size, GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	memcpy_fromio(data, mem, data_size);
	if (copy_to_user(arg, data, data_size)) {
		kvfree(data);
		return -EACCES;
	}
	kvfree(data);

	return 0;
}

int elcore50_coredump_done(struct elcore50_job_inst_desc *desc)
{
	desc->coredump_done = true;
	wake_up(&desc->coredump_waitq);
	return 0;
}

#endif
