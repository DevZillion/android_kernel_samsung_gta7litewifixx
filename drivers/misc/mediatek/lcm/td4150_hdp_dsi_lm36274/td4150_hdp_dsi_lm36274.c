// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

#define LOG_TAG "LCM"

#ifndef BUILD_LK
#  include <linux/string.h>
#  include <linux/kernel.h>
#endif

#include "lcm_drv.h"

#ifdef BUILD_LK
#  include <platform/upmu_common.h>
#  include <platform/mt_gpio.h>
#  include <platform/mt_i2c.h>
#  include <platform/mt_pmic.h>
#  include <string.h>
#elif defined(BUILD_UBOOT)
#  include <asm/arch/mt_gpio.h>
#endif

#ifdef BUILD_LK
#  define LCM_LOGI(string, args...)  dprintf(0, "[LK/"LOG_TAG"]"string, ##args)
#  define LCM_LOGD(string, args...)  dprintf(1, "[LK/"LOG_TAG"]"string, ##args)
#else
#  define LCM_LOGI(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#  define LCM_LOGD(fmt, args...)  pr_debug("[KERNEL/"LOG_TAG"]"fmt, ##args)
#endif

static struct LCM_UTIL_FUNCS lcm_util;

#define SET_RESET_PIN(v)	(lcm_util.set_reset_pin((v)))
#define MDELAY(n)		(lcm_util.mdelay(n))
#define UDELAY(n)		(lcm_util.udelay(n))

#define dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update) \
		lcm_util.dsi_set_cmdq_V22(cmdq, cmd, count, ppara, force_update)
#define dsi_set_cmdq_V2(cmd, count, ppara, force_update) \
		lcm_util.dsi_set_cmdq_V2(cmd, count, ppara, force_update)
#define dsi_set_cmdq(pdata, queue_size, force_update) \
		lcm_util.dsi_set_cmdq(pdata, queue_size, force_update)
#define wrtie_cmd(cmd) lcm_util.dsi_write_cmd(cmd)
#define write_regs(addr, pdata, byte_nums) \
		lcm_util.dsi_write_regs(addr, pdata, byte_nums)
#define read_reg(cmd)	lcm_util.dsi_dcs_read_lcm_reg(cmd)
#define read_reg_v2(cmd, buffer, buffer_size) \
		lcm_util.dsi_dcs_read_lcm_reg_v2(cmd, buffer, buffer_size)

#ifndef BUILD_LK
#  include <linux/kernel.h>
#  include <linux/module.h>
#  include <linux/fs.h>
#  include <linux/slab.h>
#  include <linux/init.h>
#  include <linux/list.h>
#  include <linux/i2c.h>
#  include <linux/irq.h>
#  include <linux/uaccess.h>
#  include <linux/interrupt.h>
#  include <linux/io.h>
#  include <linux/platform_device.h>
#endif

#define FRAME_WIDTH			(720)
#define FRAME_HEIGHT        (1600)

/* physical size in um */
#define LCM_PHYSICAL_WIDTH		(64500)
#define LCM_PHYSICAL_HEIGHT		(129000)
#define LCM_DENSITY			(480)

#define REGFLAG_DELAY			0xFFFC
#define REGFLAG_UDELAY			0xFFFB
#define REGFLAG_END_OF_TABLE		0xFFFD
#define REGFLAG_RESET_LOW		0xFFFE
#define REGFLAG_RESET_HIGH		0xFFFF

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#include <linux/platform_device.h>
#include "disp_dts_gpio.h"

#define BOARD_ID_BRINGUP        0
#define BOARD_ID_REV00      1

struct LCM_setting_table {
	unsigned int cmd;
	unsigned char count;
	unsigned char para_list[230];
};

static struct LCM_setting_table lcm_suspend_setting[] = {
	{0x28, 0, {} },
	{REGFLAG_DELAY, 50, {} },
	{0x10, 0, {} },
	{REGFLAG_DELAY, 150, {} },
};

#ifdef CUSTOMER_REFERENCE
static struct LCM_setting_table lcm_disable_setting[] = {
	{0x28, 0, {} },
	{REGFLAG_DELAY, 50, {} },
	{0x10, 0, {} },
	{REGFLAG_DELAY, 150, {} },
};
#endif

static struct LCM_setting_table init_setting_vdo[] = {
	{0xB0, 0x1, {0x04}},
	{0xB6, 0x6, {0x30, 0x6A, 0x00, 0x06, 0xC3, 0x03}},
	{0xB7, 0x4, {0x11, 0x00, 0x00, 0x00}},
	{0xB8, 0x6, {0x00, 0x78, 0x64, 0x10, 0x64, 0xB4}},
	{0xB9, 0x6, {0x00, 0x78, 0x64, 0x10, 0x64, 0xB4}},
	{0xBA, 0x6, {0x07, 0x6A, 0x55, 0x0F, 0x32, 0x44}},
	{0xBB, 0x3, {0x00, 0xB4, 0xA0}},
	{0xBC, 0x3, {0x00, 0xB4, 0xA0}},
	{0xBD, 0x3, {0x00, 0xB4, 0xA0}},
	{0xBE, 0xD, {0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00}},
	{0xC0, 0x17, {0x00, 0x7F, 0x10, 0x06, 0x40, 0x00, 0x0B, 0x06, 0xB3, 0x00,
				0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00}},
	{0xC1, 0x20, {0x30, 0x11, 0x50, 0xFA, 0x00, 0x00, 0x00, 0x22, 0x00, 0x00,
				0x00, 0x00, 0x40, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00}},
	{0xC2, 0xE4, {0x00, 0x20, 0x4E, 0x22, 0x05, 0x00, 0x05, 0x12, 0x00, 0x04,
            	0x20, 0x0E, 0x22, 0x06, 0x00, 0x05, 0x11, 0x00, 0x03, 0x20,
            	0x7C, 0x01, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x4E,
            	0x01, 0x05, 0x06, 0x54, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00,
            	0x00, 0x00, 0x02, 0x00, 0x32, 0x08, 0x08, 0x04, 0x05, 0x05,
            	0x00, 0x96, 0x50, 0x03, 0x00, 0x10, 0x08, 0x00, 0x00, 0x00,
            	0x00, 0x11, 0x00, 0x96, 0x50, 0x08, 0x00, 0x00, 0x00, 0x00,
            	0x00, 0x00, 0x00, 0x00, 0x06, 0x00, 0x72, 0x08, 0x08, 0x04,
            	0x05, 0x05, 0x00, 0x96, 0x50, 0x03, 0x00, 0x10, 0x08, 0x00,
            	0x00, 0x00, 0x00, 0x11, 0x00, 0x96, 0x50, 0x08, 0x00, 0x00,
            	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            	0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x00,
            	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00,
            	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00,
            	0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
            	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            	0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            	0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}},
	{0xC3, 0x69, {0x00, 0x40, 0x01, 0x01, 0x65, 0x90, 0x00, 0x00, 0x00, 0x00,
				0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x23, 0x00, 0x07, 0x10, 0x3B, 0x01, 0x65, 0x10,
				0x0A, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
				0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00}},
	{0xC4, 0x54, {0x00, 0x57, 0x0B, 0x00, 0x03, 0x04, 0x61, 0x55, 0x1A, 0x19,
				0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x02, 0x02, 0x02, 0x02,
				0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
				0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x55, 0x61,
				0x08, 0x07, 0x00, 0x0B, 0x57, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x80, 0x7F, 0x00, 0xE0, 0x1F, 0x00, 0x00, 0x00, 0x00,
				0xE0, 0x1F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x3F,
				0x3F, 0x3F, 0x3F, 0x55, 0x55, 0x55, 0xD5, 0xFF, 0xFF, 0xFF,
				0x57, 0x55, 0x55, 0x55}},
	{0xC5, 0x5, {0x08, 0x00, 0x00, 0x00, 0x00}},
	{0xC6, 0x4A, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11, 0x04, 0x00,
				0x40, 0x3C, 0x00, 0x00, 0x04, 0x00, 0x40, 0x3C, 0x0F, 0x05,
				0x01, 0x19, 0x01, 0x35, 0xFF, 0x8F, 0x06, 0x05, 0x01, 0xC0,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x20, 0x20, 0x00, 0x00, 0x00, 0xC0, 0x11,
				0x1F, 0x00, 0x00, 0x10, 0x10, 0x00, 0x01, 0xF0, 0x01, 0x00,
				0x50, 0x00, 0x33, 0x03}},
	{0xC7, 0x4C, {0x00, 0x00, 0x00, 0x4E, 0x00, 0x5F, 0x00, 0x69, 0x00, 0x81, 
				0x00, 0x8E, 0x00, 0xAA, 0x00, 0xA5, 0x00, 0xBC, 0x00, 0x98,
				0x01, 0x0A, 0x00, 0xB0, 0x01, 0x10, 0x00, 0xF2, 0x01, 0x5F,
				0x01, 0x88, 0x02, 0x17, 0x02, 0x94, 0x02, 0xA0, 0x00, 0x00,
				0x00, 0x4E, 0x00, 0x5F, 0x00, 0x69, 0x00, 0x81, 0x00, 0x8E,
				0x00, 0xAA, 0x00, 0xCD, 0x00, 0xFA, 0x00, 0xCE, 0x01, 0x24,
				0x01, 0x1E, 0x01, 0x54, 0x00, 0xFE, 0x01, 0x7F, 0x01, 0x88,
				0x02, 0x17, 0x02, 0x94, 0x02, 0xA0}},
	{0xC8, 0x31, {0x40, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x00, 0x00, 0x00,
				0x00, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x00,
				0x00, 0x00, 0x00, 0xFC, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x00,
				0x00, 0x00, 0x00, 0xFC, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x00,
				0x00, 0x00, 0x00, 0xFC, 0x00, 0x00, 0x00, 0x00, 0xFC}},
	{0xC9, 0x19, {0x00, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00,
				0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x24, 0x8B,
				0x4D, 0x8B, 0x4D, 0x8B, 0x4D}},
	{0xCA, 0x44, {0x1C, 0xFC, 0xFC, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00}},
	{0xCB, 0x1E, {0x12, 0xD0, 0x09, 0xC1, 0xB9, 0x65, 0x30, 0xA8, 0x74, 0x21,
				0xB8, 0x75, 0x21, 0xA9, 0x64, 0x30, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x40, 0x70, 0x00, 0x24, 0x00, 0x00, 0x00, 0xFF}},
	{0xCE, 0x18, {0x7D, 0x40, 0x48, 0x56, 0x67, 0x78, 0x88, 0x98, 0xA7, 0xB5,
				0xC3, 0xD1, 0xDE, 0xE9, 0xF2, 0xFA, 0xFF, 0x04, 0x00, 0x00,
				0x00, 0x45, 0x00, 0x00}},
	{0xCF, 0x1, {0x00}},
	{0xD0, 0x12 , {0xC2, 0x33, 0x81, 0x66, 0x09, 0x90, 0x00, 0xC0, 0x92, 0x80,
				0x12, 0x48, 0x06, 0x7E, 0x09, 0x08, 0xD0, 0x00}},
	{0xD1, 0x17, {0xD0, 0xD8, 0x1B, 0x33, 0x33, 0x17, 0x07, 0x3B, 0x55, 0x74,
				0x55, 0x74, 0x00, 0x33, 0x77, 0x07, 0x33, 0x30, 0x06, 0x72,
				0x13, 0x13, 0x00}},
	{0xD2, 0x3, {0x0F, 0x00, 0x00}},
	{0xD3, 0x99, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF,
				0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7,
				0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF,
				0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF,
				0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7,
				0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF,
				0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF,
				0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7,
				0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF,
				0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF,
				0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7,
				0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF,
				0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF,
				0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7,
				0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF, 0xFF, 0xF7, 0xFF,
				0xFF, 0xF7, 0xFF}},
	{0xE5, 0x4, {0x03, 0x00, 0x00, 0x00}},
	{0xD6, 0x1, {0xC0}},
	{0xD7, 0x2C, {0x21, 0x00, 0x12, 0x12, 0x00, 0x70, 0x08, 0x58, 0x00, 0x70,
				0x08, 0x58, 0x00, 0x83, 0x80, 0x85, 0x85, 0x85, 0x87, 0x84,
				0x45, 0x86, 0x87, 0x80, 0x82, 0x80, 0x80, 0x83, 0x83, 0x88,
				0x84, 0x08, 0x0C, 0x0C, 0x0B, 0x0A, 0x0A, 0x0A, 0x07, 0x07,
				0x06, 0x06, 0x0C, 0x08}},
	{0xD9, 0x18, {0x00, 0x02, 0x7F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00}},
	{0xDD, 0x4, {0x30, 0x06, 0x23, 0x65}},
	{0xDE, 0xE, {0x00, 0x00, 0x00, 0x0F, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x10,
				0x00, 0x00, 0x00, 0x00}},
	{0xE3, 0x1, {0xFF}},
	{0xE9, 0x3F, {0x80, 0x11, 0x17, 0x00, 0x00, 0x00, 0x5C, 0x00, 0xFF, 0xBF,
				0x05, 0xFF, 0x9F, 0x04, 0xFF, 0x7F, 0x06, 0xFF, 0x3F, 0x05,
				0xFF, 0x9F, 0x04, 0xFF, 0xCF, 0x07, 0xFF, 0xE7, 0x07, 0xFF,
				0xF3, 0x07, 0xFF, 0xF9, 0x07, 0xCD, 0xFF, 0x07, 0xE4, 0xFF,
				0x07, 0xF3, 0xFF, 0x07, 0xE9, 0xFF, 0x07, 0xE4, 0xFC, 0x07,
				0x7F, 0xFE, 0x07, 0x3F, 0xFF, 0x07, 0x9F, 0xFF, 0x07, 0xDF,
				0xFF, 0x07, 0x0C}},
	{0xEA, 0x22, {0x01, 0x0A, 0x14, 0x08, 0xE1, 0x0A, 0x20, 0xA2, 0x00, 0x00,
				0x00, 0x0A, 0x23, 0x00, 0x03, 0x01, 0x16, 0x01, 0x16, 0x01,
				0x16, 0x01, 0x28, 0x01, 0x28, 0x00, 0x70, 0x00, 0x00, 0x00,
				0x7F, 0x00, 0x24, 0x00}},
	{0xEB, 0x7, {0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x11}},
	{0xEC, 0x17, {0x01, 0xC0, 0x00, 0x10, 0x9B, 0x0A, 0x20, 0xA2, 0x00, 0x00,
				0x00, 0x01, 0x2B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00}},
	{0xED, 0x21, {0x01, 0x02, 0x06, 0x07, 0x00, 0x00, 0x02, 0x01, 0x65, 0x80,
				0x01, 0x00, 0x08, 0x00, 0x80, 0x08, 0x00, 0x00, 0x00, 0x01,
				0x0F, 0x00, 0x01, 0x30, 0x01, 0x01, 0x0F, 0x00, 0x00, 0x10,
				0x81, 0x10, 0x01}},
	{0xEE, 0x79, {0x05, 0x40, 0x05, 0x00, 0xC0, 0x0F, 0x00, 0xC0, 0x0F, 0x00,
				0xC0, 0x0F, 0x00, 0xC0, 0x0F, 0x00, 0xC0, 0x0F, 0x00, 0x00,
				0x00, 0x00, 0x00, 0xC0, 0x0F, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00,
				0x00, 0x00, 0x00, 0x00, 0xFF, 0x3F, 0x00, 0xC0, 0x0F, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03,
				0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03, 0x00,
				0x00, 0x03, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
				0x02}},
	{0xB0, 0x1, {0x03}},
	{0x11, 0x0, {}},	/* sleep_out */
	{REGFLAG_DELAY, 150, {}},/* wait 4 frame */
	{0x29, 0x0, {} },	/* display_on */
	{0x53, 0x1, {0x24} },	/* brightness_mode */
	{0x51, 0x1, {0xFF} }
};

static struct LCM_setting_table
__maybe_unused lcm_deep_sleep_mode_in_setting[] = {
	{0x28, 0x0, {} },
	{REGFLAG_DELAY, 50, {} },
	{0x10, 0x0, {} },
	{REGFLAG_DELAY, 150, {} },
};

static struct LCM_setting_table __maybe_unused lcm_sleep_out_setting[] = {
	{0x11, 0x0, {} },
	{REGFLAG_DELAY, 120, {} },
	{0x29, 0x0, {} },
	{REGFLAG_DELAY, 50, {} },
};

static struct LCM_setting_table bl_level[] = {
	{0x51, 0x1, {0xFF} },
	{REGFLAG_END_OF_TABLE, 0x00, {} }
};

/* i2c control start */
//#define LCM_I2C_ADDR 0x11
//#define LCM_I2C_BUSNUM  1	/* for I2C channel 0 */
#define LCM_I2C_ID_NAME "td4150_lm36274"

static struct i2c_client *_lcm_i2c_client;

/*****************************************************************************
 * Function Prototype
 *****************************************************************************/
static int _lcm_i2c_probe(struct i2c_client *client,
const struct i2c_device_id *id);
static int _lcm_i2c_remove(struct i2c_client *client);

/*****************************************************************************
 * Data Structure
 *****************************************************************************/
struct _lcm_i2c_dev {
	struct i2c_client *client;
};

static const struct of_device_id _lcm_i2c_of_match[] = {
	{ .compatible = "mediatek,i2c_lcd_bias", },
	{},
};

static const struct i2c_device_id _lcm_i2c_id[] = {
	{LCM_I2C_ID_NAME, 0},
	{}
};

static struct i2c_driver _lcm_i2c_driver = {
	.id_table = _lcm_i2c_id,
	.probe = _lcm_i2c_probe,
	.remove = _lcm_i2c_remove,
	/* .detect               = _lcm_i2c_detect, */
	.driver = {
		.owner = THIS_MODULE,
		.name = LCM_I2C_ID_NAME,
		.of_match_table = _lcm_i2c_of_match,
	},
};

/*****************************************************************************
 * Function
 *****************************************************************************/
static int _lcm_i2c_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct device_node *root = of_find_node_by_path("/");
	unsigned int hw_version;
	int ret;

	pr_info("td4150 : [LCM][I2C] %s\n", __func__);
	pr_info("td4150 : [LCM][I2C] NT: info==>name=%s addr=0x%x\n",
				client->name, client->addr);
	_lcm_i2c_client = client;

	if (IS_ERR_OR_NULL(root)) {
		pr_info("root dev node is NULL\n");
		return -1;
	}

	ret = of_property_read_u32(root, "dtbo-hw_rev", &hw_version);
	if (ret < 0) {
		pr_info("get dtbo-hw_rev fail:%d\n", ret);
		hw_version = 0;
	} else {
		pr_info("Get HW version = %d\n",
			hw_version);
	}

	if (hw_version == BOARD_ID_BRINGUP) {
		pr_info("td4150 : %s : board revision and LCM match\n", __func__);
		return 0;
	}
	pr_info("td4150 : %s : board revision and LCM doesn't match\n", __func__);
	return -EPERM;
}

static int _lcm_i2c_remove(struct i2c_client *client)
{
	pr_debug("td4150 : [LCM][I2C] %s\n", __func__);
	_lcm_i2c_client = NULL;
	i2c_unregister_device(client);
	return 0;
}

static int _lcm_i2c_write_bytes(unsigned char addr, unsigned char value)
{
	int ret = 0;
	struct i2c_client *client = _lcm_i2c_client;
	char write_data[2] = { 0 };

	pr_info("td4320 : %s\n", __func__);

	if (client == NULL) {
		pr_debug("ERROR!! _lcm_i2c_client is null\n");
	return 0;
	}

	write_data[0] = addr;
	write_data[1] = value;
	ret = i2c_master_send(client, write_data, 2);
	if (ret < 0)
		pr_info("[LCM][ERROR] _lcm_i2c write data fail !!\n");

	return ret;
}

/*
 * module load/unload record keeping
 */
static int __init _lcm_i2c_init(void)
{
	int ret;

	pr_debug("td4320 : [LCM][I2C] %s\n", __func__);
	ret = i2c_add_driver(&_lcm_i2c_driver);
	if (ret < 0) {
		pr_info("td4320 :[LCM][I2C] %s fail, device delete\n", __func__);
		i2c_del_driver(&_lcm_i2c_driver);
	} else
		pr_info("td4320 :[LCM][I2C] %s success\n", __func__);

	return ret;
}

static void __exit _lcm_i2c_exit(void)
{
	pr_debug("[LCM][I2C] %s\n", __func__);
	i2c_del_driver(&_lcm_i2c_driver);
}

module_init(_lcm_i2c_init);
module_exit(_lcm_i2c_exit);
/* i2c control end */

static void push_table(void *cmdq, struct LCM_setting_table *table,
		       unsigned int count, unsigned char force_update)
{
	unsigned int i;
	unsigned int cmd;

	for (i = 0; i < count; i++) {
		cmd = table[i].cmd;
		switch (cmd) {
		case REGFLAG_DELAY:
			if (table[i].count <= 10)
				MDELAY(table[i].count);
			else
				MDELAY(table[i].count);
			break;
		case REGFLAG_UDELAY:
			UDELAY(table[i].count);
			break;
		case REGFLAG_END_OF_TABLE:
			break;
		default:
			dsi_set_cmdq_V22(cmdq, cmd, table[i].count,
					 table[i].para_list, force_update);
			break;
		}
	}
}

static void lcm_set_util_funcs(const struct LCM_UTIL_FUNCS *util)
{
	memcpy(&lcm_util, util, sizeof(struct LCM_UTIL_FUNCS));
}

static void lcm_get_params(struct LCM_PARAMS *params)
{
	memset(params, 0, sizeof(struct LCM_PARAMS));

	params->type = LCM_TYPE_DSI;

	params->width = FRAME_WIDTH;
	params->height = FRAME_HEIGHT;

	params->dsi.mode = BURST_VDO_MODE;
	params->dsi.switch_mode = CMD_MODE;
	params->dsi.switch_mode_enable = 0;

	/* DSI */
	/* Command mode setting */
	params->dsi.LANE_NUM = LCM_FOUR_LANE;
	/* The following defined the fomat for data coming from LCD engine. */
	params->dsi.data_format.color_order = LCM_COLOR_ORDER_RGB;
	params->dsi.data_format.trans_seq = LCM_DSI_TRANS_SEQ_MSB_FIRST;
	params->dsi.data_format.padding = LCM_DSI_PADDING_ON_LSB;
	params->dsi.data_format.format = LCM_DSI_FORMAT_RGB888;

	/* Highly depends on LCD driver capability. */
	params->dsi.packet_size = 256;
	/* video mode timing */

	params->dsi.PS = LCM_PACKED_PS_24BIT_RGB888;

	params->dsi.vertical_sync_active = 2;
	params->dsi.vertical_backporch = 4;
	params->dsi.vertical_frontporch = 24;
	params->dsi.vertical_frontporch_for_low_power = 168;//84;
	params->dsi.vertical_active_line = FRAME_HEIGHT;

	params->dsi.horizontal_sync_active = 2;
	params->dsi.horizontal_backporch = 80;
	params->dsi.horizontal_frontporch = 508;
	params->dsi.horizontal_active_pixel = FRAME_WIDTH;
	/* params->dsi.ssc_disable = 1; */
#ifndef MACH_FPGA
	/* this value must be in MTK suggested table */
	params->dsi.PLL_CLOCK = 384;
#else
	params->dsi.pll_div1 = 0;
	params->dsi.pll_div2 = 0;
	params->dsi.fbk_div = 0x1;
#endif
	params->dsi.CLK_HS_POST = 36;
	params->dsi.clk_lp_per_line_enable = 0;
	params->dsi.esd_check_enable = 1;
	params->dsi.customization_esd_check_enable = 0;
	params->dsi.lcm_esd_check_table[0].cmd = 0x0a;
	params->dsi.lcm_esd_check_table[0].count = 1;
	params->dsi.lcm_esd_check_table[0].para_list[0] = 0x9c;

	params->dsi.esd_check_enable = 0;
	params->dsi.customization_esd_check_enable = 0;
}

int lcm_bias_regulator_init(void)
{
	return 0;
}

int lcm_bias_disable(void)
{
	return 0;
}

/* turn on gate ic & control voltage to 5.5V */
static void lcm_init_power(void)
{
	pr_info("%s\n", __func__);

	/*HT BL enable*/
	_lcm_i2c_write_bytes(0x0C, 0x2C);
	_lcm_i2c_write_bytes(0x0D, 0x26);
	_lcm_i2c_write_bytes(0x0E, 0x26);
	_lcm_i2c_write_bytes(0x09, 0xBE);
	_lcm_i2c_write_bytes(0x02, 0x49);
	_lcm_i2c_write_bytes(0x03, 0x0D);
	_lcm_i2c_write_bytes(0x11, 0x74);
	_lcm_i2c_write_bytes(0x04, 0x05);
	_lcm_i2c_write_bytes(0x05, 0xCC);
	_lcm_i2c_write_bytes(0x10, 0x67);
	_lcm_i2c_write_bytes(0x08, 0x13);
}

static void lcm_suspend_power(void)
{
	pr_info("td4320 : %s\n", __func__);
	SET_RESET_PIN(0);
	disp_dts_gpio_select_state(DTS_GPIO_STATE_LCM_RST_OUT0);

	MDELAY(1);
	disp_dts_gpio_select_state(DTS_GPIO_STATE_LCD_BIAS_ENP0);
}

static void lcm_resume_power(void)
{
	pr_info("td4320 : %s\n", __func__);
	disp_dts_gpio_select_state(DTS_GPIO_STATE_LCD_BIAS_ENP1);
	SET_RESET_PIN(0);
	disp_dts_gpio_select_state(DTS_GPIO_STATE_LCM_RST_OUT1);

	MDELAY(1);

	lcm_init_power();
}

static void lcm_init(void)
{
	pr_info("td4320 : %s\n", __func__);
	disp_dts_gpio_select_state(DTS_GPIO_STATE_LCM_RST_OUT1);
	MDELAY(1);
	disp_dts_gpio_select_state(DTS_GPIO_STATE_LCM_RST_OUT0);
	MDELAY(10);

	SET_RESET_PIN(1);
	disp_dts_gpio_select_state(DTS_GPIO_STATE_LCM_RST_OUT1);
	MDELAY(5);

	push_table(NULL, init_setting_vdo, ARRAY_SIZE(init_setting_vdo), 1);
	LCM_LOGI("td4150_fhdp----lm36274----lcm mode = vdo mode :%d----\n",
		 lcm_dsi_mode);
}

static void lcm_suspend(void)
{
	pr_info("td4320 : %s\n", __func__);
	push_table(NULL, lcm_suspend_setting,
		   ARRAY_SIZE(lcm_suspend_setting), 1);
}

/*
// This is for reference only. Customer is expected to modify in their LCM driver directly
static void lcm_disable(void)
{
#ifdef CUSTOMER_REFERENCE
	pr_info("td4320 : %s\n", __func__);
	push_table(NULL, lcm_disable_setting,
		   ARRAY_SIZE(lcm_disable_setting), 1);
#endif
}
*/

static void lcm_resume(void)
{
	pr_info("td4320 : %s\n", __func__);
	lcm_init();
}

static void lcm_setbacklight_cmdq(void *handle, unsigned int level)
{
	LCM_LOGI("%s,td4320 backlight: level = %d\n", __func__, level);

	bl_level[0].para_list[0] = level;

	push_table(handle, bl_level, ARRAY_SIZE(bl_level), 1);
}

static void lcm_update(unsigned int x, unsigned int y, unsigned int width,
	unsigned int height)
{
	unsigned int x0 = x;
	unsigned int y0 = y;
	unsigned int x1 = x0 + width - 1;
	unsigned int y1 = y0 + height - 1;

	unsigned char x0_MSB = ((x0 >> 8) & 0xFF);
	unsigned char x0_LSB = (x0 & 0xFF);
	unsigned char x1_MSB = ((x1 >> 8) & 0xFF);
	unsigned char x1_LSB = (x1 & 0xFF);
	unsigned char y0_MSB = ((y0 >> 8) & 0xFF);
	unsigned char y0_LSB = (y0 & 0xFF);
	unsigned char y1_MSB = ((y1 >> 8) & 0xFF);
	unsigned char y1_LSB = (y1 & 0xFF);

	unsigned int data_array[16];

	data_array[0] = 0x00053902;
	data_array[1] = (x1_MSB << 24) | (x0_LSB << 16) | (x0_MSB << 8) | 0x2a;
	data_array[2] = (x1_LSB);
	dsi_set_cmdq(data_array, 3, 1);

	data_array[0] = 0x00053902;
	data_array[1] = (y1_MSB << 24) | (y0_LSB << 16) | (y0_MSB << 8) | 0x2b;
	data_array[2] = (y1_LSB);
	dsi_set_cmdq(data_array, 3, 1);

	data_array[0] = 0x002c3909;
	dsi_set_cmdq(data_array, 1, 0);
}

static unsigned int lcm_compare_id(void)
{
	struct device_node *root = of_find_node_by_path("/");
	unsigned int hw_version;
	int ret;

	if (IS_ERR_OR_NULL(root)) {
		pr_info("root dev node is NULL\n");
		return -1;
	}

	ret = of_property_read_u32(root, "dtbo-hw_rev", &hw_version);
	if (ret < 0) {
		pr_info("get dtbo-hw_rev fail:%d\n", ret);
		hw_version = 0;
	} else {
		pr_info("Get HW version = %d\n",
			hw_version);
	}
	pr_info("%s : get dtbo-hw_rev:%d\n", ret, __func__);

	if (hw_version == BOARD_ID_BRINGUP)
		return 1;
	else
		return 0;

}

struct LCM_DRIVER td4150_hdp_dsi_lm36274_lcm_drv = {
	.name = "td4150_hdp_dsi_lm36274_drv",
	.set_util_funcs = lcm_set_util_funcs,
	.get_params = lcm_get_params,
	.init = lcm_init,
	.suspend = lcm_suspend,
	.resume = lcm_resume,
	.compare_id = lcm_compare_id,
	.init_power = lcm_init_power,
	.resume_power = lcm_resume_power,
	.suspend_power = lcm_suspend_power,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.update = lcm_update,
//	.disable = lcm_disable,
};