// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 Junhui Liu <junhui.liu@pigmoral.tech>
 */

#include <linux/clk.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/remoteproc.h>
#include <linux/reset.h>

#include "remoteproc_internal.h"

struct spacemit_syscon {
	struct regmap *map;
	u32 reg;
};

struct spacemit_rproc {
	struct rproc *rproc;
	struct device *dev;

	struct clk *clk;
	struct reset_control *reset;

	struct spacemit_syscon boot_entry;
	struct spacemit_syscon boot_ctrl;
};

static void *spacemit_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
{
	struct device *dev = rproc->dev.parent;
	struct device_node *np = dev->of_node;
	struct rproc_mem_entry *carveout;
	void *ptr = NULL;
	u64 pa;

	pa = of_translate_dma_address(np, (const u32 *)&da);

	dev_dbg(&rproc->dev, "translated da 0x%llx to pa 0x%llx\n", da, pa);

	list_for_each_entry(carveout, &rproc->carveouts, node) {
		int offset = pa - carveout->da;

		if (!carveout->va || offset < 0 || offset + len > carveout->len)
			continue;

		ptr = carveout->va + offset;

		if (is_iomem)
			*is_iomem = carveout->is_iomem;

		break;
	}

	return ptr;
}

static int spacemit_rproc_mem_alloc(struct rproc *rproc,
				    struct rproc_mem_entry *mem)
{
	void __iomem *va;

	va = ioremap_wc(mem->dma, mem->len);
	if (!va)
		return -ENOMEM;

	/* Update memory entry va */
	mem->va = (void *)va;

	return 0;
}

static int spacemit_rproc_mem_release(struct rproc *rproc,
				      struct rproc_mem_entry *mem)
{
	iounmap((void __iomem *)mem->va);
	return 0;
}

static int spacemit_rproc_add_carveout(struct rproc *rproc)
{
	struct device *dev = rproc->dev.parent;
	struct device_node *np = dev->of_node;
	struct rproc_mem_entry *rproc_mem;
	int i = 0;

	/* Register associated reserved memory regions */
	while (1) {
		struct resource res;
		int ret;

		ret = of_reserved_mem_region_to_resource(np, i, &res);
		if (ret)
			return 0;

		if (strstarts(res.name, "vdev0buffer")) {
			/* Init reserved memory for vdev buffer */
			rproc_mem = rproc_of_resm_mem_entry_init(
				&rproc->dev, i, resource_size(&res), res.start,
				"vdev0buffer");
		} else {
			/* Register associated reserved memory regions */
			rproc_mem = rproc_mem_entry_init(
				&rproc->dev, NULL, (dma_addr_t)res.start,
				resource_size(&res), res.start,
				spacemit_rproc_mem_alloc,
				spacemit_rproc_mem_release, "%.*s",
				strchrnul(res.name, '@') - res.name, res.name);
		}

		if (!rproc_mem)
			return -ENOMEM;

		rproc_add_carveout(rproc, rproc_mem);
		rproc_coredump_add_segment(rproc, res.start,
					   resource_size(&res));

		dev_dbg(&rproc->dev, "reserved mem carveout %pR\n", &res);
		i++;
	}
}

static int spacemit_rproc_prepare(struct rproc *rproc)
{
	struct spacemit_rproc *priv = rproc->priv;
	int ret;

	ret = spacemit_rproc_add_carveout(rproc);
	if (ret)
		return ret;

	return reset_control_deassert(priv->reset);
}

static int spacemit_rproc_start(struct rproc *rproc)
{
	struct spacemit_rproc *priv = rproc->priv;
	struct spacemit_syscon *boot_entry = &priv->boot_entry;
	struct spacemit_syscon *boot_ctrl = &priv->boot_ctrl;
	int ret;

	/* set the boot-entry */
	ret = regmap_update_bits(boot_entry->map, boot_entry->reg, 0xff,
				 rproc->bootaddr);
	if (ret)
		return ret;

	/* lanching up esos */
	return regmap_update_bits(boot_ctrl->map, boot_ctrl->reg, BIT(0), 1);
}

static int spacemit_rproc_stop(struct rproc *rproc)
{
	struct spacemit_rproc *priv = rproc->priv;
	struct spacemit_syscon *boot_ctrl = &priv->boot_ctrl;
	int ret;

	ret = regmap_update_bits(boot_ctrl->map, boot_ctrl->reg, BIT(0), 0);
	if (ret)
		return ret;

	return reset_control_assert(priv->reset);
}

static int spacemit_rproc_parse_fw(struct rproc *rproc,
				   const struct firmware *fw)
{
	int ret;

	ret = rproc_elf_load_rsc_table(rproc, fw);
	if (ret == -EINVAL) {
		dev_info(&rproc->dev, "No resource table in elf\n");
		ret = 0;
	}

	return ret;
}

static const struct rproc_ops spacemit_rproc_ops = {
	.prepare = spacemit_rproc_prepare,
	.start = spacemit_rproc_start,
	.stop = spacemit_rproc_stop,
	.load = rproc_elf_load_segments,
	.parse_fw = spacemit_rproc_parse_fw,
	.find_loaded_rsc_table = rproc_elf_find_loaded_rsc_table,
	.sanity_check = rproc_elf_sanity_check,
	.get_boot_addr = rproc_elf_get_boot_addr,
	.da_to_va = spacemit_rproc_da_to_va,
};

static int spacemit_rproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct spacemit_rproc *priv;
	struct spacemit_syscon *boot_entry, *boot_ctrl;
	struct rproc *rproc;
	const char *fw_name;
	int ret;

	ret = rproc_of_parse_firmware(dev, 0, &fw_name);
	if (ret)
		return dev_err_probe(dev, ret, "No firmware filename given\n");

	rproc = devm_rproc_alloc(dev, dev_name(dev), &spacemit_rproc_ops,
				 fw_name, sizeof(*priv));
	if (!rproc)
		return dev_err_probe(dev, -ENOMEM,
				     "unable to allocate remoteproc\n");

	rproc->has_iommu = false;

	priv = rproc->priv;
	priv->dev = dev;
	priv->rproc = rproc;

	priv->reset = devm_reset_control_array_get_exclusive(&pdev->dev);
	if (IS_ERR(priv->reset))
		return dev_err_probe(dev, PTR_ERR(priv->reset),
				     "failed to get reset control handle\n");

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "Failed to get clock\n");
		return PTR_ERR(priv->clk);
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(dev, "Failed to enable clock\n");
		return ret;
	}

	boot_ctrl = &priv->boot_ctrl;
	boot_ctrl->map = syscon_regmap_lookup_by_phandle_args(
		np, "spacemit,audpmu-bootctrl", 1, &boot_ctrl->reg);
	if (IS_ERR(boot_ctrl->map))
		return dev_err_probe(dev, PTR_ERR(boot_ctrl->map),
				     "failed to get boot_ctrl\n");

	boot_entry = &priv->boot_entry;
	boot_entry->map = syscon_regmap_lookup_by_phandle_args(
		np, "spacemit,rcpu-bootentry", 1, &boot_entry->reg);
	if (IS_ERR(boot_entry->map))
		return dev_err_probe(dev, PTR_ERR(boot_entry->map),
				     "failed to get boot_entry\n");

	platform_set_drvdata(pdev, rproc);

	ret = devm_rproc_add(dev, rproc);
	if (ret)
		return dev_err_probe(dev, ret, "rproc_add failed\n");

	return 0;
}

static void spacemit_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);

	rproc_del(rproc);
}

static const struct of_device_id spacemit_rproc_of_match[] = {
	{ .compatible = "spacemit,k1-n308" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, spacemit_rproc_of_match);

static struct platform_driver spacemit_rproc_driver = {
	.probe = spacemit_rproc_probe,
	.remove = spacemit_rproc_remove,
	.driver = {
		.name = "spacemit-rproc",
		.of_match_table = spacemit_rproc_of_match,
	},
};
module_platform_driver(spacemit_rproc_driver);

MODULE_AUTHOR("Junhui Liu <junhui.liu@pigmoral.tech>");
MODULE_DESCRIPTION("SpacemiT remote processor control driver");
MODULE_LICENSE("GPL");
