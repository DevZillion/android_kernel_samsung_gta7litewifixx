/*
 * Copyright (C) 2014 Samsung Electronics Co. Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __LINUX_USB_NOTIFIER_H__
#define __LINUX_USB_NOTIFIER_H__

#ifdef CONFIG_USB_DWC3
extern int dwc3_exynos_id_event(struct device *dev, int state);
extern int dwc3_exynos_vbus_event(struct device *dev, int state);
#else
static inline int dwc3_exynos_id_event
		(struct device *dev, int state) {return 0; }
static inline int dwc3_exynos_vbus_event
		(struct device *dev, int state) {return 0; }
#endif
#ifdef CONFIG_USB_S3C_OTGD
extern int exynos_otg_vbus_event(struct platform_device *pdev, int state);
#else
static inline int exynos_otg_vbus_event(
		struct platform_device *pdev, int state) {return 0; }
#endif
#if IS_ENABLED(CONFIG_USB_MTK_HDRC) || IS_ENABLED(CONFIG_USB_MTU3)
void mtk_usb_connect(void);
void mtk_usb_disconnect(void);
void mtk_usb_host_connect(void);
void mtk_usb_host_disconnect(void);
#endif
#endif

