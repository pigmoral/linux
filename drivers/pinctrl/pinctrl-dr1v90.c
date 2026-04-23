// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2024 Anlogic, Inc.
 * Copyright (C) 2026 Junhui Liu <junhui.liu@pigmoral.tech>
 */

#include <linux/array_size.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <linux/pinctrl/pinconf.h>
#include <linux/pinctrl/pinconf-generic.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>

#include "pinconf.h"
#include "pinctrl-utils.h"

#define DR1_PIN_MUX_OFFSET		0x000
#define DR1_PIN_MUX_REG(i)		(DR1_PIN_MUX_OFFSET + (i) * 4)
#define  DR1_PIN_MUX_FUNCSEL_MASK	GENMASK(3, 0)

#define DR1_PIN_EMIOSEL_OFFSET		0x400
#define DR1_PIN_EMIOSEL_REG(i)		(DR1_PIN_EMIOSEL_OFFSET + (i) * 4)

#define DR1_PIN_CFG_OFFSET		0x800
#define DR1_PIN_CFG_PARA0_REG(i)	(DR1_PIN_CFG_OFFSET + (i) * 8)
#define  DR1_PIN_CFG_PULL_MASK		GENMASK(31, 28)
#define  DR1_PIN_CFG_PULL_UP		BIT(31)
#define  DR1_PIN_CFG_PULL_DOWN		BIT(30)
#define  DR1_PIN_CFG_PULL_UP_10K		BIT(29)
#define  DR1_PIN_CFG_PULL_DOWN_10K	BIT(28)
#define  DR1_PIN_CFG_SPEED_MASK		GENMASK(2, 0)
#define  DR1_PIN_CFG_SPEED_SLOW		0x0
#define  DR1_PIN_CFG_SPEED_MEDIUM	0x2
#define  DR1_PIN_CFG_SPEED_FAST		0x7
#define DR1_PIN_CFG_PARA1_REG(i)	(DR1_PIN_CFG_OFFSET + (i) * 8 + 4)
#define  DR1_PIN_CFG_SINK_MASK		GENMASK(20, 16)
#define  DR1_PIN_CFG_SOURCE_MASK		GENMASK(12, 8)
#define  DR1_PIN_CFG_INPUT_MASK		GENMASK(1, 0)
#define  DR1_PIN_CFG_INPUT_1V8		0x2
#define  DR1_PIN_CFG_INPUT_3V3_2V5	0x3

#define DR1_PULL_OHM			25000
#define DR1_PULL_STRONG_OHM		10000

static const u8 dr1_pin_cfg_speed[] = {
	DR1_PIN_CFG_SPEED_SLOW,
	DR1_PIN_CFG_SPEED_MEDIUM,
	DR1_PIN_CFG_SPEED_FAST,
};

static const u8 dr1_pin_drive_strength[] = { 4, 8, 12, 16, 20 };

enum dr1v90_io_voltage {
	DR1_IO_VOLTAGE_1V8,
	DR1_IO_VOLTAGE_2V5,
	DR1_IO_VOLTAGE_3V3,
};

static const u8 dr1_pin_drive_source[][5] = {
	[DR1_IO_VOLTAGE_1V8] = { 0x09, 0x06, 0x18, 0x15, 0x17 },
	[DR1_IO_VOLTAGE_2V5] = { 0x09, 0x0b, 0x1b, 0x17, 0x11 },
	[DR1_IO_VOLTAGE_3V3] = { 0x0b, 0x01, 0x17, 0x12, 0x13 },
};

static const u8 dr1_pin_drive_sink[] = { 0x0a, 0x1e, 0x15, 0x17, 0x12 };

struct dr1v90_pinctrl {
	struct pinctrl_dev *pctldev;
	struct pinctrl_desc desc;

	void __iomem *base;

	struct regulator *vccio200;
	struct regulator *vccio201;
};

struct dr1v90_pinctrl_function {
	struct pinfunction func;

	u8 funcsel;

	bool emio_supported;
	unsigned int emio_sel_index;
};

static const struct pinctrl_pin_desc dr1v90_pins[] = {
	PINCTRL_PIN(0, "MIO0"),	  PINCTRL_PIN(1, "MIO1"),   PINCTRL_PIN(2, "MIO2"),
	PINCTRL_PIN(3, "MIO3"),	  PINCTRL_PIN(4, "MIO4"),   PINCTRL_PIN(5, "MIO5"),
	PINCTRL_PIN(6, "MIO6"),	  PINCTRL_PIN(7, "MIO7"),   PINCTRL_PIN(8, "MIO8"),
	PINCTRL_PIN(9, "MIO9"),	  PINCTRL_PIN(10, "MIO10"), PINCTRL_PIN(11, "MIO11"),
	PINCTRL_PIN(12, "MIO12"), PINCTRL_PIN(13, "MIO13"), PINCTRL_PIN(14, "MIO14"),
	PINCTRL_PIN(15, "MIO15"), PINCTRL_PIN(16, "MIO16"), PINCTRL_PIN(17, "MIO17"),
	PINCTRL_PIN(18, "MIO18"), PINCTRL_PIN(19, "MIO19"), PINCTRL_PIN(20, "MIO20"),
	PINCTRL_PIN(21, "MIO21"), PINCTRL_PIN(22, "MIO22"), PINCTRL_PIN(23, "MIO23"),
	PINCTRL_PIN(24, "MIO24"), PINCTRL_PIN(25, "MIO25"), PINCTRL_PIN(26, "MIO26"),
	PINCTRL_PIN(27, "MIO27"), PINCTRL_PIN(28, "MIO28"), PINCTRL_PIN(29, "MIO29"),
	PINCTRL_PIN(30, "MIO30"), PINCTRL_PIN(31, "MIO31"), PINCTRL_PIN(32, "MIO32"),
	PINCTRL_PIN(33, "MIO33"), PINCTRL_PIN(34, "MIO34"), PINCTRL_PIN(35, "MIO35"),
	PINCTRL_PIN(36, "MIO36"), PINCTRL_PIN(37, "MIO37"), PINCTRL_PIN(38, "MIO38"),
	PINCTRL_PIN(39, "MIO39"), PINCTRL_PIN(40, "MIO40"), PINCTRL_PIN(41, "MIO41"),
	PINCTRL_PIN(42, "MIO42"), PINCTRL_PIN(43, "MIO43"), PINCTRL_PIN(44, "MIO44"),
	PINCTRL_PIN(45, "MIO45"), PINCTRL_PIN(46, "MIO46"), PINCTRL_PIN(47, "MIO47"),
	PINCTRL_PIN(48, "MIO48"), PINCTRL_PIN(49, "MIO49"), PINCTRL_PIN(50, "MIO50"),
	PINCTRL_PIN(51, "MIO51"), PINCTRL_PIN(52, "MIO52"), PINCTRL_PIN(53, "MIO53"),
};

#define DR1V90_PINCTRL_GROUP(_name, ...) \
	PINCTRL_PINGROUP(#_name "_grp", ((const unsigned int[]){ __VA_ARGS__ }), \
			 ARRAY_SIZE(((const unsigned int[]){ __VA_ARGS__ })))

static const struct pingroup dr1v90_pinctrl_groups[] = {
	DR1V90_PINCTRL_GROUP(qspi0_0, 1, 2, 3, 4, 5, 6),
	DR1V90_PINCTRL_GROUP(i2c0_0, 10, 11),
	DR1V90_PINCTRL_GROUP(i2c0_1, 14, 15),
	DR1V90_PINCTRL_GROUP(i2c0_2, 18, 19),
	DR1V90_PINCTRL_GROUP(i2c0_3, 22, 23),
	DR1V90_PINCTRL_GROUP(i2c0_4, 26, 27),
	DR1V90_PINCTRL_GROUP(i2c0_5, 30, 31),
	DR1V90_PINCTRL_GROUP(i2c0_6, 34, 35),
	DR1V90_PINCTRL_GROUP(i2c0_7, 38, 39),
	DR1V90_PINCTRL_GROUP(i2c0_8, 42, 43),
	DR1V90_PINCTRL_GROUP(i2c0_9, 46, 47),
	DR1V90_PINCTRL_GROUP(i2c0_10, 50, 51),
	DR1V90_PINCTRL_GROUP(i2c0_emio),
	DR1V90_PINCTRL_GROUP(i2c1_0, 12, 13),
	DR1V90_PINCTRL_GROUP(i2c1_1, 16, 17),
	DR1V90_PINCTRL_GROUP(i2c1_2, 20, 21),
	DR1V90_PINCTRL_GROUP(i2c1_3, 24, 25),
	DR1V90_PINCTRL_GROUP(i2c1_4, 28, 29),
	DR1V90_PINCTRL_GROUP(i2c1_5, 32, 33),
	DR1V90_PINCTRL_GROUP(i2c1_6, 36, 37),
	DR1V90_PINCTRL_GROUP(i2c1_7, 40, 41),
	DR1V90_PINCTRL_GROUP(i2c1_8, 44, 45),
	DR1V90_PINCTRL_GROUP(i2c1_9, 48, 49),
	DR1V90_PINCTRL_GROUP(i2c1_10, 52, 53),
	DR1V90_PINCTRL_GROUP(i2c1_emio),
	DR1V90_PINCTRL_GROUP(can0_0, 10, 11),
	DR1V90_PINCTRL_GROUP(can0_1, 14, 15),
	DR1V90_PINCTRL_GROUP(can0_2, 18, 19),
	DR1V90_PINCTRL_GROUP(can0_3, 22, 23),
	DR1V90_PINCTRL_GROUP(can0_4, 26, 27),
	DR1V90_PINCTRL_GROUP(can0_5, 30, 31),
	DR1V90_PINCTRL_GROUP(can0_6, 34, 35),
	DR1V90_PINCTRL_GROUP(can0_7, 38, 39),
	DR1V90_PINCTRL_GROUP(can0_8, 42, 43),
	DR1V90_PINCTRL_GROUP(can0_9, 46, 47),
	DR1V90_PINCTRL_GROUP(can0_10, 50, 51),
	DR1V90_PINCTRL_GROUP(can0_emio),
	DR1V90_PINCTRL_GROUP(can1_0, 8, 9),
	DR1V90_PINCTRL_GROUP(can1_1, 12, 13),
	DR1V90_PINCTRL_GROUP(can1_2, 16, 17),
	DR1V90_PINCTRL_GROUP(can1_3, 20, 21),
	DR1V90_PINCTRL_GROUP(can1_4, 24, 25),
	DR1V90_PINCTRL_GROUP(can1_5, 28, 29),
	DR1V90_PINCTRL_GROUP(can1_6, 32, 33),
	DR1V90_PINCTRL_GROUP(can1_7, 36, 37),
	DR1V90_PINCTRL_GROUP(can1_8, 40, 41),
	DR1V90_PINCTRL_GROUP(can1_9, 44, 45),
	DR1V90_PINCTRL_GROUP(can1_10, 48, 49),
	DR1V90_PINCTRL_GROUP(can1_11, 52, 53),
	DR1V90_PINCTRL_GROUP(can1_emio),
	DR1V90_PINCTRL_GROUP(smc0_nand8, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14),
	DR1V90_PINCTRL_GROUP(smc0_nand16, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
					  16, 17, 18, 19, 20, 21, 22, 23),
	DR1V90_PINCTRL_GROUP(uart0_0, 10, 11),
	DR1V90_PINCTRL_GROUP(uart0_1, 14, 15),
	DR1V90_PINCTRL_GROUP(uart0_2, 18, 19),
	DR1V90_PINCTRL_GROUP(uart0_3, 22, 23),
	DR1V90_PINCTRL_GROUP(uart0_4, 26, 27),
	DR1V90_PINCTRL_GROUP(uart0_5, 30, 31),
	DR1V90_PINCTRL_GROUP(uart0_6, 34, 35),
	DR1V90_PINCTRL_GROUP(uart0_7, 38, 39),
	DR1V90_PINCTRL_GROUP(uart0_8, 42, 43),
	DR1V90_PINCTRL_GROUP(uart0_9, 46, 47),
	DR1V90_PINCTRL_GROUP(uart0_10, 50, 51),
	DR1V90_PINCTRL_GROUP(uart0_emio),
	DR1V90_PINCTRL_GROUP(uart1_0, 8, 9),
	DR1V90_PINCTRL_GROUP(uart1_1, 12, 13),
	DR1V90_PINCTRL_GROUP(uart1_2, 16, 17),
	DR1V90_PINCTRL_GROUP(uart1_3, 20, 21),
	DR1V90_PINCTRL_GROUP(uart1_4, 24, 25),
	DR1V90_PINCTRL_GROUP(uart1_5, 28, 29),
	DR1V90_PINCTRL_GROUP(uart1_6, 32, 33),
	DR1V90_PINCTRL_GROUP(uart1_7, 36, 37),
	DR1V90_PINCTRL_GROUP(uart1_8, 40, 41),
	DR1V90_PINCTRL_GROUP(uart1_9, 44, 45),
	DR1V90_PINCTRL_GROUP(uart1_10, 48, 49),
	DR1V90_PINCTRL_GROUP(uart1_11, 52, 53),
	DR1V90_PINCTRL_GROUP(uart1_emio),
	DR1V90_PINCTRL_GROUP(swdt0_0, 14, 15),
	DR1V90_PINCTRL_GROUP(swdt0_1, 26, 27),
	DR1V90_PINCTRL_GROUP(swdt0_2, 38, 39),
	DR1V90_PINCTRL_GROUP(swdt0_3, 50, 51),
	DR1V90_PINCTRL_GROUP(swdt0_4, 52, 53),
	DR1V90_PINCTRL_GROUP(swdt0_emio),
	DR1V90_PINCTRL_GROUP(ttc0_0, 18, 19),
	DR1V90_PINCTRL_GROUP(ttc0_1, 30, 31),
	DR1V90_PINCTRL_GROUP(ttc0_2, 42, 43),
	DR1V90_PINCTRL_GROUP(ttc0_emio),
	DR1V90_PINCTRL_GROUP(ttc1_0, 16, 17),
	DR1V90_PINCTRL_GROUP(ttc1_1, 28, 29),
	DR1V90_PINCTRL_GROUP(ttc1_2, 40, 41),
	DR1V90_PINCTRL_GROUP(ttc1_emio),
	DR1V90_PINCTRL_GROUP(spi0_0_ss0, 16, 17, 21, 18),
	DR1V90_PINCTRL_GROUP(spi0_0_ss1, 16, 17, 21, 19),
	DR1V90_PINCTRL_GROUP(spi0_0_ss2, 16, 17, 21, 20),
	DR1V90_PINCTRL_GROUP(spi0_1_ss0, 28, 29, 33, 30),
	DR1V90_PINCTRL_GROUP(spi0_1_ss1, 28, 29, 33, 31),
	DR1V90_PINCTRL_GROUP(spi0_1_ss2, 28, 29, 33, 32),
	DR1V90_PINCTRL_GROUP(spi0_2_ss0, 40, 41, 45, 42),
	DR1V90_PINCTRL_GROUP(spi0_2_ss1, 40, 41, 45, 43),
	DR1V90_PINCTRL_GROUP(spi0_2_ss2, 40, 41, 45, 44),
	DR1V90_PINCTRL_GROUP(spi0_emio),
	DR1V90_PINCTRL_GROUP(spi1_0_ss0, 10, 11, 12, 13),
	DR1V90_PINCTRL_GROUP(spi1_0_ss1, 10, 11, 12, 14),
	DR1V90_PINCTRL_GROUP(spi1_0_ss2, 10, 11, 12, 15),
	DR1V90_PINCTRL_GROUP(spi1_1_ss0, 22, 23, 24, 25),
	DR1V90_PINCTRL_GROUP(spi1_1_ss1, 22, 23, 24, 26),
	DR1V90_PINCTRL_GROUP(spi1_1_ss2, 22, 23, 24, 27),
	DR1V90_PINCTRL_GROUP(spi1_2_ss0, 34, 35, 36, 37),
	DR1V90_PINCTRL_GROUP(spi1_2_ss1, 34, 35, 36, 38),
	DR1V90_PINCTRL_GROUP(spi1_2_ss2, 34, 35, 36, 39),
	DR1V90_PINCTRL_GROUP(spi1_3_ss0, 46, 47, 48, 49),
	DR1V90_PINCTRL_GROUP(spi1_3_ss1, 46, 47, 48, 50),
	DR1V90_PINCTRL_GROUP(spi1_3_ss2, 46, 47, 48, 51),
	DR1V90_PINCTRL_GROUP(spi1_emio),
	DR1V90_PINCTRL_GROUP(sdio0_0, 16, 17, 18, 19, 20, 21),
	DR1V90_PINCTRL_GROUP(sdio0_1, 28, 29, 30, 31, 32, 33),
	DR1V90_PINCTRL_GROUP(sdio0_2, 40, 41, 42, 43, 44, 45),
	DR1V90_PINCTRL_GROUP(sdio0_emio),
	DR1V90_PINCTRL_GROUP(sdio1_0, 10, 11, 12, 13, 14, 15),
	DR1V90_PINCTRL_GROUP(sdio1_1, 22, 23, 24, 25, 26, 27),
	DR1V90_PINCTRL_GROUP(sdio1_2, 34, 35, 36, 37, 38, 39),
	DR1V90_PINCTRL_GROUP(sdio1_3, 46, 47, 48, 49, 50, 51),
	DR1V90_PINCTRL_GROUP(sdio1_emio),
	DR1V90_PINCTRL_GROUP(ethernet0_0, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27),
	DR1V90_PINCTRL_GROUP(ethernet1_0, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39),
	DR1V90_PINCTRL_GROUP(mdio0_0, 52, 53),
	DR1V90_PINCTRL_GROUP(mdio0_emio),
	DR1V90_PINCTRL_GROUP(mdio1_0, 52, 53),
	DR1V90_PINCTRL_GROUP(mdio1_emio),
	DR1V90_PINCTRL_GROUP(usb0_0, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39),
	DR1V90_PINCTRL_GROUP(usb1_0, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51),
	DR1V90_PINCTRL_GROUP(gpio_0, 0),
	DR1V90_PINCTRL_GROUP(gpio_1, 1),
	DR1V90_PINCTRL_GROUP(gpio_2, 2),
	DR1V90_PINCTRL_GROUP(gpio_3, 3),
	DR1V90_PINCTRL_GROUP(gpio_4, 4),
	DR1V90_PINCTRL_GROUP(gpio_5, 5),
	DR1V90_PINCTRL_GROUP(gpio_6, 6),
	DR1V90_PINCTRL_GROUP(gpio_7, 7),
	DR1V90_PINCTRL_GROUP(gpio_8, 8),
	DR1V90_PINCTRL_GROUP(gpio_9, 9),
	DR1V90_PINCTRL_GROUP(gpio_10, 10),
	DR1V90_PINCTRL_GROUP(gpio_11, 11),
	DR1V90_PINCTRL_GROUP(gpio_12, 12),
	DR1V90_PINCTRL_GROUP(gpio_13, 13),
	DR1V90_PINCTRL_GROUP(gpio_14, 14),
	DR1V90_PINCTRL_GROUP(gpio_15, 15),
	DR1V90_PINCTRL_GROUP(gpio_16, 16),
	DR1V90_PINCTRL_GROUP(gpio_17, 17),
	DR1V90_PINCTRL_GROUP(gpio_18, 18),
	DR1V90_PINCTRL_GROUP(gpio_19, 19),
	DR1V90_PINCTRL_GROUP(gpio_20, 20),
	DR1V90_PINCTRL_GROUP(gpio_21, 21),
	DR1V90_PINCTRL_GROUP(gpio_22, 22),
	DR1V90_PINCTRL_GROUP(gpio_23, 23),
	DR1V90_PINCTRL_GROUP(gpio_24, 24),
	DR1V90_PINCTRL_GROUP(gpio_25, 25),
	DR1V90_PINCTRL_GROUP(gpio_26, 26),
	DR1V90_PINCTRL_GROUP(gpio_27, 27),
	DR1V90_PINCTRL_GROUP(gpio_28, 28),
	DR1V90_PINCTRL_GROUP(gpio_29, 29),
	DR1V90_PINCTRL_GROUP(gpio_30, 30),
	DR1V90_PINCTRL_GROUP(gpio_31, 31),
	DR1V90_PINCTRL_GROUP(gpio_32, 32),
	DR1V90_PINCTRL_GROUP(gpio_33, 33),
	DR1V90_PINCTRL_GROUP(gpio_34, 34),
	DR1V90_PINCTRL_GROUP(gpio_35, 35),
	DR1V90_PINCTRL_GROUP(gpio_36, 36),
	DR1V90_PINCTRL_GROUP(gpio_37, 37),
	DR1V90_PINCTRL_GROUP(gpio_38, 38),
	DR1V90_PINCTRL_GROUP(gpio_39, 39),
	DR1V90_PINCTRL_GROUP(gpio_40, 40),
	DR1V90_PINCTRL_GROUP(gpio_41, 41),
	DR1V90_PINCTRL_GROUP(gpio_42, 42),
	DR1V90_PINCTRL_GROUP(gpio_43, 43),
	DR1V90_PINCTRL_GROUP(gpio_44, 44),
	DR1V90_PINCTRL_GROUP(gpio_45, 45),
	DR1V90_PINCTRL_GROUP(gpio_46, 46),
	DR1V90_PINCTRL_GROUP(gpio_47, 47),
	DR1V90_PINCTRL_GROUP(gpio_48, 48),
	DR1V90_PINCTRL_GROUP(gpio_49, 49),
	DR1V90_PINCTRL_GROUP(gpio_50, 50),
	DR1V90_PINCTRL_GROUP(gpio_51, 51),
	DR1V90_PINCTRL_GROUP(gpio_52, 52),
	DR1V90_PINCTRL_GROUP(gpio_53, 53),
};

static const char *const qspi0_groups[] = {
	"qspi0_0_grp",
};
static const char *const i2c0_groups[] = {
	"i2c0_0_grp", "i2c0_1_grp", "i2c0_2_grp", "i2c0_3_grp", "i2c0_4_grp",  "i2c0_5_grp",
	"i2c0_6_grp", "i2c0_7_grp", "i2c0_8_grp", "i2c0_9_grp", "i2c0_10_grp", "i2c0_emio_grp"
};
static const char *const i2c1_groups[] = {
	"i2c1_0_grp", "i2c1_1_grp", "i2c1_2_grp", "i2c1_3_grp", "i2c1_4_grp",  "i2c1_5_grp",
	"i2c1_6_grp", "i2c1_7_grp", "i2c1_8_grp", "i2c1_9_grp", "i2c1_10_grp", "i2c1_emio_grp"
};
static const char *const can0_groups[] = {
	"can0_0_grp", "can0_1_grp", "can0_2_grp", "can0_3_grp", "can0_4_grp",  "can0_5_grp",
	"can0_6_grp", "can0_7_grp", "can0_8_grp", "can0_9_grp", "can0_10_grp", "can0_emio_grp"
};
static const char *const can1_groups[] = {
	"can1_0_grp", "can1_1_grp", "can1_2_grp", "can1_3_grp", "can1_4_grp",  "can1_5_grp",
	"can1_6_grp", "can1_7_grp", "can1_8_grp", "can1_9_grp", "can1_10_grp", "can1_11_grp",
	"can1_emio_grp"
};
static const char *const smc0_groups[] = {
	"smc0_nand8_grp", "smc0_nand16_grp"
};
static const char *const uart0_groups[] = {
	"uart0_0_grp", "uart0_1_grp", "uart0_2_grp", "uart0_3_grp", "uart0_4_grp",  "uart0_5_grp",
	"uart0_6_grp", "uart0_7_grp", "uart0_8_grp", "uart0_9_grp", "uart0_10_grp", "uart0_emio_grp"
};
static const char *const uart1_groups[] = {
	"uart1_0_grp", "uart1_1_grp", "uart1_2_grp", "uart1_3_grp", "uart1_4_grp",  "uart1_5_grp",
	"uart1_6_grp", "uart1_7_grp", "uart1_8_grp", "uart1_9_grp", "uart1_10_grp", "uart1_11_grp",
	"uart1_emio_grp"
};
static const char *const gpio_groups[] = {
	"gpio_0_grp",  "gpio_1_grp",  "gpio_2_grp",  "gpio_3_grp",  "gpio_4_grp",  "gpio_5_grp",
	"gpio_6_grp",  "gpio_7_grp",  "gpio_8_grp",  "gpio_9_grp",  "gpio_10_grp", "gpio_11_grp",
	"gpio_12_grp", "gpio_13_grp", "gpio_14_grp", "gpio_15_grp", "gpio_16_grp", "gpio_17_grp",
	"gpio_18_grp", "gpio_19_grp", "gpio_20_grp", "gpio_21_grp", "gpio_22_grp", "gpio_23_grp",
	"gpio_24_grp", "gpio_25_grp", "gpio_26_grp", "gpio_27_grp", "gpio_28_grp", "gpio_29_grp",
	"gpio_30_grp", "gpio_31_grp", "gpio_32_grp", "gpio_33_grp", "gpio_34_grp", "gpio_35_grp",
	"gpio_36_grp", "gpio_37_grp", "gpio_38_grp", "gpio_39_grp", "gpio_40_grp", "gpio_41_grp",
	"gpio_42_grp", "gpio_43_grp", "gpio_44_grp", "gpio_45_grp", "gpio_46_grp", "gpio_47_grp",
	"gpio_48_grp", "gpio_49_grp", "gpio_50_grp", "gpio_51_grp", "gpio_52_grp", "gpio_53_grp"
};
static const char *const swdt0_groups[] = {
	"swdt0_0_grp", "swdt0_1_grp", "swdt0_2_grp", "swdt0_3_grp", "swdt0_4_grp", "swdt0_emio_grp"
};
static const char *const ttc0_groups[] = {
	"ttc0_0_grp", "ttc0_1_grp", "ttc0_2_grp", "ttc0_emio_grp"
};
static const char *const ttc1_groups[] = {
	"ttc1_0_grp", "ttc1_1_grp", "ttc1_2_grp", "ttc1_emio_grp"
};
static const char *const spi0_groups[] = {
	"spi0_0_ss0_grp", "spi0_0_ss1_grp", "spi0_0_ss2_grp", "spi0_1_ss0_grp", "spi0_1_ss1_grp",
	"spi0_1_ss2_grp", "spi0_2_ss0_grp", "spi0_2_ss1_grp", "spi0_2_ss2_grp", "spi0_emio_grp"
};
static const char *const spi1_groups[] = {
	"spi1_0_ss0_grp", "spi1_0_ss1_grp", "spi1_0_ss2_grp", "spi1_1_ss0_grp", "spi1_1_ss1_grp",
	"spi1_1_ss2_grp", "spi1_2_ss0_grp", "spi1_2_ss1_grp", "spi1_2_ss2_grp",
	"spi1_3_ss0_grp", "spi1_3_ss1_grp", "spi1_3_ss2_grp", "spi1_emio_grp"
};
static const char *const sdio0_groups[] = {
	"sdio0_0_grp", "sdio0_1_grp", "sdio0_2_grp", "sdio0_emio_grp"
};
static const char *const sdio1_groups[] = {
	"sdio1_0_grp", "sdio1_1_grp", "sdio1_2_grp", "sdio1_3_grp", "sdio1_emio_grp"
};
static const char *const ethernet0_groups[] = {
	"ethernet0_0_grp"
};
static const char *const ethernet1_groups[] = {
	"ethernet1_0_grp"
};
static const char *const mdio0_groups[] = {
	"mdio0_0_grp", "mdio0_emio_grp"
};
static const char *const mdio1_groups[] = {
	"mdio1_0_grp", "mdio1_emio_grp"
};
static const char *const usb0_groups[] = {
	"usb0_0_grp"
};
static const char *const usb1_groups[] = {
	"usb1_0_grp"
};

#define _DR1V90_PINCTRL_FUNCTION(_name, _funcsel, _emio_supported, _emio_sel_index) \
	{ \
		.func = PINCTRL_PINFUNCTION(#_name, _name##_groups, ARRAY_SIZE(_name##_groups)), \
		.funcsel = _funcsel, \
		.emio_supported = _emio_supported, \
		.emio_sel_index = _emio_sel_index, \
	}
#define DR1V90_PINCTRL_FUNCTION(_name, _funcsel) \
	_DR1V90_PINCTRL_FUNCTION(_name, _funcsel, false, 0)
#define DR1V90_PINCTRL_FUNCTION_WITH_EMIO(_name, _funcsel, _emio_sel_index) \
	_DR1V90_PINCTRL_FUNCTION(_name, _funcsel, true, _emio_sel_index)

static const struct dr1v90_pinctrl_function dr1v90_pinctrl_functions[] = {
	DR1V90_PINCTRL_FUNCTION(qspi0, 0),
	DR1V90_PINCTRL_FUNCTION_WITH_EMIO(i2c0, 0, 0),
	DR1V90_PINCTRL_FUNCTION_WITH_EMIO(i2c1, 0, 1),
	DR1V90_PINCTRL_FUNCTION_WITH_EMIO(can0, 1, 2),
	DR1V90_PINCTRL_FUNCTION_WITH_EMIO(can1, 1, 3),
	DR1V90_PINCTRL_FUNCTION(smc0, 2),
	DR1V90_PINCTRL_FUNCTION_WITH_EMIO(uart0, 3, 4),
	DR1V90_PINCTRL_FUNCTION_WITH_EMIO(uart1, 3, 5),
	DR1V90_PINCTRL_FUNCTION(gpio, 4),
	DR1V90_PINCTRL_FUNCTION_WITH_EMIO(swdt0, 7, 8),
	DR1V90_PINCTRL_FUNCTION_WITH_EMIO(ttc0, 7, 6),
	DR1V90_PINCTRL_FUNCTION_WITH_EMIO(ttc1, 7, 7),
	DR1V90_PINCTRL_FUNCTION_WITH_EMIO(spi0, 9, 9),
	DR1V90_PINCTRL_FUNCTION_WITH_EMIO(spi1, 9, 10),
	DR1V90_PINCTRL_FUNCTION_WITH_EMIO(sdio0, 10, 11),
	DR1V90_PINCTRL_FUNCTION_WITH_EMIO(sdio1, 10, 12),
	DR1V90_PINCTRL_FUNCTION(ethernet0, 14),
	DR1V90_PINCTRL_FUNCTION(ethernet1, 14),
	DR1V90_PINCTRL_FUNCTION_WITH_EMIO(mdio0, 14, 13),
	DR1V90_PINCTRL_FUNCTION_WITH_EMIO(mdio1, 15, 14),
	DR1V90_PINCTRL_FUNCTION(usb0, 15),
	DR1V90_PINCTRL_FUNCTION(usb1, 15),
};

static int dr1v90_get_groups_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(dr1v90_pinctrl_groups);
}

static const char *dr1v90_get_group_name(struct pinctrl_dev *pctldev, unsigned int selector)
{
	return dr1v90_pinctrl_groups[selector].name;
}

static int dr1v90_get_group_pins(struct pinctrl_dev *pctldev, unsigned int selector,
				 const unsigned int **pins, unsigned int *num_pins)
{
	*pins = dr1v90_pinctrl_groups[selector].pins;
	*num_pins = dr1v90_pinctrl_groups[selector].npins;

	return 0;
}

static void dr1v90_pin_dbg_show(struct pinctrl_dev *pctldev, struct seq_file *s,
				unsigned int offset)
{
	struct dr1v90_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	u32 para0, para1, funcsel;

	funcsel = FIELD_GET(DR1_PIN_MUX_FUNCSEL_MASK, readl_relaxed(pctrl->base + DR1_PIN_MUX_REG(offset)));

	para0 = readl_relaxed(pctrl->base + DR1_PIN_CFG_PARA0_REG(offset));
	para1 = readl_relaxed(pctrl->base + DR1_PIN_CFG_PARA1_REG(offset));

	seq_printf(s, "funcsel = %d, para0 = 0x%08x, para1 = 0x%08x", funcsel, para0, para1);
}

static const struct pinctrl_ops dr1v90_pinctrl_ops = {
	.get_groups_count = dr1v90_get_groups_count,
	.get_group_name = dr1v90_get_group_name,
	.get_group_pins = dr1v90_get_group_pins,
	.pin_dbg_show = dr1v90_pin_dbg_show,
	.dt_node_to_map = pinconf_generic_dt_node_to_map_all,
	.dt_free_map = pinctrl_utils_free_map,
};

static int dr1v90_get_functions_count(struct pinctrl_dev *pctldev)
{
	return ARRAY_SIZE(dr1v90_pinctrl_functions);
}

static const char *dr1v90_get_function_name(struct pinctrl_dev *pctldev, unsigned int selector)
{
	return dr1v90_pinctrl_functions[selector].func.name;
}

static int dr1v90_get_function_groups(struct pinctrl_dev *pctldev, unsigned int selector,
				      const char *const **groups, unsigned int *num_groups)
{
	*groups = dr1v90_pinctrl_functions[selector].func.groups;
	*num_groups = dr1v90_pinctrl_functions[selector].func.ngroups;

	return 0;
}

static int dr1v90_set_mux(struct pinctrl_dev *pctldev, unsigned int func_selector,
			  unsigned int group_selector)
{
	const struct dr1v90_pinctrl_function *func = &dr1v90_pinctrl_functions[func_selector];
	const struct pingroup *group = &dr1v90_pinctrl_groups[group_selector];
	struct dr1v90_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	int i;

	for (i = 0; i < group->npins; i++) {
		unsigned int pin = group->pins[i];
		u32 value = readl_relaxed(pctrl->base + DR1_PIN_MUX_REG(pin));

		value &= ~DR1_PIN_MUX_FUNCSEL_MASK;
		value |= FIELD_PREP(DR1_PIN_MUX_FUNCSEL_MASK, func->funcsel);

		writel_relaxed(value, pctrl->base + DR1_PIN_MUX_REG(pin));
	}

	if (!func->emio_supported)
		return 0;

	/*
	 * Set EMIO selection based on whether the group has assigned pins.
	 *	0 for EMIO, 1 for MIO
	 */
	if (group->npins)
		writel_relaxed(1, pctrl->base + DR1_PIN_EMIOSEL_REG(func->emio_sel_index));
	else
		writel_relaxed(0, pctrl->base + DR1_PIN_EMIOSEL_REG(func->emio_sel_index));

	return 0;
}

static int dr1v90_pin_voltage(struct dr1v90_pinctrl *pctrl, unsigned int pin)
{
	struct regulator *supply;
	int voltage;

	/* Bank200 covers MIO0-15, Bank201 covers MIO16-53. */
	supply = pin < 16 ? pctrl->vccio200 : pctrl->vccio201;
	voltage = regulator_get_voltage(supply);
	if (voltage < 0)
		return voltage;

	switch (voltage) {
	case 1800000:
		return DR1_IO_VOLTAGE_1V8;
	case 2500000:
		return DR1_IO_VOLTAGE_2V5;
	case 3300000:
		return DR1_IO_VOLTAGE_3V3;
	default:
		return -EINVAL;
	}
}

static int dr1v90_gpio_request_enable(struct pinctrl_dev *pctldev,
				      struct pinctrl_gpio_range *range, unsigned int pin)
{
	struct dr1v90_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	int voltage;
	u32 value;

	voltage = dr1v90_pin_voltage(pctrl, pin);
	if (voltage < 0)
		return voltage;

	value = readl_relaxed(pctrl->base + DR1_PIN_CFG_PARA1_REG(pin));
	value &= ~DR1_PIN_CFG_INPUT_MASK;
	value |= FIELD_PREP(DR1_PIN_CFG_INPUT_MASK, voltage == DR1_IO_VOLTAGE_1V8 ?
			    DR1_PIN_CFG_INPUT_1V8 : DR1_PIN_CFG_INPUT_3V3_2V5);
	writel_relaxed(value, pctrl->base + DR1_PIN_CFG_PARA1_REG(pin));

	value = readl_relaxed(pctrl->base + DR1_PIN_MUX_REG(pin));
	value &= ~DR1_PIN_MUX_FUNCSEL_MASK;
	value |= FIELD_PREP(DR1_PIN_MUX_FUNCSEL_MASK, 4);
	writel_relaxed(value, pctrl->base + DR1_PIN_MUX_REG(pin));

	return 0;
}

static const struct pinmux_ops dr1v90_pinmux_ops = {
	.get_functions_count = dr1v90_get_functions_count,
	.get_function_name = dr1v90_get_function_name,
	.get_function_groups = dr1v90_get_function_groups,
	.set_mux = dr1v90_set_mux,
	.gpio_request_enable = dr1v90_gpio_request_enable,
	.strict = true,
};

static u32 dr1v90_pin_drive_config(unsigned int voltage, unsigned int index)
{
	u32 source = dr1_pin_drive_source[voltage][index];
	u32 sink = dr1_pin_drive_sink[index];

	return FIELD_PREP(DR1_PIN_CFG_SOURCE_MASK, source) |
	       FIELD_PREP(DR1_PIN_CFG_SINK_MASK, sink);
}

static int dr1v90_pin_config_get(struct pinctrl_dev *pctldev, unsigned int pin,
				 unsigned long *config)
{
	struct dr1v90_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	enum pin_config_param param;
	bool enabled = false;
	u32 arg = 0, value;
	int voltage, i;

	value = readl_relaxed(pctrl->base + DR1_PIN_CFG_PARA0_REG(pin));

	param = pinconf_to_config_param(*config);
	switch (param) {
	case PIN_CONFIG_BIAS_DISABLE:
		enabled = !(value & (DR1_PIN_CFG_PULL_UP | DR1_PIN_CFG_PULL_DOWN));
		break;
	case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
		enabled = (value & DR1_PIN_CFG_PULL_UP) && (value & DR1_PIN_CFG_PULL_DOWN);
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		enabled = (value & DR1_PIN_CFG_PULL_DOWN) && !(value & DR1_PIN_CFG_PULL_UP);
		if (enabled)
			arg = (value & DR1_PIN_CFG_PULL_DOWN_10K) ?
			      DR1_PULL_STRONG_OHM : DR1_PULL_OHM;
		break;
	case PIN_CONFIG_BIAS_PULL_UP:
		enabled = (value & DR1_PIN_CFG_PULL_UP) && !(value & DR1_PIN_CFG_PULL_DOWN);
		if (enabled)
			arg = (value & DR1_PIN_CFG_PULL_UP_10K) ?
			      DR1_PULL_STRONG_OHM : DR1_PULL_OHM;
		break;
	case PIN_CONFIG_INPUT_ENABLE:
		value = readl_relaxed(pctrl->base + DR1_PIN_CFG_PARA1_REG(pin));
		arg = !!FIELD_GET(DR1_PIN_CFG_INPUT_MASK, value);
		enabled = true;
		break;
	case PIN_CONFIG_DRIVE_STRENGTH:
		voltage = dr1v90_pin_voltage(pctrl, pin);
		if (voltage < 0)
			return voltage;

		value = readl_relaxed(pctrl->base + DR1_PIN_CFG_PARA1_REG(pin));
		value &= DR1_PIN_CFG_SOURCE_MASK | DR1_PIN_CFG_SINK_MASK;

		for (i = 0; i < ARRAY_SIZE(dr1_pin_drive_strength); i++) {
			if (value != dr1v90_pin_drive_config(voltage, i))
				continue;

			enabled = true;
			arg = dr1_pin_drive_strength[i];
			break;
		}
		break;
	case PIN_CONFIG_SLEW_RATE:
		arg = FIELD_GET(DR1_PIN_CFG_SPEED_MASK, value);

		for (i = 0; i < ARRAY_SIZE(dr1_pin_cfg_speed); i++) {
			if (dr1_pin_cfg_speed[i] != arg)
				continue;

			enabled = true;
			arg = i;
			break;
		}
		break;
	default:
		return -ENOTSUPP;
	}

	*config = pinconf_to_config_packed(param, arg);

	return enabled ? 0 : -EINVAL;
}

static int dr1v90_pin_config_set(struct pinctrl_dev *pctldev, unsigned int pin,
				 unsigned long *configs, unsigned int num_configs)
{
	struct dr1v90_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);
	u32 para0, para1;
	int voltage, i;

	para0 = readl_relaxed(pctrl->base + DR1_PIN_CFG_PARA0_REG(pin));
	para1 = readl_relaxed(pctrl->base + DR1_PIN_CFG_PARA1_REG(pin));

	for (i = 0; i < num_configs; i++) {
		enum pin_config_param param = pinconf_to_config_param(configs[i]);
		u32 arg = pinconf_to_config_argument(configs[i]);

		switch (param) {
		case PIN_CONFIG_BIAS_DISABLE:
			para0 &= ~DR1_PIN_CFG_PULL_MASK;
			break;
		case PIN_CONFIG_BIAS_HIGH_IMPEDANCE:
			para0 &= ~DR1_PIN_CFG_PULL_MASK;
			para0 |= (DR1_PIN_CFG_PULL_DOWN | DR1_PIN_CFG_PULL_UP);
			break;
		case PIN_CONFIG_BIAS_PULL_DOWN:
			para0 &= ~DR1_PIN_CFG_PULL_MASK;
			para0 |= DR1_PIN_CFG_PULL_DOWN;
			if (arg == DR1_PULL_STRONG_OHM)
				para0 |= DR1_PIN_CFG_PULL_DOWN_10K;
			break;
		case PIN_CONFIG_BIAS_PULL_UP:
			para0 &= ~DR1_PIN_CFG_PULL_MASK;
			para0 |= DR1_PIN_CFG_PULL_UP;
			if (arg == DR1_PULL_STRONG_OHM)
				para0 |= DR1_PIN_CFG_PULL_UP_10K;
			break;
		case PIN_CONFIG_INPUT_ENABLE:
			if (arg) {
				voltage = dr1v90_pin_voltage(pctrl, pin);
				if (voltage < 0)
					return voltage;

				arg = voltage == DR1_IO_VOLTAGE_1V8 ?
				      DR1_PIN_CFG_INPUT_1V8 : DR1_PIN_CFG_INPUT_3V3_2V5;
			}

			para1 &= ~DR1_PIN_CFG_INPUT_MASK;
			para1 |= FIELD_PREP(DR1_PIN_CFG_INPUT_MASK, arg);
			break;
		case PIN_CONFIG_DRIVE_STRENGTH: {
			int j;

			for (j = 0; j < ARRAY_SIZE(dr1_pin_drive_strength); j++) {
				if (arg == dr1_pin_drive_strength[j])
					break;
			}
			if (j == ARRAY_SIZE(dr1_pin_drive_strength))
				return -EINVAL;

			voltage = dr1v90_pin_voltage(pctrl, pin);
			if (voltage < 0)
				return voltage;

			para1 &= ~(DR1_PIN_CFG_SOURCE_MASK | DR1_PIN_CFG_SINK_MASK);
			para1 |= dr1v90_pin_drive_config(voltage, j);
			break;
		}
		case PIN_CONFIG_SLEW_RATE:
			if (arg >= ARRAY_SIZE(dr1_pin_cfg_speed))
				return -ENOTSUPP;

			para0 &= ~DR1_PIN_CFG_SPEED_MASK;
			para0 |= FIELD_PREP(DR1_PIN_CFG_SPEED_MASK, dr1_pin_cfg_speed[arg]);
			break;
		default:
			return -ENOTSUPP;
		}
	}

	writel_relaxed(para0, pctrl->base + DR1_PIN_CFG_PARA0_REG(pin));
	writel_relaxed(para1, pctrl->base + DR1_PIN_CFG_PARA1_REG(pin));

	return 0;
}

static int dr1v90_pin_config_group_get(struct pinctrl_dev *pctldev, unsigned int selector,
				       unsigned long *config)
{
	unsigned long prev_config = 0;
	const unsigned int *pins;
	unsigned int npins;
	int i, ret;

	ret = dr1v90_get_group_pins(pctldev, selector, &pins, &npins);
	if (ret)
		return ret;

	if (!npins)
		return -ENOTSUPP;

	for (i = 0; i < npins; i++) {
		ret = dr1v90_pin_config_get(pctldev, pins[i], config);
		if (ret)
			return ret;

		/* Check config matches previous pins. */
		if (i && prev_config != *config)
			return -ENOTSUPP;

		prev_config = *config;
	}

	return 0;
}

static int dr1v90_pin_config_group_set(struct pinctrl_dev *pctldev, unsigned int selector,
				       unsigned long *configs, unsigned int num_configs)
{
	const unsigned int *pins;
	unsigned int npins;
	int i, ret;

	ret = dr1v90_get_group_pins(pctldev, selector, &pins, &npins);
	if (ret)
		return ret;

	if (!npins)
		return -ENOTSUPP;

	for (i = 0; i < npins; i++) {
		ret = dr1v90_pin_config_set(pctldev, pins[i], configs, num_configs);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct pinconf_ops dr1v90_pinconf_ops = {
	.is_generic = true,
	.pin_config_get = dr1v90_pin_config_get,
	.pin_config_set = dr1v90_pin_config_set,
	.pin_config_group_get = dr1v90_pin_config_group_get,
	.pin_config_group_set = dr1v90_pin_config_group_set,
	.pin_config_config_dbg_show = pinconf_generic_dump_config,
};

static int dr1v90_pinctrl_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct dr1v90_pinctrl *pctrl;
	int ret;

	pctrl = devm_kzalloc(dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

	pctrl->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pctrl->base))
		return PTR_ERR(pctrl->base);

	pctrl->vccio200 = devm_regulator_get(dev, "vccio200");
	if (IS_ERR(pctrl->vccio200))
		return dev_err_probe(dev, PTR_ERR(pctrl->vccio200),
				     "failed to get VCCIO200 supply\n");

	pctrl->vccio201 = devm_regulator_get(dev, "vccio201");
	if (IS_ERR(pctrl->vccio201))
		return dev_err_probe(dev, PTR_ERR(pctrl->vccio201),
				     "failed to get VCCIO201 supply\n");

	pctrl->desc.name = dev_name(dev);
	pctrl->desc.pins = dr1v90_pins;
	pctrl->desc.npins = ARRAY_SIZE(dr1v90_pins);
	pctrl->desc.pctlops = &dr1v90_pinctrl_ops;
	pctrl->desc.pmxops = &dr1v90_pinmux_ops;
	pctrl->desc.confops = &dr1v90_pinconf_ops;
	pctrl->desc.owner = THIS_MODULE;

	ret = devm_pinctrl_register_and_init(dev, &pctrl->desc, pctrl, &pctrl->pctldev);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register pinctrl\n");

	return pinctrl_enable(pctrl->pctldev);
}

static const struct of_device_id dr1v90_pinctrl_of_match[] = {
	{ .compatible = "anlogic,dr1v90-pinctrl" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, dr1v90_pinctrl_of_match);

static struct platform_driver dr1v90_pinctrl_driver = {
	.probe = dr1v90_pinctrl_probe,
	.driver = {
		.name = "pinctrl-dr1v90",
		.of_match_table = dr1v90_pinctrl_of_match,
	},
};
module_platform_driver(dr1v90_pinctrl_driver);

MODULE_AUTHOR("Naiyuan Que <naiyuan.que@anlogic.com>");
MODULE_AUTHOR("Junhui Liu <junhui.liu@pigmoral.tech>");
MODULE_DESCRIPTION("Anlogic DR1V90 pinctrl driver");
MODULE_LICENSE("GPL");
