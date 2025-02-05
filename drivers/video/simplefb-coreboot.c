// SPDX-License-Identifier: GPL-2.0+
/*
 * (C) Copyright 2017 Rob Clark
 */

#include <dm.h>
#include <log.h>
#include <video.h>
#include <backlight.h>
#include <asm/io.h>
#include <asm/global_data.h>
#include <coreboot_tables.h>
#include <cb_sysinfo.h>

struct simple_video_coreboot_priv {
	struct udevice *backlight;
};

static int simple_video_coreboot_probe(struct udevice *dev)
{
	struct simple_video_coreboot_priv *priv = dev_get_priv(dev);
	struct video_uc_plat *plat = dev_get_uclass_plat(dev);
	struct video_priv *uc_priv = dev_get_uclass_priv(dev);
	struct cb_framebuffer *fb = lib_sysinfo.framebuffer;

	if (!plat->base)
		return -ENODEV;

	uc_priv->xsize = fb->x_resolution;
	uc_priv->ysize = fb->y_resolution;
	uc_priv->line_length = fb->bytes_per_line;
	switch (fb->bits_per_pixel) {
	case 32:
	case 24:
		uc_priv->bpix = VIDEO_BPP32;
		break;
	case 16:
		uc_priv->bpix = VIDEO_BPP16;
		break;
	default:
		return -EPROTONOSUPPORT;
	}
	uc_priv->format = VIDEO_RGBA8888;

	video_set_flush_dcache(dev, true);

	if (priv->backlight)
		backlight_enable(priv->backlight);

	printf("%s: base=%llx, size=%llu\n",
	      __func__, (unsigned long long)plat->base, (unsigned long long)plat->size);

	writel((15 << 4 | 0x1 << 1 | 0x1 | 0x1 << 8), 0x0AE94004);
	writel(plat->base, 0x0AE05014);
	writel(1, 0x0AE6B800);

	return 0;
}

static int simple_video_coreboot_remove(struct udevice *dev)
{
	/* Disable timing engine */
	writel(0, 0x0AE6B800);

	/* Reset DSI */
	writel(0, 0x0AE94004);

	return 0;
}

static int simple_video_coreboot_bind(struct udevice *dev)
{
	struct video_uc_plat *plat = dev_get_uclass_plat(dev);
	struct cb_framebuffer *fb = lib_sysinfo.framebuffer;

	plat->size = fb->bytes_per_line * fb->y_resolution;
	printf("setting plat size to %d\n", plat->size);

	return 0;
}

static int simple_video_coreboot_to_plat(struct udevice *dev)
{
	struct simple_video_coreboot_priv *priv = dev_get_priv(dev);
	int ret;

	ret = uclass_get_device_by_phandle(UCLASS_PANEL_BACKLIGHT, dev,
						   "backlight", &priv->backlight);
	if (ret) {
		debug("%s: Cannot get backlight: ret=%d\n", __func__, ret);
		if (ret != -ENOENT)
			return log_ret(ret);
	}

	return 0;
}

static const struct udevice_id simple_video_coreboot_ids[] = {
	{ .compatible = "coreboot-simple-fb" },
	{ }
};

U_BOOT_DRIVER(simple_video) = {
	.name	= "simple_video_coreboot",
	.id	= UCLASS_VIDEO,
	.of_match = simple_video_coreboot_ids,
	.of_to_plat	= simple_video_coreboot_to_plat,
	.bind	= simple_video_coreboot_bind,
	.probe	= simple_video_coreboot_probe,
	.remove = simple_video_coreboot_remove,
	.priv_auto	= sizeof(struct simple_video_coreboot_priv),
	.flags = DM_FLAG_PRE_RELOC,
};
