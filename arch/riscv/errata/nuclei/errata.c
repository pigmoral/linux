// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (C) 2026 Junhui Liu <junhui.liu@pigmoral.tech>
 */

#include <asm/alternative.h>
#include <asm/csr.h>
#include <asm/dma-noncoherent.h>
#include <linux/acpi.h>
#include <linux/align.h>
#include <linux/cacheflush.h>
#include <linux/io.h>
#include <linux/irqflags.h>
#include <linux/of.h>

#define CSR_CCM_SBEGINADDR	0x5cb
#define CSR_CCM_SCOMMAND	0x5cc

#define NUCLEI_CCM_DC_INVAL	0x0
#define NUCLEI_CCM_DC_WB	0x1
#define NUCLEI_CCM_DC_WBINVAL	0x2

#define NUCLEI_UX900_MARCHID	0xc900

static void nuclei_ccm_op(unsigned long op, phys_addr_t paddr, size_t size)
{
	unsigned long start, end, flags;

	if (!size)
		return;

	start = (unsigned long)phys_to_virt(paddr);
	end = start + size;
	/* Align start to the cache block. */
	start = ALIGN_DOWN(start, (unsigned long)riscv_cbom_block_size);

	/*
	 * The Nuclei CCM uses CCM_SBEGINADDR for setting the virtual address
	 * and CCM_SCOMMAND to trigger the cache operation. The hardware
	 * automatically increments SBEGINADDR by one cache line after each
	 * operation.
	 * Disable interrupts to prevent other contexts on this hart from
	 * overwriting these CSRs during the loop.
	 */
	local_irq_save(flags);

	csr_write(CSR_CCM_SBEGINADDR, start);
	for (; start < end; start += riscv_cbom_block_size)
		csr_write(CSR_CCM_SCOMMAND, op);

	local_irq_restore(flags);
}

static void nuclei_errata_cache_inv(phys_addr_t paddr, size_t size)
{
	nuclei_ccm_op(NUCLEI_CCM_DC_INVAL, paddr, size);
}

static void nuclei_errata_cache_wback(phys_addr_t paddr, size_t size)
{
	nuclei_ccm_op(NUCLEI_CCM_DC_WB, paddr, size);
}

static void nuclei_errata_cache_wback_inv(phys_addr_t paddr, size_t size)
{
	nuclei_ccm_op(NUCLEI_CCM_DC_WBINVAL, paddr, size);
}

static const struct riscv_nonstd_cache_ops nuclei_errata_cmo_ops = {
	.wback = nuclei_errata_cache_wback,
	.inv = nuclei_errata_cache_inv,
	.wback_inv = nuclei_errata_cache_wback_inv,
};

static void nuclei_get_cbom_block_size(u32 *block_size)
{
	unsigned long hartid, first_hartid = 0;
	struct device_node *node;
	u32 val;

	for_each_of_cpu_node(node) {
		if (riscv_of_processor_hartid(node, &hartid))
			continue;

		if (of_property_read_u32(node, "d-cache-block-size", &val))
			continue;

		if (!*block_size) {
			*block_size = val;
			first_hartid = hartid;
		} else if (*block_size != val) {
			pr_warn("d-cache-block-size mismatched between harts %lu and %lu\n",
				first_hartid, hartid);
		}
	}
}

static void errata_probe_cmo(unsigned long arch_id, unsigned long impid)
{
	u32 block_size = 0;

	if (!IS_ENABLED(CONFIG_ERRATA_NUCLEI_CMO))
		return;

	/*
	 * UX900 cores prior to v2.8.0 (impid < 0x20800) predate RVA22 and
	 * rely on the Nuclei CCM mechanism for cache management.
	 */
	if (arch_id != NUCLEI_UX900_MARCHID || impid >= 0x20800)
		return;

	if (!acpi_disabled)
		return;

	nuclei_get_cbom_block_size(&block_size);
	if (!block_size)
		return;

	riscv_cbom_block_size = block_size;
	riscv_noncoherent_supported();
	riscv_noncoherent_register_cache_ops(&nuclei_errata_cmo_ops);
}

void __init_or_module nuclei_errata_patch_func(struct alt_entry *begin, struct alt_entry *end,
					       unsigned long archid, unsigned long impid,
					       unsigned int stage)
{
	if (stage == RISCV_ALTERNATIVES_BOOT)
		errata_probe_cmo(archid, impid);
}
