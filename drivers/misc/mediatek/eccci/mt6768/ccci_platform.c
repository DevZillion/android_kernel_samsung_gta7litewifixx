/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
*/

#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#endif
#include <mt-plat/mtk_ccci_common.h>
#include "ccci_config.h"
#include "ccci_modem.h"
#include "ccci_bm.h"
#include "ccci_platform.h"
#include "../hif/ccci_hif_ccif.h"

#ifdef FEATURE_USING_4G_MEMORY_API
#include <mt-plat/mtk_lpae.h>
#endif

#include "modem_secure_base.h"

#include <linux/arm-smccc.h>


#define MTK_SIP_CCCI_CONTROL_ARCH32		0x82000505
#define MTK_SIP_CCCI_CONTROL_ARCH64		0xC2000505

#define TAG "plat"

int Is_MD_EMI_voilation(void)
{
	return 1;
}

unsigned long infra_ao_base;

/*
 * when MD attached its codeviser for debuging, this bit will be set.
 * so CCCI should disable some checkings and
 * operations as MD may not respond to us.
 */
unsigned int ccci_get_md_debug_mode(struct ccci_modem *md)
{
	return 0;
}
EXPORT_SYMBOL(ccci_get_md_debug_mode);

int ccci_get_md_sec_smem_size_and_update(void)
{
#ifdef ENABLE_MD_SEC_SMEM
	struct arm_smccc_res res;

#ifdef __aarch64__
	arm_smccc_smc(MTK_SIP_CCCI_CONTROL_ARCH64,
				UPDATE_MD_SEC_SMEM, 0, 0, 0, 0, 0, 0, &res);
#else
	arm_smccc_smc(MTK_SIP_CCCI_CONTROL_ARCH32,
				UPDATE_MD_SEC_SMEM, 0, 0, 0, 0, 0, 0, &res);
#endif
	CCCI_NORMAL_LOG(-1, TAG,
		"%s:size=0x%x\n", __func__, (int)res.a0);

	return (int)res.a0;
#else

	return 0;
#endif
}
EXPORT_SYMBOL(ccci_get_md_sec_smem_size_and_update);

#ifdef FEATURE_LOW_BATTERY_SUPPORT
static int ccci_md_low_power_notify(struct ccci_modem *md,
	enum LOW_POEWR_NOTIFY_TYPE type, int level)
{
	unsigned int reserve = 0xFFFFFFFF;
	int ret = 0;

	CCCI_NORMAL_LOG(md->index, TAG,
		"low power notification type=%d, level=%d\n", type, level);
	/*
	 * byte3 byte2 byte1 byte0
	 *    0   4G   3G   2G
	 */
	switch (type) {
	case LOW_BATTERY:
		if (level == LOW_BATTERY_LEVEL_0)
			reserve = 0;	/* 0 */
		else if (level == LOW_BATTERY_LEVEL_1
						|| level == LOW_BATTERY_LEVEL_2)
			reserve = (1 << 6);	/* 64 */
		ret = port_proxy_send_msg_to_md(md->port_proxy,
			CCCI_SYSTEM_TX, MD_LOW_BATTERY_LEVEL, reserve, 1);
		if (ret)
			CCCI_ERROR_LOG(md->index, TAG,
			 "send low battery notification fail, ret=%d\n", ret);
		break;
	case BATTERY_PERCENT:
		if (level == BATTERY_PERCENT_LEVEL_0)
			reserve = 0;	/* 0 */
		else if (level == BATTERY_PERCENT_LEVEL_1)
			reserve = (1 << 6);	/* 64 */
		ret = port_proxy_send_msg_to_md(md->port_proxy,
			CCCI_SYSTEM_TX, MD_LOW_BATTERY_LEVEL, reserve, 1);
		if (ret)
			CCCI_ERROR_LOG(md->index, TAG,
			"send battery percent info fail, ret=%d\n", ret);
		break;
	default:
		break;
	};

	return ret;
}

static void ccci_md_low_battery_cb(LOW_BATTERY_LEVEL level)
{
	int idx = 0;
	struct ccci_modem *md;

	for (idx = 0; idx < MAX_MD_NUM; idx++) {
		md = ccci_md_get_modem_by_id(idx);
		if (md != NULL)
			ccci_md_low_power_notify(md, LOW_BATTERY, level);
	}
}

static void ccci_md_battery_percent_cb(BATTERY_PERCENT_LEVEL level)
{
	int idx = 0;
	struct ccci_modem *md;

	for (idx = 0; idx < MAX_MD_NUM; idx++) {
		md = ccci_md_get_modem_by_id(idx);
		if (md != NULL)
			ccci_md_low_power_notify(md, BATTERY_PERCENT, level);
	}
}
#endif

#define PCCIF_BUSY (0x4)
#define PCCIF_TCHNUM (0xC)
#define PCCIF_ACK (0x14)
#define PCCIF_CHDATA (0x100)
#define PCCIF_SRAM_SIZE (512)

void ccci_reset_ccif_hw(unsigned char md_id,
			int ccif_id, void __iomem *baseA, void __iomem *baseB, struct md_ccif_ctrl *md_ctrl)
{
	int i;
	struct ccci_smem_region *region;

	{
		int reset_bit = -1;

		switch (ccif_id) {
		case AP_MD1_CCIF:
			reset_bit = 8;
			break;
		}

		if (reset_bit == -1)
			return;

		/*
		 *this reset bit will clear
		 *CCIF's busy/wch/irq, but not SRAM
		 */
		/*set reset bit*/
		ccci_write32(infra_ao_base, 0x150, 1 << reset_bit);
		/*clear reset bit*/
		ccci_write32(infra_ao_base, 0x154, 1 << reset_bit);
	}

	/* clear SRAM */
	for (i = 0; i < PCCIF_SRAM_SIZE/sizeof(unsigned int); i++) {
		ccci_write32(baseA, PCCIF_CHDATA+i*sizeof(unsigned int), 0);
		ccci_write32(baseB, PCCIF_CHDATA+i*sizeof(unsigned int), 0);
	}

	/* extend from 36bytes to 72bytes in CCIF SRAM */
	/* 0~60bytes for bootup trace,
	 *last 12bytes for magic pattern,smem address and size
	 */
	region = ccci_md_get_smem_by_user_id(md_id,
		SMEM_USER_RAW_MDSS_DBG);
	ccci_write32(baseA,
		PCCIF_CHDATA + PCCIF_SRAM_SIZE - 3 * sizeof(u32),
		0x7274626E);
	ccci_write32(baseA,
		PCCIF_CHDATA + PCCIF_SRAM_SIZE - 2 * sizeof(u32),
		region->base_md_view_phy);
	ccci_write32(baseA,
		PCCIF_CHDATA + PCCIF_SRAM_SIZE - sizeof(u32),
		region->size);
}

int ccci_platform_init(struct ccci_modem *md)
{
	struct device_node *node;
	/* Get infra cfg ao base */
	node = of_find_compatible_node(NULL, NULL, "mediatek,infracfg_ao");
	infra_ao_base = (unsigned long)of_iomap(node, 0);
	if (!infra_ao_base) {
		CCCI_ERROR_LOG(md->index, TAG,
			"%s: infra_ao_base of_iomap failed\n", node->full_name);
		return -1;
	}
	CCCI_INIT_LOG(-1, TAG, "infra_ao_base:0x%p\n", (void *)infra_ao_base);
#ifdef FEATURE_LOW_BATTERY_SUPPORT
	register_low_battery_notify(
		&ccci_md_low_battery_cb, LOW_BATTERY_PRIO_MD);
	register_battery_percent_notify(
		&ccci_md_battery_percent_cb, BATTERY_PERCENT_PRIO_MD);
#endif
	return 0;
}

