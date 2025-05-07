#include <asm/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <linux/reset-controller.h>
#include <dt-bindings/reset/canaan,k230-rst.h>

struct k230_reset_test_map {
	u32 id;
	u32 addr_to_write;
	u32 write_val;
	u32 reset_val;
};

static const struct k230_reset_test_map k230_reset_test_map[] = {
	// RST_TYPE_HW_DONE
	{ RST_SDIO1, 0x91581008, 0x00000001, 0x00000000},
	{ RST_USB0, 0x91500008, 0x00000001, 0x00000000},
	{ RST_USB1, 0x91540008, 0x00000001, 0x00000000},
	{ RST_SPI0, 0x9158400c, 0x00000001, 0x00000000},
	{ RST_SPI1, 0x9158200c, 0x00000001, 0x00000000},
	{ RST_SPI2, 0x9158300c, 0x00000001, 0x00000000},
	// RST_TYPE_SW_DONE
	{ RST_TIMER_APB, 0x91105808, 0x00000002, 0x00000000},
	{ RST_MAILBOX, 0x91104000, 0x00000002, 0x00000000},
	{ RST_UART1, 0x9140100C, 0x00000001, 0x00000000},
	{ RST_UART2, 0x9140200C, 0x00000001, 0x00000000},
	{ RST_UART3, 0x9140300C, 0x00000001, 0x00000000},
	{ RST_UART4, 0x9140400C, 0x00000001, 0x00000000},
	{ RST_I2C0, 0x91405008, 0x00000066, 0x00000055},
	{ RST_I2C1, 0x91406008, 0x00000066, 0x00000055},
	{ RST_I2C2, 0x91407008, 0x00000066, 0x00000055},
	{ RST_I2C3, 0x91408008, 0x00000066, 0x00000055},
	{ RST_I2C4, 0x91409008, 0x00000066, 0x00000055},
	{ RST_GPIO_APB, 0x9140B000, 0x0000001, 0x0000000},
	{ RST_PWM_APB, 0x9140a000, 0x0000001, 0x0000000},
};

static int k230_rst_test(struct device *dev)
{
	int ret = 0;
	int i;
	int nr = ARRAY_SIZE(k230_reset_test_map);
	void __iomem *reg;
	u32 val;
	struct reset_control *rc;
	const struct k230_reset_test_map *node;

	for (i = 0; i < nr; i++) {
		node = &k230_reset_test_map[i];

		reg = ioremap(node->addr_to_write, sizeof(u32));
		if (!reg) {
			printk(KERN_ERR "ioremap failed for id: %u\n", node->id);
			ret = -ENOMEM;
			continue;
		}

		/* Write value to ensure not default value */
		iowrite32(node->write_val, reg);
		val = ioread32(reg);
		if (val != node->write_val) {
			printk(KERN_ERR "Write verification failed for id: %u, wrote: 0x%x, read: 0x%x\n",
				node->id, node->write_val, val);
			iounmap(reg);
			ret = -EIO;
			continue;
		}

		rc = devm_reset_control_get_by_index(dev, i);
		if (IS_ERR(rc)) {
			printk(KERN_ERR "Failed to get reset control for id: %u\n", node->id);
			iounmap(reg);
			ret = PTR_ERR(rc);
			continue;
		}

		/* Reset the device */
		ret = reset_control_reset(rc);
		if (ret) {
			printk(KERN_ERR "Reset control failed for id: %u, error: %d\n", node->id, ret);
			iounmap(reg);
			continue;
		}

		/* Check reset value */
		val = ioread32(reg);
		if (val != node->reset_val) {
			printk(KERN_ERR "Reset verification failed for id: %u, expected: 0x%x, got: 0x%x\n",
				node->id, node->reset_val, val);
			ret = -EIO;
		} else {
			printk(KERN_INFO "Reset test passed for id: %u\n", node->id);
		}

		iounmap(reg);
	}

	return ret;
}

static int k230_rst_test_probe(struct platform_device *pdev) {
	printk("Test start\n");

	struct device *dev = &pdev->dev;

	k230_rst_test(dev);

	printk("Test end\n");

	return 0;
}

static void k230_rst_test_remove(struct platform_device *pdev) {
	printk("Exiting test module\n");
}

static const struct of_device_id k230_rst_test_match[] = {
	{ .compatible = "canaan,k230-rst-test", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, k230_rst_test_match);

static struct platform_driver k230_rst_test_driver = {
	.probe = k230_rst_test_probe,
	.remove = k230_rst_test_remove,
	.driver = {
		.name = "k230-rst-test",
		.of_match_table = k230_rst_test_match,
	}
};
module_platform_driver(k230_rst_test_driver);

MODULE_AUTHOR("Junhui Liu <junhui.liu@pigmoral.tech>");
MODULE_DESCRIPTION("Canaan K230 reset driver test");
MODULE_LICENSE("GPL");
