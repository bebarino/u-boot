// SPDX-License-Identifier: GPL-2.0

#include <display.h>
#include <dm.h>
#include <log.h>
#include <regmap.h>
#include <video.h>
#include <asm/arch-rockchip/hardware.h>
#include <asm/global_data.h>
#include "rk_vop.h"

DECLARE_GLOBAL_DATA_PTR;

struct sc7180_mdp_priv {
	void *grf;
	void *regs;
	int win_offset;
	int dsp_offset;
};

static int rk3399_vop_probe(struct udevice *dev)
{
	/* Before relocation we don't need to do anything */
	if (!(gd->flags & GD_FLG_RELOC))
		return 0;

	return rk_vop_probe(dev);
}

static int sc7180_mdp_bind(struct udevice *dev)
{
	struct video_uc_plat *plat = dev_get_uclass_plat(dev);

	plat->size = 4 * (CONFIG_VIDEO_ROCKCHIP_MAX_XRES *
			  CONFIG_VIDEO_ROCKCHIP_MAX_YRES);

	return 0;
}

static const struct udevice_id qcom_sc7180_mdp_ids[] = {
	{ .compatible = "rockchip,rk3399-vop-big" },
	{ }
};

static const struct video_ops qcom_sc7180_mdp_ops = {
};

U_BOOT_DRIVER(sc7180_mdp) = {
	.name	= "sc7180_mdp",
	.id	= UCLASS_VIDEO,
	.of_match = qcom_sc7180_mdp_ids,
	.ops	= &qcom_sc7180_mdp_ops,
	.bind	= sc7180_mdp_bind,
	.probe	= rk3399_vop_probe,
	.priv_auto	= sizeof(struct sc7180_mdp_priv),
};
