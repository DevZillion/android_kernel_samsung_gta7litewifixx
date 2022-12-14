/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef __N3D_UTIL_H__
#define __N3D_UTIL_H__

#define PFX "SeninfN3D"

#define LOG_D(format, args...) pr_debug(PFX "[%s] " format, __func__, ##args)
#define LOG_W(format, args...) pr_warn(PFX "[%s] " format, __func__, ##args)
#define LOG_E(format, args...) pr_err(PFX "[%s] " format, __func__, ##args)

#endif

