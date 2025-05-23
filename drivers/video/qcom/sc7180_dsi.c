// SPDX-License-Identifier: GPL-2.0
/*
 */

#include <asm/io.h>
#include <display.h>
#include <dm.h>
#include <log.h>
#include <dm/uclass-internal.h>
#include <linux/err.h>
#include <linux/kernel.h>

struct qcom_mipi_priv {
	uintptr_t regs;
};

static int qcom_mipi_display_enable(struct udevice *dev, int panel_bpp,
			  const struct display_timing *timing)
{
	struct qcom_mipi_priv *priv = dev_get_priv(dev);

	writel((15 << 4 | 0x1 << 1 | 0x1 | 0x1 << 8), priv->regs + 0x4)

	return 0;
}

static int qcom_mipi_of_to_plat(struct udevice *dev)
{
	struct qcom_mipi_priv *priv = dev_get_priv(dev);

	priv->regs = dev_read_addr(dev);
	if (priv->regs == FDT_ADDR_T_NONE) {
		debug("%s: Get MIPI dsi address failed\n", __func__);
		return  -ENXIO;
	}

	return 0;
}

static int qcom_mipi_probe(struct udevice *dev)
{
	return 0;
}

static const struct dm_display_ops qcom_mipi_dsi_ops = {
	.enable = qcom_mipi_display_enable,
};

static const struct udevice_id qcom_mipi_dsi_ids[] = {
	{ .compatible = "qcom,mdss-dsi-ctrl", }
	{ }
};

U_BOOT_DRIVER(rk_mipi_dsi) = {
	.name	= "qcom_mipi_dsi",
	.id	= UCLASS_DISPLAY,
	.of_match = qcom_mipi_dsi_ids,
	.of_to_plat = qcom_mipi_of_to_plat,
	.probe	= qcom_mipi_probe,
	.ops	= &qcom_mipi_dsi_ops,
	.priv_auto	  = sizeof(struct qcom_mipi_priv),
};
