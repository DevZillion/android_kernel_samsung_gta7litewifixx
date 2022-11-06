// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 MediaTek Inc.
 */

#include <linux/module.h>
#include <linux/usb/usb_phy_generic.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/power_supply.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/clk.h>

#include "usb20.h"
#include "../musb.h"
#include "../musb_core.h"
#include "../mtk_musb.h"
#include "../musb_dr.h"
#include "../musbhsdma.h"

#ifdef CONFIG_MTK_MUSB_PHY
#include <usb20_phy.h>
#endif

#include <mt-plat/mtk_boot_common.h>

MODULE_LICENSE("GPL v2");

struct musb *mtk_musb;
EXPORT_SYMBOL(mtk_musb);

bool mtk_usb_power;
EXPORT_SYMBOL(mtk_usb_power);

int musb_force_on;
EXPORT_SYMBOL(musb_force_on);

static void (*usb_hal_dpidle_request_fptr)(int);
void usb_hal_dpidle_request(int mode)
{
	if (usb_hal_dpidle_request_fptr)
		usb_hal_dpidle_request_fptr(mode);
}
EXPORT_SYMBOL(usb_hal_dpidle_request);

void register_usb_hal_dpidle_request(void (*function)(int))
{
	usb_hal_dpidle_request_fptr = function;
}
EXPORT_SYMBOL(register_usb_hal_dpidle_request);

void (*usb_hal_disconnect_check_fptr)(void);
void usb_hal_disconnect_check(void)
{
	if (usb_hal_disconnect_check_fptr)
		usb_hal_disconnect_check_fptr();
}
EXPORT_SYMBOL(usb_hal_disconnect_check);

void register_usb_hal_disconnect_check(void (*function)(void))
{
	usb_hal_disconnect_check_fptr = function;
}
EXPORT_SYMBOL(register_usb_hal_disconnect_check);

#ifdef FPGA_PLATFORM
#include <linux/i2c.h>
#include "mtk-phy-a60810.h"
#endif

#ifdef CONFIG_MTK_MUSB_QMU_SUPPORT
#include "musb_qmu.h"
#endif

#ifdef CONFIG_MTK_USB2JTAG_SUPPORT
#include <mt-plat/mtk_usb2jtag.h>
#endif

#if defined(CONFIG_MTK_BASE_POWER)
#include "mtk_spm_resource_req.h"

#if defined(CONFIG_CABLE_TYPE_NOTIFIER)
#include <linux/cable_type_notifier.h>
#endif

static int dpidle_status = USB_DPIDLE_ALLOWED;
module_param(dpidle_status, int, 0644);

static int dpidle_debug;
module_param(dpidle_debug, int, 0644);

static DEFINE_SPINLOCK(usb_hal_dpidle_lock);

#define DPIDLE_TIMER_INTERVAL_MS 30

static void issue_dpidle_timer(void);

static void dpidle_timer_wakeup_func(struct timer_list *timer)
{
	DBG_LIMIT(1, "dpidle_timer<%p> alive", timer);
	DBG(2, "dpidle_timer<%p> alive...\n", timer);

	if (dpidle_status == USB_DPIDLE_TIMER)
		issue_dpidle_timer();
	kfree(timer);
}

static void issue_dpidle_timer(void)
{
	struct timer_list *timer;

	timer = kzalloc(sizeof(struct timer_list), GFP_ATOMIC);
	if (!timer)
		return;

	DBG(2, "add dpidle_timer<%p>\n", timer);
	timer_setup(timer, dpidle_timer_wakeup_func, 0);
	timer->expires = jiffies + msecs_to_jiffies(DPIDLE_TIMER_INTERVAL_MS);
	add_timer(timer);
}

static void usb_6765_dpidle_request(int mode)
{
	unsigned long flags;
#ifdef CONFIG_MACH_MT6761
	int ret;
#endif

	spin_lock_irqsave(&usb_hal_dpidle_lock, flags);

	/* update dpidle_status */
	dpidle_status = mode;

	switch (mode) {
	case USB_DPIDLE_ALLOWED:
		spm_resource_req(SPM_RESOURCE_USER_SSUSB, SPM_RESOURCE_RELEASE);
		if (likely(!dpidle_debug))
			DBG_LIMIT(1, "USB_DPIDLE_ALLOWED");
		else
			DBG(0, "USB_DPIDLE_ALLOWED\n");
		break;
	case USB_DPIDLE_FORBIDDEN:
		spm_resource_req(SPM_RESOURCE_USER_SSUSB, SPM_RESOURCE_ALL);
		if (likely(!dpidle_debug))
			DBG_LIMIT(1, "USB_DPIDLE_FORBIDDEN");
		else
			DBG(0, "USB_DPIDLE_FORBIDDEN\n");
		break;
	case USB_DPIDLE_SRAM:
		spm_resource_req(SPM_RESOURCE_USER_SSUSB,
				SPM_RESOURCE_CK_26M | SPM_RESOURCE_MAINPLL);
		if (likely(!dpidle_debug))
			DBG_LIMIT(1, "USB_DPIDLE_SRAM");
		else
			DBG(0, "USB_DPIDLE_SRAM\n");
		break;
	case USB_DPIDLE_TIMER:
		spm_resource_req(SPM_RESOURCE_USER_SSUSB,
				SPM_RESOURCE_CK_26M | SPM_RESOURCE_MAINPLL);
		DBG(0, "USB_DPIDLE_TIMER\n");
		issue_dpidle_timer();
		break;
	case USB_DPIDLE_SUSPEND:
		spm_resource_req(SPM_RESOURCE_USER_SSUSB,
			SPM_RESOURCE_MAINPLL | SPM_RESOURCE_CK_26M |
			SPM_RESOURCE_AXI_BUS);
#ifdef CONFIG_MACH_MT6761
		/* workaround: keep clock on for wakeup function */
		ret = clk_prepare_enable(glue->musb_clk_top_sel);
		if (ret)
			DBG(0, "%s: clk_prepare_enable: musb_clk_top_sel failed: %d\n",
				__func__, ret);
		ret = clk_prepare_enable(glue->musb_clk);
		if (ret)
			DBG(0, "%s: clk_prepare_enable: musb_clk failed: %d\n", __func__, ret);
#endif
		DBG(0, "DPIDLE_SUSPEND\n");
		break;
	case USB_DPIDLE_RESUME:
		spm_resource_req(SPM_RESOURCE_USER_SSUSB,
			SPM_RESOURCE_RELEASE);
#ifdef CONFIG_MACH_MT6761
		/* workaround: keep clock on for wakeup function */
		clk_disable_unprepare(glue->musb_clk_top_sel);
		clk_disable_unprepare(glue->musb_clk);
#endif
		DBG(0, "DPIDLE_RESUME\n");
		break;
	default:
		DBG(0, "[ERROR] Are you kidding!?!?\n");
		break;
	}

	spin_unlock_irqrestore(&usb_hal_dpidle_lock, flags);
}
#endif

/* default value 0 */
static int usb_rdy;
bool is_usb_rdy(void)
{
	if (mtk_musb->is_ready) {
		usb_rdy = 1;
		DBG(0, "set usb_rdy, wake up bat\n");
	}

	if (usb_rdy)
		return true;
	else
		return false;
}
EXPORT_SYMBOL(is_usb_rdy);

/* BC1.2 */
/* Duplicate define in phy-mtk-tphy */
#define PHY_MODE_BC11_SW_SET 1
#define PHY_MODE_BC11_SW_CLR 2

void Charger_Detect_Init(void)
{
	if(!glue) {
		DBG(0, "wlc 01!glue, maybe something wrong?\n");
		return;
	}
	usb_prepare_enable_clock(true);

	/* wait 50 usec. */
	udelay(50);

#ifdef CONFIG_PHY_MTK_TPHY
	phy_set_mode_ext(glue->phy, PHY_MODE_USB_DEVICE, PHY_MODE_BC11_SW_SET);
#endif
	usb_prepare_enable_clock(false);

	DBG(0, "%s\n", __func__);
}
EXPORT_SYMBOL(Charger_Detect_Init);

void Charger_Detect_Release(void)
{
	if(!glue) {
		DBG(0, "wlc 02!glue, maybe something wrong?\n");
		return;
	}
	usb_prepare_enable_clock(true);

#ifdef CONFIG_PHY_MTK_TPHY
	phy_set_mode_ext(glue->phy, PHY_MODE_USB_DEVICE, PHY_MODE_BC11_SW_CLR);
#endif

	udelay(1);

	usb_prepare_enable_clock(false);

	DBG(0, "%s\n", __func__);
}
EXPORT_SYMBOL(Charger_Detect_Release);

void usb_phy_tuning(void)
{
	static bool inited;
	static s32 u2_vrt_ref, u2_term_ref, u2_enhance;
	static struct device_node *of_node;

	if (!inited) {
		u2_vrt_ref = u2_term_ref = u2_enhance = -1;
		of_node = of_find_compatible_node(NULL,
			NULL, "mediatek,phy_tuning");
		if (of_node) {
			/* value won't be updated if property not being found */
			of_property_read_u32(of_node,
				"u2_vrt_ref", (u32 *) &u2_vrt_ref);
			of_property_read_u32(of_node,
				"u2_term_ref", (u32 *) &u2_term_ref);
			of_property_read_u32(of_node,
				"u2_enhance", (u32 *) &u2_enhance);
		}
		inited = true;
	} else if (!of_node)
		return;

	if (u2_vrt_ref != -1) {
		if (u2_vrt_ref <= VAL_MAX_WIDTH_3) {
			USBPHY_CLR32(OFFSET_RG_USB20_VRT_VREF_SEL,
				VAL_MAX_WIDTH_3 << SHFT_RG_USB20_VRT_VREF_SEL);
			USBPHY_SET32(OFFSET_RG_USB20_VRT_VREF_SEL,
				u2_vrt_ref << SHFT_RG_USB20_VRT_VREF_SEL);
		}
	}
	if (u2_term_ref != -1) {
		if (u2_term_ref <= VAL_MAX_WIDTH_3) {
			USBPHY_CLR32(OFFSET_RG_USB20_TERM_VREF_SEL,
				VAL_MAX_WIDTH_3 << SHFT_RG_USB20_TERM_VREF_SEL);
			USBPHY_SET32(OFFSET_RG_USB20_TERM_VREF_SEL,
				u2_term_ref << SHFT_RG_USB20_TERM_VREF_SEL);
		}
	}
	if (u2_enhance != -1) {
		if (u2_enhance <= VAL_MAX_WIDTH_2) {
			USBPHY_CLR32(OFFSET_RG_USB20_PHY_REV6,
				VAL_MAX_WIDTH_2 << SHFT_RG_USB20_PHY_REV6);
			USBPHY_SET32(OFFSET_RG_USB20_PHY_REV6,
					u2_enhance<<SHFT_RG_USB20_PHY_REV6);
		}
	}
}

void usb_rev6_setting(int value)
{
	static int direct_return;

	if (direct_return)
		return;

	/* RG_USB20_PHY_REV[7:0] = 8'b01000000 */
	USBPHY_CLR32(0x18, (0xFF << 24));

	if (value)
		USBPHY_SET32(0x18, (value << 24));
	else
		direct_return = 1;
}

static void hs_slew_rate_cal(void)
{
	unsigned long data;
	unsigned long x;
	unsigned char value;
	unsigned long start_time, timeout;
	unsigned int timeout_flag = 0;
	/* enable usb ring oscillator. */
	USBPHY_SET32(0x14, (0x1 << 15));

	/* wait 1us. */
	udelay(1);

	/* enable free run clock */
	USBPHY_SET32(0xF10 - 0x800, (0x01 << 8));
	/* setting cyclecnt. */
	USBPHY_SET32(0xF00 - 0x800, (0x04 << 8));
	/* enable frequency meter */
	USBPHY_SET32(0xF00 - 0x800, (0x01 << 24));

	/* wait for frequency valid. */
	start_time = jiffies;
	timeout = jiffies + 3 * HZ;

	while (!((USBPHY_READ32(0xF10 - 0x800) & 0xFF) == 0x1)) {
		if (time_after(jiffies, timeout)) {
			timeout_flag = 1;
			break;
		}
	}

	/* read result. */
	if (timeout_flag) {
		DBG(0, "[USBPHY] Slew Rate Calibration: Timeout\n");
		value = 0x4;
	} else {
		data = USBPHY_READ32(0xF0C - 0x800);
		x = ((1024 * FRA * PARA) / data);
		value = (unsigned char)(x / 1000);
		if ((x - value * 1000) / 100 >= 5)
			value += 1;
		DBG(1, "[USBPHY]slew calibration:FM_OUT =%lu,x=%lu,value=%d\n",
				data, x, value);
	}

	/* disable Frequency and disable free run clock. */
	USBPHY_CLR32(0xF00 - 0x800, (0x01 << 24));
	USBPHY_CLR32(0xF10 - 0x800, (0x01 << 8));

#define MSK_RG_USB20_HSTX_SRCTRL 0x7
	/* all clr first then set */
	/*HS03s for DEVAL5625-8 by wenyaqi at 20210425 start*/
	#if 0
	USBPHY_CLR32(0x14, (MSK_RG_USB20_HSTX_SRCTRL << 12));
	USBPHY_SET32(0x14, ((value & MSK_RG_USB20_HSTX_SRCTRL) << 12));
	#endif
	/*HS03s for DEVAL5625-8 by wenyaqi at 20210425 end*/

	/* disable usb ring oscillator. */
	USBPHY_CLR32(0x14, (0x1 << 15));
}

/* M17_USB_PWR Sequence 20160603.xls */
void usb_phy_recover(struct musb *musb)
{
	unsigned int efuse_val = 0;

#ifdef CONFIG_MTK_UART_USB_SWITCH
	if (in_uart_mode) {
		DBG(0, "At UART mode. No %s\n", __func__);
		return;
	}
#endif

	/* wait 50 usec. */
	udelay(50);

	/*
	 * 04.force_uart_en	1'b0 0x68 26
	 * 04.RG_UART_EN		1'b0 0x6C 16
	 * 04.rg_usb20_gpio_ctl	1'b0 0x20 09
	 * 04.usb20_gpio_mode	1'b0 0x20 08

	 * 05.force_suspendm	1'b0 0x68 18

	 * 06.RG_DPPULLDOWN	1'b0 0x68 06
	 * 07.RG_DMPULLDOWN	1'b0 0x68 07
	 * 08.RG_XCVRSEL[1:0]	2'b00 0x68 [04:05]
	 * 09.RG_TERMSEL		1'b0 0x68 02
	 * 10.RG_DATAIN[3:0]	4'b0000 0x68 [10:13]
	 * 11.force_dp_pulldown	1'b0 0x68 20
	 * 12.force_dm_pulldown	1'b0 0x68 21
	 * 13.force_xcversel	1'b0 0x68 19
	 * 14.force_termsel	1'b0 0x68 17
	 * 15.force_datain	1'b0 0x68 23
	 * 16.RG_USB20_BC11_SW_EN	1'b0 0x18 23
	 * 17.RG_USB20_OTG_VBUSCMP_EN	1'b1 0x18 20
	 */

	/* clean PUPD_BIST_EN */
	/* PUPD_BIST_EN = 1'b0 */
	/* PMIC will use it to detect charger type */
	/* NEED?? USBPHY_CLR8(0x1d, 0x10);*/
	USBPHY_CLR32(0x1c, (0x1 << 12));

	/* force_uart_en, 1'b0 */
	USBPHY_CLR32(0x68, (0x1 << 26));
	/* RG_UART_EN, 1'b0 */
	USBPHY_CLR32(0x6C, (0x1 << 16));
	/* rg_usb20_gpio_ctl, 1'b0, usb20_gpio_mode, 1'b0 */
	USBPHY_CLR32(0x20, (0x1 << 9));
	USBPHY_CLR32(0x20, (0x1 << 8));

	/* force_suspendm, 1'b0 */
	USBPHY_CLR32(0x68, (0x1 << 18));

	/* RG_DPPULLDOWN, 1'b0, RG_DMPULLDOWN, 1'b0 */
	USBPHY_CLR32(0x68, ((0x1 << 6) | (0x1 << 7)));

	/* RG_XCVRSEL[1:0], 2'b00. */
	USBPHY_CLR32(0x68, (0x3 << 4));

	/* RG_TERMSEL, 1'b0 */
	USBPHY_CLR32(0x68, (0x1 << 2));
	/* RG_DATAIN[3:0], 4'b0000 */
	USBPHY_CLR32(0x68, (0xF << 10));

	/* force_dp_pulldown, 1'b0, force_dm_pulldown, 1'b0,
	 * force_xcversel, 1'b0, force_termsel, 1'b0, force_datain, 1'b0
	 */
	USBPHY_CLR32(0x68, ((0x1 << 20) | (0x1 << 21) |
				(0x1 << 19) | (0x1 << 17) | (0x1 << 23)));

	/* RG_USB20_BC11_SW_EN, 1'b0 */
	USBPHY_CLR32(0x18, (0x1 << 23));
	/* RG_USB20_OTG_VBUSCMP_EN, 1'b1 */
	USBPHY_SET32(0x18, (0x1 << 20));

	/* RG_USB20_PHY_REV[7:0] = 8'b01000000 */
	usb_rev6_setting(0x40);

	/* wait 800 usec. */
	udelay(800);

	/* force enter device mode */
	set_usb_phy_mode(PHY_DEV_ACTIVE);

	hs_slew_rate_cal();

	efuse_val = musb->efuse_val;
	if (efuse_val) {
		DBG(0, "apply efuse setting, RG_USB20_INTR_CAL=0x%x\n",
			efuse_val);
		USBPHY_CLR32(0x04, (0x1F<<19));
		/* Add pass margin */
		USBPHY_SET32(0x04, ((efuse_val+2)<<19));
	}

	/* disc threshold to max, RG_USB20_DISCTH[7:4], dft:1000, MAX:1111 */
	USBPHY_SET32(0x18, (0xf0<<0));
	usb_phy_tuning();

	DBG(0, "usb recovery success\n");
}

#ifdef CONFIG_MTK_UART_USB_SWITCH
bool in_uart_mode;
bool usb_phy_check_in_uart_mode(void)
{
	int mode;

	usb_enable_clock(true);
	udelay(50);

	/* get phy mode */
	mode = phy_get_mode_ext(glue->phy);

	/* usb_port_mode = USBPHY_READ32(0x68); */
	usb_enable_clock(false);

	if (mode == PHY_MODE_UART) {
		DBG(0, "%s:%d - IN UART MODE : 0x%x\n",
				__func__, __LINE__, mode);
		mode = true;
	} else {
		DBG(0, "%s:%d - NOT IN UART MODE : 0x%x\n",
				__func__, __LINE__, mode);
		mode = false;
	}
	return mode;
}

void usb_phy_switch_to_uart(void)
{
	unsigned int val = 0;

	in_uart_mode = usb_phy_check_in_uart_mode();
	if (in_uart_mode) {
		DBG(0, "Already in UART mode.\n");
		return;
	}

	udelay(50);

	/* set PHY UART mode */
	phy_set_mode(glue->phy, PHY_MODE_UART);

	/* GPIO Selection */
	val = readl(ap_gpio_base);
	writel(val & (~(GPIO_SEL_MASK)), ap_gpio_base);

	val = readl(ap_gpio_base);
	writel(val | (GPIO_SEL_UART0), ap_gpio_base);

	in_uart_mode = true;
}

void usb_phy_switch_to_usb(void)
{
	unsigned int val = 0;

	/* GPIO Selection */
	val = readl(ap_gpio_base);
	writel(val & (~(GPIO_SEL_MASK)), ap_gpio_base);

	/* set UART mode to USB */
	phy_set_mode(glue->phy, PHY_MODE_USB_OTG);

	in_uart_mode = false;

	phy_power_on(glue->phy);
}

void usb_phy_context_save(void)
{
	in_uart_mode = usb_phy_check_in_uart_mode();
}
EXPORT_SYMBOL(usb_phy_context_save);

void usb_phy_context_restore(void)
{
	if (in_uart_mode)
		usb_phy_switch_to_uart();
}
EXPORT_SYMBOL(usb_phy_context_restore);
#endif

#ifdef CONFIG_USB_MTK_OTG
static struct regmap *pericfg;

static void mt_usb_wakeup(struct musb *musb, bool enable)
{
	u32 tmp;
	bool is_con = musb->port1_status & USB_PORT_STAT_CONNECTION;

	if (IS_ERR_OR_NULL(pericfg)) {
		DBG(0, "init fail");
		return;
	}

	DBG(0, "connection=%d\n", is_con);

	if (enable) {
		regmap_read(pericfg, USB_WAKEUP_DEC_CON1, &tmp);
		tmp |= USB1_CDDEBOUNCE(0x8) | USB1_CDEN;
		regmap_write(pericfg, USB_WAKEUP_DEC_CON1, tmp);

		tmp = musb_readw(musb->mregs, RESREG);
		if (is_con)
			tmp &= ~HSTPWRDWN_OPT;
		else
			tmp |= HSTPWRDWN_OPT;
		musb_writew(musb->mregs, RESREG, tmp);
	} else {
		regmap_read(pericfg, USB_WAKEUP_DEC_CON1, &tmp);
		tmp &= ~(USB1_CDEN | USB1_CDDEBOUNCE(0xf));
		regmap_write(pericfg, USB_WAKEUP_DEC_CON1, tmp);

		tmp = musb_readw(musb->mregs, RESREG);
		tmp &= ~HSTPWRDWN_OPT;
		musb_writew(musb->mregs, RESREG, tmp);
		if (is_con && !musb->is_active) {
			DBG(0, "resume with device connected\n");
			musb->is_active = 1;
		}
	}
}

static int mt_usb_wakeup_init(struct musb *musb)
{
	struct device_node *node;

#ifdef CONFIG_MACH_MT6761
	node = of_find_compatible_node(NULL, NULL,
					"mediatek,mt6761-usb20");
#else

	node = of_find_compatible_node(NULL, NULL,
					"mediatek,mt6765-usb20");
#endif
	if (!node) {
		DBG(0, "map node failed\n");
		return -ENODEV;
	}

	pericfg = syscon_regmap_lookup_by_phandle(node,
					"pericfg");
	if (IS_ERR(pericfg)) {
		DBG(0, "fail to get pericfg regs\n");
		return PTR_ERR(pericfg);
	}

	return 0;
}
#endif

static u32 cable_mode = CABLE_MODE_NORMAL;
#ifndef FPGA_PLATFORM
static struct regulator *reg_vusb;
static struct regulator *reg_va12;
#endif

#ifdef CONFIG_MTK_UART_USB_SWITCH
static u32 port_mode = PORT_MODE_USB;
#define AP_GPIO_COMPATIBLE_NAME "mediatek,gpio"
void __iomem *ap_gpio_base;
#endif

/* EP Fifo Config */
static struct musb_fifo_cfg fifo_cfg[] __initdata = {
	{.hw_ep_num = 1, .style = FIFO_TX, .maxpacket = 512,
		.ep_mode = EP_BULK, .mode = BUF_DOUBLE},
	{.hw_ep_num = 1, .style = FIFO_RX, .maxpacket = 512,
		.ep_mode = EP_BULK, .mode = BUF_DOUBLE},
	{.hw_ep_num = 2, .style = FIFO_TX, .maxpacket = 512,
		.ep_mode = EP_BULK, .mode = BUF_DOUBLE},
	{.hw_ep_num = 2, .style = FIFO_RX, .maxpacket = 512,
		.ep_mode = EP_BULK, .mode = BUF_DOUBLE},
	{.hw_ep_num = 3, .style = FIFO_TX, .maxpacket = 512,
		.ep_mode = EP_BULK, .mode = BUF_DOUBLE},
	{.hw_ep_num = 3, .style = FIFO_RX, .maxpacket = 512,
		.ep_mode = EP_BULK, .mode = BUF_DOUBLE},
	{.hw_ep_num = 4, .style = FIFO_TX, .maxpacket = 512,
		.ep_mode = EP_BULK, .mode = BUF_DOUBLE},
	{.hw_ep_num = 4, .style = FIFO_RX, .maxpacket = 512,
		.ep_mode = EP_BULK, .mode = BUF_DOUBLE},
	{.hw_ep_num = 5, .style = FIFO_TX, .maxpacket = 512,
		.ep_mode = EP_INT, .mode = BUF_SINGLE},
	{.hw_ep_num = 5, .style = FIFO_RX, .maxpacket = 512,
		.ep_mode = EP_INT, .mode = BUF_SINGLE},
	{.hw_ep_num = 6, .style = FIFO_TX, .maxpacket = 512,
		.ep_mode = EP_INT, .mode = BUF_SINGLE},
	{.hw_ep_num = 6, .style = FIFO_RX, .maxpacket = 512,
		.ep_mode = EP_INT, .mode = BUF_SINGLE},
	{.hw_ep_num = 7, .style = FIFO_TX, .maxpacket = 512,
		.ep_mode = EP_BULK, .mode = BUF_SINGLE},
	{.hw_ep_num = 7, .style = FIFO_RX, .maxpacket = 512,
		.ep_mode = EP_BULK, .mode = BUF_SINGLE},
	{.hw_ep_num = 8, .style = FIFO_TX, .maxpacket = 512,
		.ep_mode = EP_ISO, .mode = BUF_DOUBLE},
	{.hw_ep_num = 8, .style = FIFO_RX, .maxpacket = 512,
		.ep_mode = EP_ISO, .mode = BUF_DOUBLE},
};

/*=======================================================================*/
/* USB GADGET                                                     */
/*=======================================================================*/
static const struct of_device_id apusb_of_ids[] = {
	{.compatible = "mediatek,mt6765-usb20",},
	{.compatible = "mediatek,mt6761-usb20",},
	{},
};

MODULE_DEVICE_TABLE(of, apusb_of_ids);

/*HS03s for SR-AL5625-01-261 by wenyaqi at 20210428 start*/
#ifndef HQ_FACTORY_BUILD	//ss version
extern int hq_get_boot_mode(void);
#endif
#if 0
#ifdef CONFIG_TCPC_CLASS
extern bool sink_to_src_flag;
extern bool src_to_sink_flag;
extern bool ss_musb_is_host(void);
#endif
static int mt_usb_psy_notifier(struct notifier_block *nb,
				unsigned long event, void *ptr)
{
	struct musb *musb = container_of(nb, struct musb, psy_nb);
	struct power_supply *psy = ptr;

	if (event == PSY_EVENT_PROP_CHANGED && psy == musb->usb_psy) {

		DBG(0, "psy=%s, event=%d", psy->desc->name, event);

		#ifdef CONFIG_TCPC_CLASS
		if (sink_to_src_flag && !src_to_sink_flag &&
			!ss_musb_is_host()) {
			DBG(0, "device mode, typec sink->src, keep device\n");
			return NOTIFY_DONE;
		} else if (!sink_to_src_flag && src_to_sink_flag &&
			ss_musb_is_host()) {
			DBG(0, "host mode, typec src->sink, keep host\n");
			return NOTIFY_DONE;
		} else if (!sink_to_src_flag && src_to_sink_flag &&
			!ss_musb_is_host()) {
			DBG(0, "device mode, typec src->sink, keep device\n");
			return NOTIFY_DONE;
		} else if (sink_to_src_flag && !src_to_sink_flag &&
			ss_musb_is_host()) {
			DBG(0, "host mode, typec sink->src, keep host\n");
			return NOTIFY_DONE;
		}
		#endif

		#ifndef HQ_FACTORY_BUILD	//ss version
		if (hq_get_boot_mode() == KERNEL_POWER_OFF_CHARGING_BOOT ||
			hq_get_boot_mode() == LOW_POWER_OFF_CHARGING_BOOT) {
			/* do nothing */
			pr_notice("%s: do nothing in KPOC\n", __func__);
		} else {
			if (usb_cable_connected(musb))
				mt_usb_connect();
			else
				mt_usb_disconnect();
		}
		#else
		if (usb_cable_connected(musb))
			mt_usb_connect();
		else
			mt_usb_disconnect();
		#endif
/*HS03s for SR-AL5625-01-261 by wenyaqi at 20210428 end*/
	}
	return NOTIFY_DONE;
}
#endif

#ifdef FPGA_PLATFORM
bool usb_enable_clock(bool enable)
{
	return true;
}
EXPORT_SYMBOL(usb_enable_clock);

bool usb_prepare_clock(bool enable)
{
	return true;
}
EXPORT_SYMBOL(usb_prepare_clock);

void usb_prepare_enable_clock(bool enable)
{
}
EXPORT_SYMBOL(usb_prepare_enable_clock);
#else
void usb_prepare_enable_clock(bool enable)
{
	if (enable) {
		usb_prepare_clock(true);
		usb_enable_clock(true);
	} else {
		usb_enable_clock(false);
		usb_prepare_clock(false);
	}
}
EXPORT_SYMBOL(usb_prepare_enable_clock);

DEFINE_MUTEX(prepare_lock);
static atomic_t clk_prepare_cnt = ATOMIC_INIT(0);

bool usb_prepare_clock(bool enable)
{
	int before_cnt = 0;
	before_cnt = atomic_read(&clk_prepare_cnt);

	mutex_lock(&prepare_lock);

	if (IS_ERR_OR_NULL(glue->musb_clk) ||
			IS_ERR_OR_NULL(glue->musb_clk_top_sel) ||
			IS_ERR_OR_NULL(glue->musb_clk_univpll3_d4)) {
		DBG(0, "clk not ready\n");
		mutex_unlock(&prepare_lock);
		return 0;
	}

	if (enable) {
		if (clk_prepare(glue->musb_clk_top_sel)) {
			DBG(1, "musb_clk_top_sel prepare fail\n");
		} else {
			if (clk_set_parent(glue->musb_clk_top_sel,
						glue->musb_clk_univpll3_d4))
				DBG(1, "musb_clk_top_sel set_parent fail\n");
		}
		if (clk_prepare(glue->musb_clk))
			DBG(1, "musb_clk prepare fail\n");

		atomic_inc(&clk_prepare_cnt);
	} else {
		clk_unprepare(glue->musb_clk_top_sel);
		clk_unprepare(glue->musb_clk);

		atomic_dec(&clk_prepare_cnt);
	}

	mutex_unlock(&prepare_lock);

	DBG(1, "enable(%d), usb prepare_cnt, before(%d), after(%d)\n",
		enable, before_cnt, atomic_read(&clk_prepare_cnt));

#ifdef CONFIG_MTK_AEE_FEATURE
	if (atomic_read(&clk_prepare_cnt) < 0)
		aee_kernel_warning("usb20", "usb clock prepare_cnt error\n");
#endif

	return 1;
}
EXPORT_SYMBOL(usb_prepare_clock);

static DEFINE_SPINLOCK(musb_reg_clock_lock);

bool usb_enable_clock(bool enable)
{
	static int count;
	static int real_enable = 0, real_disable;
	static int virt_enable = 0, virt_disable;
	unsigned long flags;

	DBG(1, "enable(%d),count(%d),<%d,%d,%d,%d>\n",
	    enable, count, virt_enable, virt_disable,
	    real_enable, real_disable);

	spin_lock_irqsave(&musb_reg_clock_lock, flags);

	if (unlikely(atomic_read(&clk_prepare_cnt) <= 0)) {
		DBG_LIMIT(1, "clock not prepare");
		goto exit;
	}

	if (enable && count == 0) {
		if (clk_enable(glue->musb_clk_top_sel)) {
			DBG(0, "musb_clk_top_sel enable fail\n");
			goto exit;
		}

		if (clk_enable(glue->musb_clk)) {
			DBG(0, "musb_clk enable fail\n");
			clk_disable(glue->musb_clk_top_sel);
			goto exit;
		}

		usb_hal_dpidle_request(USB_DPIDLE_FORBIDDEN);
		real_enable++;

	} else if (!enable && count == 1) {
		clk_disable(glue->musb_clk);
		clk_disable(glue->musb_clk_top_sel);

		usb_hal_dpidle_request(USB_DPIDLE_ALLOWED);
		real_disable++;
	}

	if (enable)
		count++;
	else
		count = (count == 0) ? 0 : (count - 1);

exit:
	if (enable)
		virt_enable++;
	else
		virt_disable++;

	spin_unlock_irqrestore(&musb_reg_clock_lock, flags);

	DBG(1, "enable(%d),count(%d), <%d,%d,%d,%d>\n",
	    enable, count, virt_enable, virt_disable,
	    real_enable, real_disable);
	return 1;
}
EXPORT_SYMBOL(usb_enable_clock);
#endif

static int mt_usb_psy_init(struct musb *musb)
{
	int ret = 0;
	struct device *dev = musb->controller->parent;

	musb->usb_psy = devm_power_supply_get_by_phandle(dev, "charger");
	if (IS_ERR_OR_NULL(musb->usb_psy)) {
		DBG(0, "couldn't get usb_psy\n");
		return -EINVAL;
	}
#if 0
	musb->psy_nb.notifier_call = mt_usb_psy_notifier;
	ret = power_supply_reg_notifier(&musb->psy_nb);
	if (ret)
		DBG(0, "failed to reg notifier: %d\n", ret);
#endif
	return ret;
}

static struct delayed_work idle_work;

void do_idle_work(struct work_struct *data)
{
	struct musb *musb = mtk_musb;
	unsigned long flags;
	u8 devctl;
	enum usb_otg_state old_state;

	usb_prepare_clock(true);

	spin_lock_irqsave(&musb->lock, flags);
	old_state = musb->xceiv->otg->state;
	if (musb->is_active) {
		DBG(0,
			"%s active, igonre do_idle\n",
			otg_state_string(musb->xceiv->otg->state));
		goto exit;
	}

	switch (musb->xceiv->otg->state) {
	case OTG_STATE_B_PERIPHERAL:
	case OTG_STATE_A_WAIT_BCON:
		devctl = musb_readb(musb->mregs, MUSB_DEVCTL);
		if (devctl & MUSB_DEVCTL_BDEVICE) {
			musb->xceiv->otg->state = OTG_STATE_B_IDLE;
			MUSB_DEV_MODE(musb);
		} else {
			musb->xceiv->otg->state = OTG_STATE_A_IDLE;
			MUSB_HST_MODE(musb);
		}
		break;
	case OTG_STATE_A_HOST:
		devctl = musb_readb(musb->mregs, MUSB_DEVCTL);
		if (devctl & MUSB_DEVCTL_BDEVICE)
			musb->xceiv->otg->state = OTG_STATE_B_IDLE;
		else
			musb->xceiv->otg->state = OTG_STATE_A_WAIT_BCON;
		break;
	default:
		break;
	}
	DBG(0, "otg_state %s to %s, is_active<%d>\n",
			otg_state_string(old_state),
			otg_state_string(musb->xceiv->otg->state),
			musb->is_active);
exit:
	spin_unlock_irqrestore(&musb->lock, flags);

	usb_prepare_clock(false);
}

#if defined(CONFIG_MTK_BASE_POWER)
static void musb_do_idle(struct timer_list *t)
{
	struct musb *musb = from_timer(musb, t, idle_timer);

	queue_delayed_work(musb->st_wq, &idle_work, 0);
}

static void mt_usb_try_idle(struct musb *musb, unsigned long timeout)
{
	unsigned long default_timeout = jiffies + msecs_to_jiffies(3);
	static unsigned long last_timer;

	if (timeout == 0)
		timeout = default_timeout;

	/* Never idle if active, or when VBUS timeout is not set as host */
	if (musb->is_active || ((musb->a_wait_bcon == 0)
				&& (musb->xceiv->otg->state
				== OTG_STATE_A_WAIT_BCON))) {
		DBG(0, "%s active, deleting timer\n",
			otg_state_string(musb->xceiv->otg->state));
		del_timer(&musb->idle_timer);
		last_timer = jiffies;
		return;
	}

	if (time_after(last_timer, timeout)) {
		if (!timer_pending(&musb->idle_timer))
			last_timer = timeout;
		else {
			DBG(0, "Longer idle timer already pending, ignoring\n");
			return;
		}
	}
	last_timer = timeout;

	DBG(0, "%s inactive, for idle timer for %lu ms\n",
	    otg_state_string(musb->xceiv->otg->state),
	    (unsigned long)jiffies_to_msecs(timeout - jiffies));
	mod_timer(&musb->idle_timer, timeout);
}
#endif

#ifdef CONFIG_MACH_MT6761
static void __iomem *infra_mbist;
#define USB_SRAM_SET 0x093cc01b

/* setup sram, only for mt6761 */
static void usb_sram_setup(void)
{
	if (infra_mbist)
		writel(USB_SRAM_SET, infra_mbist + 0x2c);
	else
		DBG(0, "infra_mbist not init\n");

	mdelay(1);
}

static int usb_sram_init(void)
{
	struct device_node *node = NULL;

	node = of_find_compatible_node(NULL, NULL,
					"mediatek,infra_mbist");
	if (!node) {
		DBG(0, "infra_mbist map node failed\n");
		return -1;
	}

	infra_mbist = of_iomap(node, 0);
	if (!infra_mbist) {
		DBG(0, "iomap infra_mbist failed\n");
		return -1;
	}

	/* usb20_top_bist */
	writel(USB_SRAM_SET, infra_mbist + 0x2c);
	/* wait stable */
	mdelay(1);

	return 0;
}
#endif

static int real_enable = 0, real_disable;
static int virt_enable = 0, virt_disable;
static void mt_usb_enable(struct musb *musb)
{
	unsigned long flags;
	#ifdef CONFIG_MTK_UART_USB_SWITCH
	static int is_check;
	#endif

	virt_enable++;
	DBG(0, "begin <%d,%d>,<%d,%d,%d,%d>\n",
			mtk_usb_power, musb->power,
			virt_enable, virt_disable,
			real_enable, real_disable);
	if (musb->power == true)
		return;

	/* clock alredy prepare before enter here */
	usb_enable_clock(true);

	mdelay(10);
#ifdef CONFIG_MTK_UART_USB_SWITCH
	if (!is_check) {
		in_uart_mode = usb_phy_check_in_uart_mode();
		is_check = 1;
	}
	#endif

	flags = musb_readl(musb->mregs, USB_L1INTM);
#ifdef CONFIG_MACH_MT6761
	/* only for mt6761 */
	usb_sram_setup();
#endif
	usb_phy_recover(musb);
	/* update musb->power & mtk_usb_power in the same time */
	musb->power = true;
	mtk_usb_power = true;
	real_enable++;
	if (in_interrupt()) {
		DBG(0, "in interrupt !!!!!!!!!!!!!!!\n");
		DBG(0, "in interrupt !!!!!!!!!!!!!!!\n");
		DBG(0, "in interrupt !!!!!!!!!!!!!!!\n");
	}
	DBG(0, "end, <%d,%d,%d,%d>\n",
		virt_enable, virt_disable,
		real_enable, real_disable);
	musb_writel(mtk_musb->mregs, USB_L1INTM, flags);
}

static void mt_usb_disable(struct musb *musb)
{
	virt_disable++;

	DBG(0, "begin, <%d,%d>,<%d,%d,%d,%d>\n",
		mtk_usb_power, musb->power,
		virt_enable, virt_disable,
	    real_enable, real_disable);
	if (musb->power == false)
		return;

	usb_enable_clock(false);
	/* clock will unprepare when leave here */

	real_disable++;
	DBG(0, "end, <%d,%d,%d,%d>\n",
		virt_enable, virt_disable,
		real_enable, real_disable);

	/* update musb->power & mtk_usb_power in the same time */
	musb->power = 0;
	mtk_usb_power = false;
}

/* ================================ */
/* connect and disconnect functions */
/* ================================ */
bool mt_usb_is_device(void)
{
	DBG(4, "called\n");

	if (!mtk_musb) {
		DBG(0, "mtk_musb is NULL\n");
		/* don't do charger detection when usb is not ready */
		return false;
	}
	DBG(4, "is_host=%d\n", mtk_musb->is_host);

#ifdef CONFIG_MTK_UART_USB_SWITCH
	if (in_uart_mode) {
		DBG(0, "in UART Mode\n");
		return false;
	}
#endif
#ifdef CONFIG_USB_MTK_OTG
	return !mtk_musb->is_host;
#else
	return true;
#endif
}
static struct delayed_work disconnect_check_work;
static bool musb_hal_is_vbus_exist(void);
void do_disconnect_check_work(struct work_struct *data)
{
	bool vbus_exist = false;
	unsigned long flags = 0;
	struct musb *musb = mtk_musb;

	msleep(200);

	vbus_exist = musb_hal_is_vbus_exist();
	DBG(1, "vbus_exist:<%d>\n", vbus_exist);
	if (vbus_exist)
		return;

	spin_lock_irqsave(&mtk_musb->lock, flags);
	DBG(1, "speed <%d>\n", musb->g.speed);
	/* notify gadget driver, g.speed judge is very important */
	if (!musb->is_host && musb->g.speed != USB_SPEED_UNKNOWN) {
		DBG(0, "musb->gadget_driver:%p\n", musb->gadget_driver);
		if (musb->gadget_driver && musb->gadget_driver->disconnect) {
			DBG(0, "musb->gadget_driver->disconnect:%p\n",
					musb->gadget_driver->disconnect);
			/* align musb_g_disconnect */
			spin_unlock(&musb->lock);
			musb->gadget_driver->disconnect(&musb->g);
			spin_lock(&musb->lock);

		}
		musb->g.speed = USB_SPEED_UNKNOWN;
	}
	DBG(1, "speed <%d>\n", musb->g.speed);
	spin_unlock_irqrestore(&mtk_musb->lock, flags);
}
void trigger_disconnect_check_work(void)
{
	static int inited;

	if (!inited) {
		INIT_DELAYED_WORK(&disconnect_check_work,
			do_disconnect_check_work);
		inited = 1;
	}
	queue_delayed_work(mtk_musb->st_wq, &disconnect_check_work, 0);
}

static bool musb_hal_is_vbus_exist(void)
{
	bool vbus_exist = true;

	return vbus_exist;
}

/* be aware this could not be used in non-sleep context */
bool usb_cable_connected(struct musb *musb)
{
	struct power_supply *psy;
	union power_supply_propval pval;
	union power_supply_propval tval;
	int ret;

	/* workaround to register psy again */
	if (IS_ERR_OR_NULL(musb->usb_psy)) {
		DBG(0, "usb_psy not ready\n");
		if (mt_usb_psy_init(musb))
			return false;
	}

	psy = musb->usb_psy;
	ret = power_supply_get_property(psy,
				POWER_SUPPLY_PROP_ONLINE, &pval);
	if (ret != 0) {
		DBG(0, "failed to get psy prop, ret=%d\n", ret);
		return false;
	}

	ret = power_supply_get_property(psy,
				POWER_SUPPLY_PROP_USB_TYPE, &tval);
	if (ret != 0) {
		DBG(0, "failed to get psy prop, ret=%d\n", ret);
		return false;
	}

	DBG(0, "online=%d, type=%d\n", pval.intval, tval.intval);

	if (pval.intval && (tval.intval == POWER_SUPPLY_USB_TYPE_SDP ||
			tval.intval == POWER_SUPPLY_USB_TYPE_CDP))
		return true;
	else
		return false;
}

static bool cmode_effect_on(void)
{
	bool effect = false;

	/* CMODE CHECK */
	if (cable_mode == CABLE_MODE_CHRG_ONLY /*||
		(cable_mode == CABLE_MODE_HOST_ONLY &&
			chg_type != CHARGING_HOST)*/)
		effect = true;

	DBG(0, "cable_mode=%d, effect=%d\n", cable_mode, effect);
	return effect;
}

void do_connection_work(struct work_struct *data)
{
	unsigned long flags = 0;
	int usb_clk_state = NO_CHANGE;
	bool usb_on, usb_connected;
	struct mt_usb_work *work =
		container_of(data, struct mt_usb_work, dwork.work);

	DBG(0, "is_host<%d>, power<%d>, ops<%d>\n",
		mtk_musb->is_host, mtk_musb->power, work->ops);

	/* always prepare clock and check if need to unprepater later */
	/* clk_prepare_cnt +1 here*/
	usb_prepare_clock(true);

	/* be aware this could not be used in non-sleep context */
	usb_connected = usb_cable_connected(mtk_musb);

	/* additional check operation here */
	if (musb_force_on)
		usb_on = true;
	else if (work->ops == CONNECTION_OPS_CHECK) {
		usb_on = usb_connected;
#if defined(CONFIG_CABLE_TYPE_NOTIFIER)
		if (!musb_is_host() && usb_on) {
			DBG(0, "mtk_musb->sec_cable_type=%d", mtk_musb->sec_cable_type);
			switch (mtk_musb->sec_cable_type) {
				case POWER_SUPPLY_USB_TYPE_SDP:
					cable_type_notifier_set_attached_dev(CABLE_TYPE_USB_SDP);
					break;
				case POWER_SUPPLY_USB_TYPE_CDP:
					cable_type_notifier_set_attached_dev(CABLE_TYPE_USB_CDP);
					break;
				default:
					break;
			}
		}
#endif
	} else
		usb_on = (work->ops ==
			CONNECTION_OPS_CONN ? true : false);

	if (cmode_effect_on())
		usb_on = false;
	/* additional check operation done */
	spin_lock_irqsave(&mtk_musb->lock, flags);

	if (mtk_musb->is_host) {
		DBG(0, "is host, return\n");
		goto exit;
	}

#ifdef CONFIG_MTK_UART_USB_SWITCH
	if (in_uart_mode) {
		DBG(0, "in uart mode, return\n");
		goto exit;
	}
#endif

	if (!mtk_musb->power && (usb_on == true)) {
		/* enable usb */
		if (!mtk_musb->usb_lock->active) {
			__pm_stay_awake(mtk_musb->usb_lock);
			DBG(0, "lock\n");
		} else {
			DBG(0, "already lock\n");
		}

		/* note this already put SOFTCON */
		musb_start(mtk_musb);
		usb_clk_state = OFF_TO_ON;

	} else if (mtk_musb->power && (usb_on == false)) {
		/* disable usb */
		musb_stop(mtk_musb);
		if (mtk_musb->usb_lock->active) {
			DBG(0, "unlock\n");
			__pm_relax(mtk_musb->usb_lock);
		} else {
			DBG(0, "lock not active\n");
		}
		usb_clk_state = ON_TO_OFF;
	} else
		DBG(0, "do nothing, usb_on:%d, power:%d\n",
				usb_on, mtk_musb->power);
exit:
	spin_unlock_irqrestore(&mtk_musb->lock, flags);

	if (usb_clk_state == ON_TO_OFF) {
		/* clock on -> of: clk_prepare_cnt -2 */
		usb_prepare_clock(false);
		usb_prepare_clock(false);
	} else if (usb_clk_state == NO_CHANGE) {
		/* clock no change : clk_prepare_cnt -1 */
		usb_prepare_clock(false);
	}

	/* free mt_usb_work */
	kfree(work);
}

static void issue_connection_work(int ops)
{
	struct mt_usb_work *work;

	if (!mtk_musb) {
		DBG(0, "mtk_musb = NULL\n");
		return;
	}
	/* create and prepare worker */
	work = kzalloc(sizeof(struct mt_usb_work), GFP_ATOMIC);
	if (!work) {
		DBG(0, "wrap is NULL, directly return\n");
		return;
	}
	work->ops = ops;
	INIT_DELAYED_WORK(&work->dwork, do_connection_work);
	/* issue connection work */
	DBG(0, "issue work, ops<%d>\n", ops);
	queue_delayed_work(mtk_musb->st_wq, &work->dwork, 0);
}

void mt_usb_connect(void)
{
	DBG(0, "[MUSB] USB connect\n");
	issue_connection_work(CONNECTION_OPS_CONN);
}
EXPORT_SYMBOL(mt_usb_connect);

void mt_usb_disconnect(void)
{
	DBG(0, "[MUSB] USB disconnect\n");
	issue_connection_work(CONNECTION_OPS_DISC);
}

void mt_usb_dev_disconnect(void)
{
	DBG(0, "[MUSB] USB disconnect\n");
	issue_connection_work(CONNECTION_OPS_DISC);
}

void mt_usb_reconnect(void)
{
	DBG(0, "[MUSB] USB reconnect\n");
	issue_connection_work(CONNECTION_OPS_CHECK);
}
EXPORT_SYMBOL(mt_usb_reconnect);

/* build time force on */
#if defined(CONFIG_FPGA_EARLY_PORTING) ||\
		defined(U3_COMPLIANCE) || defined(FOR_BRING_UP)
#define BYPASS_PMIC_LINKAGE
#endif

static int usb20_test_connect;
static struct delayed_work usb20_test_connect_work;
#define TEST_CONNECT_BASE_MS 3000
#define TEST_CONNECT_BIAS_MS 5000
static void do_usb20_test_connect_work(struct work_struct *work)
{
	static ktime_t ktime;
	static unsigned long ktime_us;
	unsigned int delay_time_ms;
	static bool test_connected;

	if (!usb20_test_connect) {
		test_connected = false;
		DBG(0, "test done, trigger connect\n");
		mt_usb_reconnect();
		return;
	}

	if (test_connected)
		mt_usb_connect();
	else
		mt_usb_dev_disconnect();

	ktime = ktime_get();
	ktime_us = ktime_to_us(ktime);
	delay_time_ms = TEST_CONNECT_BASE_MS
				+ (ktime_us % TEST_CONNECT_BIAS_MS);
	DBG(0, "work after %d ms\n", delay_time_ms);
	schedule_delayed_work(&usb20_test_connect_work,
					msecs_to_jiffies(delay_time_ms));

	test_connected = !test_connected;
}

void mt_usb_connect_test(int start)
{
	static struct wakeup_source *dev_test_wakelock;
	static int wake_lock_inited;

	if (!wake_lock_inited) {
		DBG(0, "wake_lock_init\n");
		dev_test_wakelock = wakeup_source_register(NULL, "device.test.lock");
		wake_lock_inited = 1;
	}

	if (start) {
		__pm_stay_awake(dev_test_wakelock);
		usb20_test_connect = 1;
		INIT_DELAYED_WORK(&usb20_test_connect_work,
				do_usb20_test_connect_work);
		schedule_delayed_work(&usb20_test_connect_work, 0);
	} else {
		usb20_test_connect = 0;
		__pm_relax(dev_test_wakelock);
	}
}

void musb_platform_reset(struct musb *musb)
{
	u16 swrst = 0;
	void __iomem *mbase = musb->mregs;
	u8 bit;

	/* clear all DMA enable bit */
	for (bit = 0; bit < MUSB_HSDMA_CHANNELS; bit++)
		musb_writew(mbase,
			MUSB_HSDMA_CHANNEL_OFFSET(bit, MUSB_HSDMA_CONTROL), 0);

	/* set DMA channel 0 burst mode to boost QMU speed */
	musb_writel(musb->mregs, 0x204,
			musb_readl(musb->mregs, 0x204) | 0x600);
#ifdef CONFIG_MTK_MUSB_DRV_36BIT
	/* eanble DMA channel 0 36-BIT support */
	musb_writel(musb->mregs, 0x204,
			musb_readl(musb->mregs, 0x204) | 0x4000);
#endif

	swrst = musb_readw(mbase, MUSB_SWRST);
	swrst |= (MUSB_SWRST_DISUSBRESET | MUSB_SWRST_SWRST);
	musb_writew(mbase, MUSB_SWRST, swrst);
}
EXPORT_SYMBOL(musb_platform_reset);

void musb_sync_with_bat(struct musb *musb, int usb_state)
{
	DBG(1, "BATTERY_SetUSBState, state=%d\n", usb_state);
}
EXPORT_SYMBOL(musb_sync_with_bat);

/*-------------------------------------------------------------------------*/
static irqreturn_t generic_interrupt(int irq, void *__hci)
{
	irqreturn_t retval = IRQ_NONE;
	struct musb *musb = __hci;

	/* musb_read_clear_generic_interrupt */
	musb->int_usb =
	musb_readb(musb->mregs, MUSB_INTRUSB) &
				musb_readb(musb->mregs, MUSB_INTRUSBE);
	musb->int_tx = musb_readw(musb->mregs, MUSB_INTRTX) &
				musb_readw(musb->mregs, MUSB_INTRTXE);
	musb->int_rx = musb_readw(musb->mregs, MUSB_INTRRX) &
				musb_readw(musb->mregs, MUSB_INTRRXE);
#ifdef CONFIG_MTK_MUSB_QMU_SUPPORT
	musb->int_queue = musb_readl(musb->mregs, MUSB_QISAR);
#endif
	/* hw status up to date before W1C */
	mb();
	musb_writew(musb->mregs, MUSB_INTRRX, musb->int_rx);
	musb_writew(musb->mregs, MUSB_INTRTX, musb->int_tx);
	musb_writeb(musb->mregs, MUSB_INTRUSB, musb->int_usb);
#ifdef CONFIG_MTK_MUSB_QMU_SUPPORT
	if (musb->int_queue) {
		musb_writel(musb->mregs, MUSB_QISAR, musb->int_queue);
		musb->int_queue &= ~(musb_readl(musb->mregs, MUSB_QIMR));
	}
#endif
	/* musb_read_clear_generic_interrupt */

#ifdef CONFIG_MTK_MUSB_QMU_SUPPORT
	if (musb->int_usb || musb->int_tx || musb->int_rx || musb->int_queue)
		retval = musb_interrupt(musb);
#else
	if (musb->int_usb || musb->int_tx || musb->int_rx)
		retval = musb_interrupt(musb);
#endif

	return retval;
}

static irqreturn_t mt_usb_interrupt(int irq, void *dev_id)
{
	irqreturn_t tmp_status;
	irqreturn_t status = IRQ_NONE;
	struct musb *musb = (struct musb *)dev_id;
	u32 usb_l1_ints;
	unsigned long flags;

	spin_lock_irqsave(&musb->lock, flags);
	usb_l1_ints = musb_readl(musb->mregs, USB_L1INTS) &
		musb_readl(mtk_musb->mregs, USB_L1INTM);
	DBG(1, "usb interrupt assert %x %x  %x %x %x %x %x\n", usb_l1_ints,
	    musb_readl(mtk_musb->mregs, USB_L1INTM),
	    musb_readb(musb->mregs, MUSB_INTRUSBE),
		musb_readw(musb->mregs, MUSB_INTRTX),
		musb_readw(musb->mregs, MUSB_INTRTXE),
		musb_readw(musb->mregs, MUSB_INTRRX),
		musb_readw(musb->mregs, MUSB_INTRRXE));

	if ((usb_l1_ints & TX_INT_STATUS) || (usb_l1_ints & RX_INT_STATUS)
	    || (usb_l1_ints & USBCOM_INT_STATUS)
#ifdef CONFIG_MTK_MUSB_QMU_SUPPORT
	    || (usb_l1_ints & QINT_STATUS)
#endif
	   ) {
		tmp_status = generic_interrupt(irq, musb);
		if (tmp_status != IRQ_NONE)
			status = tmp_status;
	}
	spin_unlock_irqrestore(&musb->lock, flags);

	/* FIXME, workaround for device_qmu + host_dma */
/* #ifndef CONFIG_MTK_MUSB_QMU_SUPPORT */
	if (usb_l1_ints & DMA_INT_STATUS) {
		tmp_status = dma_controller_irq(irq, musb->dma_controller);
		if (tmp_status != IRQ_NONE)
			status = tmp_status;
	}

	return status;

}

extern bool usb_data_enabled;
extern void set_otg_vbus(bool val);
void usb_notify_control(bool data_enabled)
{
	usb_data_enabled = data_enabled;
	pr_err("SXX, usb_data_enabled = %d\n",usb_data_enabled);

	if (cable_mode != usb_data_enabled) {
			cable_mode = usb_data_enabled;
			mt_usb_reconnect();
			/* let conection work do its job */
			msleep(50);
	}

	set_otg_vbus(usb_data_enabled);

}
EXPORT_SYMBOL(usb_notify_control);

static bool saving_mode;

static ssize_t saving_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	if (!dev) {
		DBG(0, "dev is null!!\n");
		return 0;
	}
	return scnprintf(buf, PAGE_SIZE, "%d\n", saving_mode);
}

static ssize_t saving_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	int saving;
	long tmp_val;

	if (!dev) {
		DBG(0, "dev is null!!\n");
		return count;
	/* } else if (1 == sscanf(buf, "%d", &saving)) { */
	} else if (kstrtol(buf, 10, (long *)&tmp_val) == 0) {
		saving = tmp_val;
		DBG(0, "old=%d new=%d\n", saving, saving_mode);
		if (saving_mode == (!saving))
			saving_mode = !saving_mode;
	}
	return count;
}

bool is_saving_mode(void)
{
	DBG(0, "%d\n", saving_mode);
	return saving_mode;
}
EXPORT_SYMBOL(is_saving_mode);

void usb_dump_debug_register(void)
{
	struct musb *musb = mtk_musb;

	usb_enable_clock(true);

	/* 1:Read 0x11200620; */
	pr_notice("[IPI USB dump]addr: 0x620, value: %x\n",
					musb_readl(musb->mregs, 0x620));

	/* 2: set 0x11200600[5:0]  = 0x23; */
	/* Read 0x11200634; */
	musb_writew(musb->mregs, 0x600, 0x23);
	pr_notice("[IPI USB dump]addr: 0x634, 0x23 value: %x\n",
					musb_readl(musb->mregs, 0x634));

	/* 3: set 0x11200600[5:0]  = 0x24; */
	/* Read 0x11200634; */
	musb_writew(musb->mregs, 0x600, 0x24);
	pr_notice("[IPI USB dump]addr: 0x634, 0x24 value: %x\n",
					musb_readl(musb->mregs, 0x634));

	/* 4:set 0x11200600[5:0]  = 0x25; */
	/* Read 0x11200634; */
	musb_writew(musb->mregs, 0x600, 0x25);
	pr_notice("[IPI USB dump]addr: 0x634, 0x25 value: %x\n",
					musb_readl(musb->mregs, 0x634));

	/* 5:set 0x11200600[5:0]  = 0x26; */
	/* Read 0x11200634; */
	musb_writew(musb->mregs, 0x600, 0x26);
	pr_notice("[IPI USB dump]addr: 0x634, 0x26 value: %x\n",
					musb_readl(musb->mregs, 0x634));

	usb_enable_clock(false);
}

DEVICE_ATTR_RW(saving);

#ifdef CONFIG_MTK_UART_USB_SWITCH
static void uart_usb_switch_dump_register(void)
{
	usb_enable_clock(true);

#ifdef CONFIG_MTK_MUSB_PHY
	/* Todo: should phase out: not supported by tphy */
	DBG(0, "[MUSB]addr: 0x68, value: %x\n"
			"[MUSB]addr: 0x6C, value: %x\n"
			"[MUSB]addr: 0x20, value: %x\n"
			"[MUSB]addr: 0x18, value: %x\n",
			USBPHY_READ32(0x68),
			USBPHY_READ32(0x6C),
			USBPHY_READ32(0x20),
			USBPHY_READ32(0x18));
#endif

	usb_enable_clock(false);
	DBG(0, "[MUSB]GPIO_SEL=%x\n", GET_GPIO_SEL_VAL(readl(ap_gpio_base)));
}

static ssize_t portmode_show(struct device *dev,
						struct device_attribute *attr,
						char *buf)
{
	if (!dev) {
		DBG(0, "dev is null!!\n");
		return 0;
	}
	usb_prepare_enable_clock(true);

	in_uart_mode = usb_phy_check_in_uart_mode();
	if (in_uart_mode)
		port_mode = PORT_MODE_UART;
	else
		port_mode = PORT_MODE_USB;

	if (port_mode == PORT_MODE_USB)
		DBG(0, "\nUSB Port mode -> USB\n");
	else if (port_mode == PORT_MODE_UART)
		DBG(0, "\nUSB Port mode -> UART\n");

	uart_usb_switch_dump_register();

	usb_prepare_enable_clock(false);

	return scnprintf(buf, PAGE_SIZE, "%d\n", port_mode);
}

static ssize_t portmode_store(struct device *dev,
					struct device_attribute *attr,
				     const char *buf, size_t count)
{
	unsigned int portmode;

	in_uart_mode = usb_phy_check_in_uart_mode();
	if (in_uart_mode)
		port_mode = PORT_MODE_UART;

	if (!dev) {
		DBG(0, "dev is null!!\n");
		return count;
	} else if (kstrtouint(buf, 10, &portmode) == 0) {
		usb_prepare_enable_clock(true);
		DBG(0,
		"\nUSB Port mode: current => %d (port_mode), change to => %d (portmode)\n",
		    port_mode, portmode);
		if (portmode >= PORT_MODE_MAX)
			portmode = PORT_MODE_USB;

		if (port_mode != portmode) {
			/* Changing to USB Mode */
			if (portmode == PORT_MODE_USB) {
				DBG(0, "USB Port mode -> USB\n");
				usb_phy_switch_to_usb();
				/* Changing to UART Mode */
			} else if (portmode == PORT_MODE_UART) {
				DBG(0, "USB Port mode -> UART\n");
				usb_phy_switch_to_uart();
			}
			uart_usb_switch_dump_register();
			port_mode = portmode;
		}
		usb_prepare_enable_clock(false);
	}
	return count;
}

DEVICE_ATTR_RW(portmode);

#endif

#ifndef FPGA_PLATFORM
static struct device_attribute *mt_usb_attributes[] = {
	&dev_attr_saving,
#ifdef CONFIG_MTK_UART_USB_SWITCH
	&dev_attr_portmode,
#endif
	NULL
};

static int init_sysfs(struct device *dev)
{
	struct device_attribute **attr;
	int rc;

	for (attr = mt_usb_attributes; *attr; attr++) {
		rc = device_create_file(dev, *attr);
		if (rc)
			goto out_unreg;
	}

	return 0;

out_unreg:
	for (; attr >= mt_usb_attributes; attr--)
		device_remove_file(dev, *attr);
	return rc;

}
#endif

#ifdef FPGA_PLATFORM
static struct i2c_client *usb_i2c_client;
static const struct i2c_device_id usb_i2c_id[] = { {"mtk-usb", 0}, {} };

void USB_PHY_Write_Register8(u8 var, u8 addr)
{
	char buffer[2];

	buffer[0] = addr;
	buffer[1] = var;
	i2c_master_send(usb_i2c_client, buffer, 2);
}

u8 USB_PHY_Read_Register8(u8 addr)
{
	u8 var;

	i2c_master_send(usb_i2c_client, &addr, 1);
	i2c_master_recv(usb_i2c_client, &var, 1);
	return var;
}

#define U3_PHY_PAGE 0xff

void _u3_write_bank(u32 value)
{
	USB_PHY_Write_Register8((u8)value, (u8)U3_PHY_PAGE);
}

u32 _u3_read_reg(u32 address)
{
	u8 databuffer = 0;

	databuffer = USB_PHY_Read_Register8((u8)address);
	return databuffer;
}

void _u3_write_reg(u32 address, u32 value)
{
	USB_PHY_Write_Register8((u8)value, (u8)address);
}

u32 u3_phy_read_reg32(u32 addr)
{
	u32 bank;
	u32 addr8;
	u32 data;

	bank = (addr >> 16) & 0xff;
	addr8 = addr & 0xff;

	_u3_write_bank(bank);
	data = _u3_read_reg(addr8);
	data |= (_u3_read_reg(addr8 + 1) << 8);
	data |= (_u3_read_reg(addr8 + 2) << 16);
	data |= (_u3_read_reg(addr8 + 3) << 24);
	return data;
}

u32 u3_phy_write_reg32(u32 addr, u32 data)
{
	u32 bank;
	u32 addr8;
	u32 data_0, data_1, data_2, data_3;

	bank = (addr >> 16) & 0xff;
	addr8 = addr & 0xff;
	data_0 = data & 0xff;
	data_1 = (data >> 8) & 0xff;
	data_2 = (data >> 16) & 0xff;
	data_3 = (data >> 24) & 0xff;

	_u3_write_bank(bank);
	_u3_write_reg(addr8, data_0);
	_u3_write_reg(addr8 + 1, data_1);
	_u3_write_reg(addr8 + 2, data_2);
	_u3_write_reg(addr8 + 3, data_3);

	return 0;
}

void u3_phy_write_field32(int addr, int offset, int mask, int value)
{
	u32 cur_value;
	u32 new_value;

	cur_value = u3_phy_read_reg32(addr);
	new_value = (cur_value & (~mask)) | ((value << offset) & mask);

	u3_phy_write_reg32(addr, new_value);
}

u32 u3_phy_write_reg8(u32 addr, u8 data)
{
	u32 bank;
	u32 addr8;

	bank = (addr >> 16) & 0xff;
	addr8 = addr & 0xff;
	_u3_write_bank(bank);
	_u3_write_reg(addr8, data);

	return 0;
}

static int usb_i2c_probe(struct i2c_client *client,
						const struct i2c_device_id *id)
{
	void __iomem *base;
	u32 val = 0;
	/* if i2c probe before musb prob, this would cause KE */
	/* base = (unsigned long)((unsigned long)mtk_musb->xceiv->io_priv); */
	DBG(0, "[MUSB]%, start, base:%p\n", __func__, base);

	usb_i2c_client = client;

	/* disable usb mac suspend */
	val = musb_readl(base, 0x868);
	DBG(0, "[MUSB]0x868=0x%x\n", val);

	musb_writel(base, 0x868, (val & ~(0x4 << 16)));

	DBG(0, "[MUSB]0x868=0x%x\n"
			"[MUSB]addr: 0xFF, value: %x\n",
			musb_readl(base, 0x868),
			USB_PHY_Read_Register8(0xFF));

	USB_PHY_Write_Register8(0x20, 0xFF);

	DBG(0, "[MUSB]version=[%02x %02x %02x %02x]\n",
		USB_PHY_Read_Register8(0xE4),
		USB_PHY_Read_Register8(0xE5),
		USB_PHY_Read_Register8(0xE6),
		USB_PHY_Read_Register8(0xE7));

	if (USB_PHY_Read_Register8(0xE7) == 0xa) {
		static struct u3phy_info info;

		DBG(0, "[A60801A] Phy version is %x\n",
					u3_phy_read_reg32(0x2000e4));

		info.u2phy_regs_a = (struct u2phy_reg_a *)0x0;
		info.u3phyd_regs_a = (struct u3phyd_reg_a *)0x100000;
		info.u3phyd_bank2_regs_a =
					(struct u3phyd_bank2_reg_a *)0x200000;
		info.u3phya_regs_a = (struct u3phya_reg_a *)0x300000;
		info.u3phya_da_regs_a = (struct u3phya_da_reg_a *)0x400000;
		info.sifslv_chip_regs_a = (struct sifslv_chip_reg_a *)0x500000;
		info.spllc_regs_a = (struct spllc_reg_a *)0x600000;
		info.sifslv_fm_regs_a = (struct sifslv_fm_reg_a *)0xf00000;

		/* BANK 0x00 */
		/* for U2 hS eye diagram */
		u3_phy_write_field32(((phys_addr_t)(uintptr_t)
			&info.u2phy_regs_a->usbphyacr1)
			, A60810_RG_USB20_TERM_VREF_SEL_OFST
			, A60810_RG_USB20_TERM_VREF_SEL
			, 0x05);
		/* for U2 hS eye diagram */
		u3_phy_write_field32(((phys_addr_t)(uintptr_t)
			&info.u2phy_regs_a->usbphyacr1)
			, A60810_RG_USB20_VRT_VREF_SEL_OFST
			, A60810_RG_USB20_VRT_VREF_SEL
			, 0x05);
		/* for U2 sensititvity */
		u3_phy_write_field32(((phys_addr_t)(uintptr_t)
			&info.u2phy_regs_a->usbphyacr6)
			, A60810_RG_USB20_SQTH_OFST
			, A60810_RG_USB20_SQTH
			, 0x04);

		/* BANK 0x10 */
		/* disable ssusb_p3_entry to work around resume from P3 bug */
		u3_phy_write_field32(((phys_addr_t)(uintptr_t)
			&info.u3phyd_regs_a->phyd_lfps0)
			, A60810_RG_SSUSB_P3_ENTRY_OFST
			, A60810_RG_SSUSB_P3_ENTRY
			, 0x00);
		/* force disable ssusb_p3_entry to
		 * work around resume from P3 bug
		 */
		u3_phy_write_field32(((phys_addr_t)(uintptr_t)
			&info.u3phyd_regs_a->phyd_lfps0)
			, A60810_RG_SSUSB_P3_ENTRY_SEL_OFST
			, A60810_RG_SSUSB_P3_ENTRY_SEL
			, 0x01);

		/* BANK 0x40 */
		/* fine tune SSC delta1 to let SSC min average ~0ppm */
		u3_phy_write_field32(((phys_addr_t)(uintptr_t)
			&info.u3phya_da_regs_a->reg19)
			, A60810_RG_SSUSB_PLL_SSC_DELTA1_U3_OFST
			, A60810_RG_SSUSB_PLL_SSC_DELTA1_U3
			, 0x46);
		/* U3PhyWriteField32(((u32)&info.u3phya_da_regs_a->reg19) */
		u3_phy_write_field32(((phys_addr_t)(uintptr_t)
			&info.u3phya_da_regs_a->reg21)
			, A60810_RG_SSUSB_PLL_SSC_DELTA1_PE1H_OFST
			, A60810_RG_SSUSB_PLL_SSC_DELTA1_PE1H
			, 0x40);

		/* fine tune SSC delta to let SSC min average ~0ppm */

		/* Fine tune SYSPLL to improve phase noise */
		/* I2C  60    0x08[01:00]	0x03
		 * RW  RG_SSUSB_PLL_BC_U3
		 */
		u3_phy_write_field32(((phys_addr_t)(uintptr_t)
			&info.u3phya_da_regs_a->reg4)
			, A60810_RG_SSUSB_PLL_BC_U3_OFST
			, A60810_RG_SSUSB_PLL_BC_U3
			, 0x3);
		/* I2C  60    0x08[12:10]	0x03
		 * RW  RG_SSUSB_PLL_DIVEN_U3
		 */
		u3_phy_write_field32(((phys_addr_t)(uintptr_t)
			&info.u3phya_da_regs_a->reg4)
			, A60810_RG_SSUSB_PLL_DIVEN_U3_OFST
			, A60810_RG_SSUSB_PLL_DIVEN_U3
			, 0x3);
		/* I2C  60    0x0C[03:00]	0x01   RW  RG_SSUSB_PLL_IC_U3 */
		u3_phy_write_field32(((phys_addr_t)(uintptr_t)
			&info.u3phya_da_regs_a->reg5)
			, A60810_RG_SSUSB_PLL_IC_U3_OFST
			, A60810_RG_SSUSB_PLL_IC_U3
			, 0x1);
		/* I2C  60    0x0C[23:22]	0x01   RW  RG_SSUSB_PLL_BR_U3 */
		u3_phy_write_field32(((phys_addr_t)(uintptr_t)
			&info.u3phya_da_regs_a->reg5)
			, A60810_RG_SSUSB_PLL_BR_U3_OFST
			, A60810_RG_SSUSB_PLL_BR_U3
			, 0x1);
		/* I2C  60    0x10[03:00]	0x01
		 * RW  RG_SSUSB_PLL_IR_U3
		 */
		u3_phy_write_field32(((phys_addr_t)(uintptr_t)
			&info.u3phya_da_regs_a->reg6)
			, A60810_RG_SSUSB_PLL_IR_U3_OFST
			, A60810_RG_SSUSB_PLL_IR_U3
			, 0x1);
		/* I2C  60    0x14[03:00]	0x0F   RW  RG_SSUSB_PLL_BP_U3 */
		u3_phy_write_field32(((phys_addr_t)(uintptr_t)
			&info.u3phya_da_regs_a->reg7)
			, A60810_RG_SSUSB_PLL_BP_U3_OFST
			, A60810_RG_SSUSB_PLL_BP_U3
			, 0x0f);

		/* BANK 0x60 */
		/* force xtal pwd mode enable */
		u3_phy_write_field32(((phys_addr_t)(uintptr_t)
			&info.spllc_regs_a->u3d_xtalctl_2)
			, A60810_RG_SSUSB_FORCE_XTAL_PWD_OFST
			, A60810_RG_SSUSB_FORCE_XTAL_PWD
			, 0x1);
		/* force bias pwd mode enable */
		u3_phy_write_field32(((phys_addr_t)(uintptr_t)
			&info.spllc_regs_a->u3d_xtalctl_2)
			, A60810_RG_SSUSB_FORCE_BIAS_PWD_OFST
			, A60810_RG_SSUSB_FORCE_BIAS_PWD
			, 0x1);
		/* force xtal pwd mode off to work around xtal drv de */
		u3_phy_write_field32(((phys_addr_t)(uintptr_t)
			&info.spllc_regs_a->u3d_xtalctl_2)
			, A60810_RG_SSUSB_XTAL_PWD_OFST
			, A60810_RG_SSUSB_XTAL_PWD
			, 0x0);
		/* force bias pwd mode off to work around xtal drv de */
		u3_phy_write_field32(((phys_addr_t)(uintptr_t)
			&info.spllc_regs_a->u3d_xtalctl_2)
			, A60810_RG_SSUSB_BIAS_PWD_OFST
			, A60810_RG_SSUSB_BIAS_PWD
			, 0x0);

		/********* test chip settings ***********/
		/* BANK 0x00 */
		/* slew rate setting */
		u3_phy_write_field32(((phys_addr_t)(uintptr_t)
			&info.u2phy_regs_a->usbphyacr5)
			, A60810_RG_USB20_HSTX_SRCTRL_OFST
			, A60810_RG_USB20_HSTX_SRCTRL
			, 0x4);

		/* BANK 0x50 */

		/* PIPE setting  BANK5 */
		/* PIPE drv = 2 */
		u3_phy_write_reg8(((phys_addr_t)(uintptr_t)
				&info.sifslv_chip_regs_a->gpio_ctla) + 2, 0x10);
		/* PIPE phase */
		/* U3PhyWriteReg8(((u32)&info.sifslv_chip_regs_a->gpio_ctla)+3,
		 * 0xdc);
		 */
		u3_phy_write_reg8(((phys_addr_t)(uintptr_t)
				&info.sifslv_chip_regs_a->gpio_ctla) + 3, 0x24);
	} else {
		USB_PHY_Write_Register8(0x00, 0xFF);

		DBG(0, "[MUSB]addr: 0xFF, value: %x\n",
				USB_PHY_Read_Register8(0xFF));

		/* usb phy initial sequence */
		USB_PHY_Write_Register8(0x00, 0xFF);
		USB_PHY_Write_Register8(0x04, 0x61);
		USB_PHY_Write_Register8(0x00, 0x68);
		USB_PHY_Write_Register8(0x00, 0x6a);
		USB_PHY_Write_Register8(0x6e, 0x00);
		USB_PHY_Write_Register8(0x0c, 0x1b);
		USB_PHY_Write_Register8(0x44, 0x08);
		USB_PHY_Write_Register8(0x55, 0x11);
		USB_PHY_Write_Register8(0x68, 0x1a);


		DBG(0, "[MUSB]addr: 0xFF, value: %x\n"
				"[MUSB]addr: 0x61, value: %x\n"
				"[MUSB]addr: 0x68, value: %x\n"
				"[MUSB]addr: 0x6a, value: %x\n"
				"[MUSB]addr: 0x00, value: %x\n"
				"[MUSB]addr: 0x1b, value: %x\n"
				"[MUSB]addr: 0x08, value: %x\n"
				"[MUSB]addr: 0x11, value: %x\n"
				"[MUSB]addr: 0x1a, value: %x\n",
				USB_PHY_Read_Register8(0xFF),
				USB_PHY_Read_Register8(0x61),
				USB_PHY_Read_Register8(0x68),
				USB_PHY_Read_Register8(0x6a),
				USB_PHY_Read_Register8(0x00),
				USB_PHY_Read_Register8(0x1b),
				USB_PHY_Read_Register8(0x08),
				USB_PHY_Read_Register8(0x11),
				USB_PHY_Read_Register8(0x1a));
	}

	DBG(0, "[MUSB]%s, end\n", __func__);
	return 0;

}

static int usb_i2c_remove(struct i2c_client *client)
{
	return 0;
}

static const struct of_device_id usb_of_match[] = {
	{.compatible = "mediatek,mtk-usb"},
	{},
};

struct i2c_driver usb_i2c_driver = {
	.probe = usb_i2c_probe,
	.remove = usb_i2c_remove,
	.driver = {
		.name = "mtk-usb",
		.of_match_table = usb_of_match,
	},
	.id_table = usb_i2c_id,
};

static int add_usb_i2c_driver(void)
{
	DBG(0, "%s\n", __func__);

	if (i2c_add_driver(&usb_i2c_driver) != 0) {
		DBG(0, "[MUSB]usb_i2c_driver initialization failed!!\n");
		return -1;
	}
	DBG(0, "[MUSB]usb_i2c_driver initialization succeed!!\n");
	return 0;
}
#endif				/* End of FPGA_PLATFORM */


static int __init mt_usb_init(struct musb *musb)
{
	int ret;

	DBG(1, "%s\n", __func__);

#ifdef CONFIG_PHY_MTK_TPHY
	musb->phy = glue->phy;
	musb->xceiv = glue->xceiv;
#endif
	musb->dma_irq = (int)SHARE_IRQ;
	musb->fifo_cfg = fifo_cfg;
	musb->fifo_cfg_size = ARRAY_SIZE(fifo_cfg);
	musb->dyn_fifo = true;
	musb->power = false;
	musb->is_host = false;
	musb->fifo_size = 8 * 1024;
	musb->usb_lock = wakeup_source_register(NULL, "USB suspend lock");

#ifdef CONFIG_PHY_MTK_TPHY
	ret = phy_init(glue->phy);
	if (ret)
		goto err_phy_init;
#endif

#ifdef CONFIG_MTK_UART_USB_SWITCH
	in_uart_mode = usb_phy_check_in_uart_mode();
	if (in_uart_mode) {
		glue->phy_mode = PHY_MODE_UART;
		DBG(0, "At UART mode. Switch to USB is not support\n");
	}
#endif

#ifdef CONFIG_PHY_MTK_TPHY
	phy_set_mode(glue->phy, glue->phy_mode);

	if (glue->phy_mode != PHY_MODE_UART)
		ret = phy_power_on(glue->phy);

	if (ret)
		goto err_phy_power_on;
#endif
#ifndef FPGA_PLATFORM
	reg_vusb = regulator_get(musb->controller, "vusb");
	if (!IS_ERR(reg_vusb)) {
#ifdef NEVER
#define	VUSB33_VOL_MIN 3070000
#define	VUSB33_VOL_MAX 3070000
		ret = regulator_set_voltage(reg_vusb,
					VUSB33_VOL_MIN, VUSB33_VOL_MAX);
		if (ret < 0)
			pr_notice("regulator set vol failed: %d\n", ret);
		else
			DBG(0, "regulator set vol ok, <%d,%d>\n",
					VUSB33_VOL_MIN, VUSB33_VOL_MAX);
#endif /* NEVER */
		ret = regulator_enable(reg_vusb);
		if (ret < 0) {
			pr_notice("regulator_enable vusb failed: %d\n", ret);
			regulator_put(reg_vusb);
		}
	} else
		pr_notice("regulator_get vusb failed\n");


	reg_va12 = regulator_get(musb->controller, "va12");
	if (!IS_ERR(reg_va12)) {
		ret = regulator_enable(reg_va12);
		if (ret < 0) {
			pr_notice("regulator_enable va12 failed: %d\n", ret);
			regulator_put(reg_va12);
		}
	} else
		pr_notice("regulator_get va12 failed\n");

#endif

	/* ret = device_create_file(musb->controller, &dev_attr_cmode); */

	/* mt_usb_enable(musb); */

	musb->isr = mt_usb_interrupt;
	musb_writel(musb->mregs,
			MUSB_HSDMA_INTR, 0xff |
			(0xff << DMA_INTR_UNMASK_SET_OFFSET));
	DBG(1, "musb platform init %x\n",
			musb_readl(musb->mregs, MUSB_HSDMA_INTR));

#ifdef CONFIG_MTK_MUSB_QMU_SUPPORT
	/* FIXME, workaround for device_qmu + host_dma */
	musb_writel(musb->mregs,
			USB_L1INTM,
		    TX_INT_STATUS |
		    RX_INT_STATUS |
		    USBCOM_INT_STATUS |
		    DMA_INT_STATUS |
		    QINT_STATUS);
#else
	musb_writel(musb->mregs,
			USB_L1INTM,
		    TX_INT_STATUS |
		    RX_INT_STATUS |
		    USBCOM_INT_STATUS |
		    DMA_INT_STATUS);
#endif
#if defined(CONFIG_MTK_BASE_POWER)
	timer_setup(&musb->idle_timer, musb_do_idle, 0);
#endif
#ifdef CONFIG_USB_MTK_OTG
	mt_usb_otg_init(musb);
	/* enable host suspend mode */
	mt_usb_wakeup_init(musb);
	musb->host_suspend = true;
#endif
#ifdef CONFIG_MACH_MT6761
	/* only for mt6761 */
	usb_sram_init();
#endif

	return 0;

#ifdef CONFIG_PHY_MTK_TPHY
err_phy_power_on:
	phy_exit(glue->phy);
err_phy_init:
#endif
	//mt_usb_psy_init(musb);
	return ret;
}

static int mt_usb_exit(struct musb *musb)
{
	del_timer_sync(&musb->idle_timer);
#ifndef FPGA_PLATFORM
	if (reg_vusb) {
		regulator_disable(reg_vusb);
		regulator_put(reg_vusb);
		reg_vusb = NULL;
	}
	if (reg_va12) {
		regulator_disable(reg_va12);
		regulator_put(reg_va12);
		reg_va12 = NULL;
	}
#endif
#ifdef CONFIG_USB_MTK_OTG
	mt_usb_otg_exit(musb);
#endif

#ifdef CONFIG_PHY_MTK_TPHY
	phy_power_off(glue->phy);
	phy_exit(glue->phy);
#endif

	return 0;
}

static void mt_usb_enable_clk(struct musb *musb)
{
	usb_enable_clock(true);
}

static void mt_usb_disable_clk(struct musb *musb)
{
	usb_enable_clock(false);
}

static void mt_usb_prepare_clk(struct musb *musb)
{
	usb_prepare_clock(true);
}

static void mt_usb_unprepare_clk(struct musb *musb)
{
	usb_prepare_clock(false);
}

static const struct musb_platform_ops mt_usb_ops = {
	.init = mt_usb_init,
	.exit = mt_usb_exit,
	/*.set_mode     = mt_usb_set_mode, */
#if defined(CONFIG_MTK_BASE_POWER)
	.try_idle = mt_usb_try_idle,
#endif
	.enable = mt_usb_enable,
	.disable = mt_usb_disable,
	/* .set_vbus = mt_usb_set_vbus, */
	.vbus_status = mt_usb_get_vbus_status,
	.enable_clk =  mt_usb_enable_clk,
	.disable_clk =  mt_usb_disable_clk,
	.prepare_clk = mt_usb_prepare_clk,
	.unprepare_clk = mt_usb_unprepare_clk,
#ifdef CONFIG_USB_MTK_OTG
	.enable_wakeup = mt_usb_wakeup,
#endif
};

#ifdef CONFIG_MTK_MUSB_DRV_36BIT
static u64 mt_usb_dmamask = DMA_BIT_MASK(36);
#else
static u64 mt_usb_dmamask = DMA_BIT_MASK(32);
#endif

struct mt_usb_glue *glue;
EXPORT_SYMBOL(glue);

static int mt_usb_probe(struct platform_device *pdev)
{
	struct musb_hdrc_platform_data *pdata = pdev->dev.platform_data;
	struct platform_device *musb_pdev;
	struct musb_hdrc_config *config;
	struct device_node *np = pdev->dev.of_node;
#ifdef CONFIG_MTK_UART_USB_SWITCH
	struct device_node *ap_gpio_node = NULL;
#endif
#ifdef CONFIG_MTK_MUSB_DUAL_ROLE
	struct otg_switch_mtk *otg_sx;
#endif
	int ret = -ENOMEM;

	glue = kzalloc(sizeof(*glue), GFP_KERNEL);
	if (!glue)
		goto err0;

	/* Device name is required */
	musb_pdev = platform_device_alloc("musb-hdrc", PLATFORM_DEVID_NONE);
	if (!musb_pdev) {
		dev_notice(&pdev->dev, "failed to allocate musb pdev\n");
		goto err1;
	}

#ifdef CONFIG_PHY_MTK_TPHY
	glue->phy = devm_of_phy_get_by_index(&pdev->dev, np, 0);
	if (IS_ERR(glue->phy)) {
		dev_err(&pdev->dev, "fail to getting phy %ld\n",
			PTR_ERR(glue->phy));
		return PTR_ERR(glue->phy);
	}

	glue->usb_phy = usb_phy_generic_register();
	if (IS_ERR(glue->usb_phy)) {
		dev_err(&pdev->dev, "fail to registering usb-phy %ld\n",
			PTR_ERR(glue->usb_phy));
		return PTR_ERR(glue->usb_phy);
	}
	glue->xceiv = devm_usb_get_phy(&pdev->dev, USB_PHY_TYPE_USB2);
	if (IS_ERR(glue->xceiv)) {
		dev_err(&pdev->dev, "fail to getting usb-phy %d\n", ret);
		ret = PTR_ERR(glue->xceiv);
		goto err_unregister_usb_phy;
	}
#endif
	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_notice(&pdev->dev, "failed to allocate musb platform data\n");
		goto err2;
	}

	config = devm_kzalloc(&pdev->dev, sizeof(*config), GFP_KERNEL);
	if (!config) {
		/* dev_notice(&pdev->dev,
		 * "failed to allocate musb hdrc config\n");
		 */
		goto err2;
	}

#ifdef CONFIG_MTK_UART_USB_SWITCH
	ap_gpio_node =
		of_find_compatible_node(NULL, NULL, AP_GPIO_COMPATIBLE_NAME);

	if (ap_gpio_node == NULL) {
		dev_notice(&pdev->dev, "USB get ap_gpio_node failed\n");
		if (ap_gpio_base)
			iounmap(ap_gpio_base);
		ap_gpio_base = 0;
	} else {
		ap_gpio_base = of_iomap(ap_gpio_node, 0);
		ap_gpio_base += RG_GPIO_SELECT;
	}
#endif

	of_property_read_u32(np, "num_eps", (u32 *) &config->num_eps);
	config->multipoint = of_property_read_bool(np, "multipoint");

	pdata->config = config;

	musb_pdev->dev.parent = &pdev->dev;
	musb_pdev->dev.dma_mask = &mt_usb_dmamask;
	musb_pdev->dev.coherent_dma_mask = mt_usb_dmamask;

	pdev->dev.dma_mask = &mt_usb_dmamask;
	pdev->dev.coherent_dma_mask = mt_usb_dmamask;
	arch_setup_dma_ops(&musb_pdev->dev, 0, mt_usb_dmamask, NULL, 0);

	glue->dev = &pdev->dev;
	glue->musb_pdev = musb_pdev;

	pdata->platform_ops = &mt_usb_ops;

	/*
	 * Don't use the name from dtsi, like "11200000.usb0".
	 * So modify the device name. And rc can use the same path for
	 * all platform, like "/sys/devices/platform/mt_usb/".
	 */
	ret = device_rename(&pdev->dev, "mt_usb");
	if (ret)
		dev_notice(&pdev->dev, "failed to rename\n");
	/*
	 * fix uaf(use afer free) issue:backup pdev->name,
	 * device_rename will free pdev->name
	 */
	pdev->name = pdev->dev.kobj.name;

	platform_set_drvdata(pdev, glue);

	ret = platform_device_add_resources(musb_pdev,
				pdev->resource, pdev->num_resources);
	if (ret) {
		dev_notice(&pdev->dev, "failed to add resources\n");
		goto err2;
	}

#ifdef CONFIG_MTK_MUSB_QMU_SUPPORT
	isoc_ep_end_idx = 1;
/*huaqin add MTK patch for cts test audio usb record fail by limengxia at 2021/3/12 start*/
	isoc_ep_gpd_count = 550; /* 30 ms for HS, at most (30*8 + 1) */
/*huaqin add MTK patch for cts test audio usb record fail by limengxia at 2021/3/12 end*/

	mtk_host_qmu_force_isoc_restart = 0;
#endif
#ifndef FPGA_PLATFORM
#if defined(CONFIG_MTK_BASE_POWER)
	register_usb_hal_dpidle_request(usb_6765_dpidle_request);
#endif
#endif
	register_usb_hal_disconnect_check(trigger_disconnect_check_work);

	INIT_DELAYED_WORK(&idle_work, do_idle_work);

	DBG(0, "keep musb->power & mtk_usb_power in the same value\n");
	mtk_usb_power = false;

#ifndef FPGA_PLATFORM
	glue->musb_clk = devm_clk_get(&pdev->dev, "usb0");
	if (IS_ERR(glue->musb_clk)) {
		DBG(0, "cannot get musb_clk clock\n");
		goto err2;
	}

	glue->musb_clk_top_sel = devm_clk_get(&pdev->dev, "usb0_clk_top_sel");
	if (IS_ERR(glue->musb_clk_top_sel)) {
		DBG(0, "cannot get musb_clk_top_sel clock\n");
		goto err2;
	}

	glue->musb_clk_univpll3_d4 =
		devm_clk_get(&pdev->dev, "usb0_clk_univpll3_d4");
	if (IS_ERR(glue->musb_clk_univpll3_d4)) {
		DBG(0, "cannot get musb_clk_univpll3_d4 clock\n");
		goto err2;
	}

	if (init_sysfs(&pdev->dev)) {
		DBG(0, "failed to init_sysfs\n");
		goto err2;
	}

#ifdef CONFIG_USB_MTK_OTG
	pdata->dr_mode = usb_get_dr_mode(&pdev->dev);
#else
	of_property_read_u32(np, "dr_mode", (u32 *) &pdata->dr_mode);
#endif

#ifdef CONFIG_PHY_MTK_TPHY
	switch (pdata->dr_mode) {
	case USB_DR_MODE_HOST:
		glue->phy_mode = PHY_MODE_USB_HOST;
		break;
	case USB_DR_MODE_PERIPHERAL:
		glue->phy_mode = PHY_MODE_USB_DEVICE;
		break;
	case USB_DR_MODE_OTG:
		glue->phy_mode = PHY_MODE_USB_OTG;
		break;
	default:
		dev_err(&pdev->dev, "Error 'dr_mode' property\n");
		return -EINVAL;
	}

	DBG(0, "get dr_mode: %d\n", pdata->dr_mode);
#endif

#ifdef CONFIG_MTK_MUSB_DUAL_ROLE
	/* assign usb-role-sw */
	otg_sx = &glue->otg_sx;

	otg_sx->manual_drd_enabled =
		of_property_read_bool(np, "enable-manual-drd");
	otg_sx->role_sw_used = of_property_read_bool(np, "usb-role-switch");

	if (!otg_sx->role_sw_used && of_property_read_bool(np, "extcon")) {
		otg_sx->edev = extcon_get_edev_by_phandle(&musb_pdev->dev, 0);
		if (IS_ERR(otg_sx->edev)) {
			dev_err(&musb_pdev->dev, "couldn't get extcon device\n");
			return PTR_ERR(otg_sx->edev);
		}
	}
#endif

	ret = platform_device_add_data(musb_pdev, pdata, sizeof(*pdata));
	if (ret) {
		dev_notice(&pdev->dev, "failed to add platform_data\n");
		goto err2;
	}

	ret = platform_device_add(musb_pdev);

	if (ret) {
		dev_notice(&pdev->dev, "failed to register musb device\n");
		goto err2;
	}
#endif /* FPGA_PLATFORM */
	DBG(0, "USB probe done!\n");

#if defined(FPGA_PLATFORM) || defined(FOR_BRING_UP)
	musb_force_on = 1;
#endif

	return 0;

err2:
	platform_device_put(musb_pdev);
	platform_device_unregister(glue->musb_pdev);
#ifdef CONFIG_PHY_MTK_TPHY
err_unregister_usb_phy:
	usb_phy_generic_unregister(glue->usb_phy);
#endif
err1:
	kfree(glue);
err0:
	return ret;
}

static int mt_usb_remove(struct platform_device *pdev)
{
	struct mt_usb_glue *glue = platform_get_drvdata(pdev);
#ifdef CONFIG_PHY_MTK_TPHY
	struct platform_device *usb_phy = glue->usb_phy;
#endif
	platform_device_unregister(glue->musb_pdev);
#ifdef CONFIG_PHY_MTK_TPHY
	usb_phy_generic_unregister(usb_phy);
#endif
	kfree(glue);

	return 0;
}

static struct platform_driver mt_usb_driver = {
	.remove = mt_usb_remove,
	.probe = mt_usb_probe,
	.driver = {
		.name = "mt_usb",
		.of_match_table = apusb_of_ids,
	},
};
module_platform_driver(mt_usb_driver);

static int __init usb20_init(void)
{
	int ret;

	DBG(0, "usb20 init\n");

#ifdef CONFIG_MTK_USB2JTAG_SUPPORT
	if (usb2jtag_mode()) {
		pr_notice("[USB2JTAG] in usb2jtag mode, not to initialize usb driver\n");
		return 0;
	}
#endif
	/* Fix musb_plat build-in */
	/* ret = platform_driver_register(&mt_usb_driver); */
	ret = 0;

#ifdef FPGA_PLATFORM
	add_usb_i2c_driver();
#endif

	DBG(0, "usb20 init ret:%d\n", ret);
	return ret;
}
fs_initcall(usb20_init);

static void __exit usb20_exit(void)
{
	/* Fix musb_plat build-in */
	/* platform_driver_unregister(&mt_usb_driver); */
}
module_exit(usb20_exit);

static int option;
static int set_option(const char *val, const struct kernel_param *kp)
{
	int local_option;
	int rv;

	/* update module parameter */
	rv = param_set_int(val, kp);
	if (rv)
		return rv;

	/* update local_option */
	rv = kstrtoint(val, 10, &local_option);
	if (rv != 0)
		return rv;

	DBG(0, "option:%d, local_option:%d\n", option, local_option);

	switch (local_option) {
	case 0:
		DBG(0, "case %d\n", local_option);
		mt_usb_connect_test(1);
		break;
	case 1:
		DBG(0, "case %d\n", local_option);
		mt_usb_connect_test(0);
		break;
	default:
		break;
	}
	return 0;
}
static struct kernel_param_ops option_param_ops = {
	.set = set_option,
	.get = param_get_int,
};
module_param_cb(option, &option_param_ops, &option, 0644);
