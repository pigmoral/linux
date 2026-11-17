// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Junhui Liu <junhui.liu@pigmoral.tech>
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/generic.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/reset.h>
#include <linux/slab.h>

#define DR1_GPIO_DR		0x00
#define DR1_GPIO_DDR		0x04
#define DR1_GPIO_DR_CLR		0x08
#define DR1_GPIO_DDR_CLR	0x0c
#define DR1_GPIO_INTEN_CLR	0x10
#define DR1_GPIO_INTMASK_CLR	0x14
#define DR1_GPIO_INTTYPE_CLR	0x18
#define DR1_GPIO_POLARITY_CLR	0x1c
#define DR1_GPIO_DEBOUNCE_CLR	0x20
#define DR1_GPIO_BOTHEDGE_CLR	0x24
#define DR1_GPIO_INTEN		0x30
#define DR1_GPIO_INTMASK	0x34
#define DR1_GPIO_INTTYPE	0x38
#define DR1_GPIO_POLARITY	0x3c
#define DR1_GPIO_INTSTATUS	0x40
#define DR1_GPIO_DEBOUNCE	0x48
#define DR1_GPIO_EOI		0x4c
#define DR1_GPIO_EXT		0x50
#define DR1_GPIO_BOTHEDGE	0x68

struct dr1v90_gpio {
	struct gpio_generic_chip chip;
	struct clk_bulk_data clks[2];
	void __iomem *base;
	unsigned int irq;
};

static struct dr1v90_gpio *to_dr1v90_gpio(struct gpio_chip *gc)
{
	return gpiochip_get_data(gc);
}

static int dr1v90_gpio_set_config(struct gpio_chip *gc, unsigned int offset, unsigned long config)
{
	struct dr1v90_gpio *gpio = to_dr1v90_gpio(gc);
	unsigned int reg;

	if (pinconf_to_config_param(config) == PIN_CONFIG_INPUT_DEBOUNCE) {
		if (pinconf_to_config_argument(config))
			reg = DR1_GPIO_DEBOUNCE;
		else
			reg = DR1_GPIO_DEBOUNCE_CLR;

		writel(BIT(offset), gpio->base + reg);

		return 0;
	}

	return gpiochip_generic_config(gc, offset, config);
}

static void dr1v90_gpio_irq_ack(struct irq_data *d)
{
	struct dr1v90_gpio *gpio = to_dr1v90_gpio(irq_data_get_irq_chip_data(d));

	writel(BIT(irqd_to_hwirq(d)), gpio->base + DR1_GPIO_EOI);
}

static void dr1v90_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct dr1v90_gpio *gpio = to_dr1v90_gpio(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	writel(BIT(hwirq), gpio->base + DR1_GPIO_INTMASK);
	gpiochip_disable_irq(gc, hwirq);
}

static void dr1v90_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct dr1v90_gpio *gpio = to_dr1v90_gpio(gc);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	gpiochip_enable_irq(gc, hwirq);
	writel(BIT(hwirq), gpio->base + DR1_GPIO_INTMASK_CLR);
}

static void dr1v90_gpio_irq_enable(struct irq_data *d)
{
	struct dr1v90_gpio *gpio = to_dr1v90_gpio(irq_data_get_irq_chip_data(d));

	writel(BIT(irqd_to_hwirq(d)), gpio->base + DR1_GPIO_INTEN);
	dr1v90_gpio_irq_unmask(d);
}

static void dr1v90_gpio_irq_disable(struct irq_data *d)
{
	struct dr1v90_gpio *gpio = to_dr1v90_gpio(irq_data_get_irq_chip_data(d));

	dr1v90_gpio_irq_mask(d);
	writel(BIT(irqd_to_hwirq(d)), gpio->base + DR1_GPIO_INTEN_CLR);
}

static int dr1v90_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct dr1v90_gpio *gpio = to_dr1v90_gpio(irq_data_get_irq_chip_data(d));
	u32 mask = BIT(irqd_to_hwirq(d));

	switch (type) {
	case IRQ_TYPE_EDGE_BOTH:
		writel(mask, gpio->base + DR1_GPIO_BOTHEDGE);
		break;
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_EDGE_FALLING:
		writel(mask, gpio->base + DR1_GPIO_INTTYPE);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
	case IRQ_TYPE_LEVEL_LOW:
		writel(mask, gpio->base + DR1_GPIO_INTTYPE_CLR);
		break;
	default:
		return -EINVAL;
	}

	if (type != IRQ_TYPE_EDGE_BOTH) {
		if (type == IRQ_TYPE_EDGE_RISING || type == IRQ_TYPE_LEVEL_HIGH)
			writel(mask, gpio->base + DR1_GPIO_POLARITY);
		else
			writel(mask, gpio->base + DR1_GPIO_POLARITY_CLR);

		writel(mask, gpio->base + DR1_GPIO_BOTHEDGE_CLR);
	}

	if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(d, handle_level_irq);
	else
		irq_set_handler_locked(d, handle_edge_irq);

	return 0;
}

static int dr1v90_gpio_irq_init_hw(struct gpio_chip *gc)
{
	struct dr1v90_gpio *gpio = to_dr1v90_gpio(gc);

	writel(U32_MAX, gpio->base + DR1_GPIO_INTEN_CLR);
	writel(U32_MAX, gpio->base + DR1_GPIO_INTMASK);
	writel(U32_MAX, gpio->base + DR1_GPIO_BOTHEDGE_CLR);
	writel(U32_MAX, gpio->base + DR1_GPIO_EOI);

	return 0;
}

static const struct irq_chip dr1v90_gpio_irq_chip = {
	.name		= "dr1v90-gpio",
	.irq_ack	= dr1v90_gpio_irq_ack,
	.irq_mask	= dr1v90_gpio_irq_mask,
	.irq_unmask	= dr1v90_gpio_irq_unmask,
	.irq_enable	= dr1v90_gpio_irq_enable,
	.irq_disable	= dr1v90_gpio_irq_disable,
	.irq_set_type	= dr1v90_gpio_irq_set_type,
	.flags		= IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static void dr1v90_gpio_irq_handler(struct irq_desc *desc)
{
	struct dr1v90_gpio *gpio = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned long pending;
	int hwirq;

	chained_irq_enter(chip, desc);

	pending = readl(gpio->base + DR1_GPIO_INTSTATUS);
	for_each_set_bit(hwirq, &pending, gpio->chip.gc.ngpio)
		generic_handle_domain_irq(gpio->chip.gc.irq.domain, hwirq);

	chained_irq_exit(chip, desc);
}

static int dr1v90_gpio_probe(struct platform_device *pdev)
{
	struct gpio_generic_chip_config config;
	struct device *dev = &pdev->dev;
	struct gpio_irq_chip *girq;
	struct reset_control *rst;
	struct dr1v90_gpio *gpio;
	struct gpio_chip *gc;
	u32 ngpios;
	int ret;

	gpio = devm_kzalloc(dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	ret = device_property_read_u32(dev, "ngpios", &ngpios);
	if (ret)
		return dev_err_probe(dev, ret, "Missing GPIO count\n");

	if (!ngpios || ngpios > 32)
		return dev_err_probe(dev, -EINVAL, "Invalid GPIO count\n");

	rst = devm_reset_control_get_shared_deasserted(dev, NULL);
	if (IS_ERR(rst))
		return dev_err_probe(dev, PTR_ERR(rst), "Cannot deassert reset\n");

	gpio->clks[0].id = "bus";
	gpio->clks[1].id = "db";
	ret = devm_clk_bulk_get_enable(dev, ARRAY_SIZE(gpio->clks), gpio->clks);
	if (ret)
		return dev_err_probe(dev, ret, "Cannot enable clocks\n");

	gpio->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gpio->base))
		return PTR_ERR(gpio->base);

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;
	gpio->irq = ret;

	config = (struct gpio_generic_chip_config) {
		.dev = dev,
		.sz = 4,
		.dat = gpio->base + DR1_GPIO_EXT,
		.set = gpio->base + DR1_GPIO_DR,
		.clr = gpio->base + DR1_GPIO_DR_CLR,
		.dirout = gpio->base + DR1_GPIO_DDR,
		.dirin = gpio->base + DR1_GPIO_DDR_CLR,
		.flags = GPIO_GENERIC_PINCTRL_BACKEND,
	};

	ret = gpio_generic_chip_init(&gpio->chip, &config);
	if (ret)
		return ret;

	gc = &gpio->chip.gc;
	gc->set_config = dr1v90_gpio_set_config;
	gc->ngpio = ngpios;

	girq = &gc->irq;
	girq->handler = handle_bad_irq;
	girq->default_type = IRQ_TYPE_NONE;
	girq->init_hw = dr1v90_gpio_irq_init_hw;
	girq->num_parents = 1;
	girq->parents = &gpio->irq;
	girq->parent_handler = dr1v90_gpio_irq_handler;
	girq->parent_handler_data = gpio;

	gpio_irq_chip_set_chip(girq, &dr1v90_gpio_irq_chip);

	return devm_gpiochip_add_data(dev, gc, gpio);
}

static const struct of_device_id dr1v90_gpio_of_match[] = {
	{ .compatible = "anlogic,dr1v90-gpio" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dr1v90_gpio_of_match);

static struct platform_driver dr1v90_gpio_driver = {
	.probe	= dr1v90_gpio_probe,
	.driver	= {
		.name		= "dr1v90-gpio",
		.of_match_table	= dr1v90_gpio_of_match,
	},
};
module_platform_driver(dr1v90_gpio_driver);

MODULE_DESCRIPTION("Anlogic DR1V90 GPIO controller driver");
MODULE_AUTHOR("Junhui Liu <junhui.liu@pigmoral.tech>");
MODULE_LICENSE("GPL");
