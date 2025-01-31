// SPDX-License-Identifier: GPL-2.0
/*
 * Clock drivers for Qualcomm sc7180
 */

#include <linux/types.h>
#include <clk-uclass.h>
#include <dm.h>
#include <linux/bug.h>
#include <linux/bitops.h>
#include <dt-bindings/clock/qcom,gcc-sc7180.h>

#include "clock-qcom.h"

#define USB30_PRIM_MOCK_UTMI_CLK_CMD_RCGR 0xf034
#define USB30_PRIM_MASTER_CLK_CMD_RCGR 0xf01c

static const struct freq_tbl ftbl_gcc_qupv3_wrap1_s3_clk_src[] = {
	F(7372800, CFG_CLK_SRC_GPLL0_EVEN, 1, 384, 15625),
	F(14745600, CFG_CLK_SRC_GPLL0_EVEN, 1, 768, 15625),
	F(19200000, CFG_CLK_SRC_CXO, 1, 0, 0),
	F(29491200, CFG_CLK_SRC_GPLL0_EVEN, 1, 1536, 15625),
	F(32000000, CFG_CLK_SRC_GPLL0_EVEN, 1, 8, 75),
	F(48000000, CFG_CLK_SRC_GPLL0_EVEN, 1, 4, 25),
	F(64000000, CFG_CLK_SRC_GPLL0_EVEN, 1, 16, 75),
	F(75000000, CFG_CLK_SRC_GPLL0_EVEN, 4, 0, 0),
	F(80000000, CFG_CLK_SRC_GPLL0_EVEN, 1, 4, 15),
	F(96000000, CFG_CLK_SRC_GPLL0_EVEN, 1, 8, 25),
	F(100000000, CFG_CLK_SRC_GPLL0_EVEN, 3, 0, 0),
	{ }
};

static ulong sc7180_set_rate(struct clk *clk, ulong rate)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);
	const struct freq_tbl *freq;

	if (clk->id < priv->data->num_clks)
		debug("%s: %s, requested rate=%ld\n", __func__, priv->data->clks[clk->id].name, rate);

	switch (clk->id) {
	case GCC_QUPV3_WRAP1_S2_CLK: /* UART8 */
		freq = qcom_find_freq(ftbl_gcc_qupv3_wrap1_s3_clk_src, rate);
		clk_rcg_set_rate_mnd(priv->base, 0x18278,
				     freq->pre_div, freq->m, freq->n, freq->src, 16);
		return freq->freq;
	case GCC_USB30_PRIM_MOCK_UTMI_CLK:
		WARN(rate != 19200000, "Unexpected rate for USB30_PRIM_MOCK_UTMI_CLK: %lu\n", rate);
		clk_rcg_set_rate(priv->base, USB30_PRIM_MASTER_CLK_CMD_RCGR, 0, CFG_CLK_SRC_CXO);
		return rate;
	case GCC_USB30_PRIM_MASTER_CLK:
		/* DT has 150000000 but this is actually rounded to 200000000 */
		WARN(rate != 150000000, "Unexpected rate for USB30_PRIM_MASTER_CLK: %lu\n", rate);
		clk_rcg_set_rate_mnd(priv->base, USB30_PRIM_MASTER_CLK_CMD_RCGR,
				     3, 0, 0, CFG_CLK_SRC_GPLL0, 8);
		clk_rcg_set_rate(priv->base, 0xf060, 0, 0);
		return rate;
	case GCC_SDCC1_APPS_CLK:
		/* TODO: Actually set a rate here */
		return rate;
	default:
		return 0;
	}
}

static const struct gate_clk sc7180_clks[] = {
	GATE_CLK(GCC_CFG_NOC_USB3_PRIM_AXI_CLK, 0x502c, 1),
	GATE_CLK(GCC_USB30_PRIM_MASTER_CLK, 0xf010, 1),
	GATE_CLK(GCC_AGGRE_USB3_PRIM_AXI_CLK, 0x8201c, 1),
	GATE_CLK(GCC_USB30_PRIM_SLEEP_CLK, 0xf014, 1),
	GATE_CLK(GCC_USB30_PRIM_MOCK_UTMI_CLK, 0xf018, 1),
	GATE_CLK(GCC_USB3_PRIM_PHY_AUX_CLK, 0xf050, 1),
	GATE_CLK(GCC_USB3_PRIM_PHY_COM_AUX_CLK, 0xf054, 1),
	GATE_CLK(GCC_QUPV3_WRAP1_S2_CLK, 0x18274, 1),
};

static int sc7180_enable(struct clk *clk)
{
	struct msm_clk_priv *priv = dev_get_priv(clk->dev);

	if (priv->data->num_clks < clk->id) {
		debug("%s: unknown clk id %lu\n", __func__, clk->id);
		return 0;
	}

	debug("%s: clk %ld: %s\n", __func__, clk->id, sc7180_clks[clk->id].name);

	switch (clk->id) {
	case GCC_AGGRE_USB3_PRIM_AXI_CLK:
		qcom_gate_clk_en(priv, GCC_USB30_PRIM_MASTER_CLK);
		fallthrough;
	case GCC_USB30_PRIM_MASTER_CLK:
		qcom_gate_clk_en(priv, GCC_USB3_PRIM_PHY_AUX_CLK);
		qcom_gate_clk_en(priv, GCC_USB3_PRIM_PHY_COM_AUX_CLK);
		break;
	}

	qcom_gate_clk_en(priv, clk->id);

	return 0;
}

static const struct qcom_reset_map sc7180_gcc_resets[] = {
	[GCC_QUSB2PHY_PRIM_BCR] = { 0x26000 },
	[GCC_QUSB2PHY_SEC_BCR] = { 0x26004 },
	[GCC_UFS_PHY_BCR] = { 0x77000 },
	[GCC_USB30_PRIM_BCR] = { 0xf000 },
	[GCC_USB3_DP_PHY_PRIM_BCR] = { 0x50008 },
	[GCC_USB3_PHY_PRIM_BCR] = { 0x50000 },
	[GCC_USB3PHY_PHY_PRIM_BCR] = { 0x50004 },
	[GCC_USB_PHY_CFG_AHB2PHY_BCR] = { 0x6a000 },
	[GCC_USB3_PHY_SEC_BCR] = { 0x5000c },
	[GCC_USB3PHY_PHY_SEC_BCR] = { 0x50010 },
	[GCC_USB3_DP_PHY_SEC_BCR] = { 0x50014 },
	[GCC_USB_PHY_CFG_AHB2PHY_BCR] = { 0x6a000 },
};

static const struct qcom_power_map sc7180_gdscs[] = {
	[UFS_PHY_GDSC] = { 0x77004 },
	[USB30_PRIM_GDSC] = { 0xf004 },
};

static struct msm_clk_data sc7180_gcc_data = {
	.resets = sc7180_gcc_resets,
	.num_resets = ARRAY_SIZE(sc7180_gcc_resets),
	.clks = sc7180_clks,
	.num_clks = ARRAY_SIZE(sc7180_clks),

	.power_domains = sc7180_gdscs,
	.num_power_domains = ARRAY_SIZE(sc7180_gdscs),

	.enable = sc7180_enable,
	.set_rate = sc7180_set_rate,
};

static const struct udevice_id gcc_sc7180_of_match[] = {
	{
		.compatible = "qcom,gcc-sc7180",
		.data = (ulong)&sc7180_gcc_data,
	},
	{ }
};

U_BOOT_DRIVER(gcc_sc7180) = {
	.name		= "gcc_sc7180",
	.id		= UCLASS_NOP,
	.of_match	= gcc_sc7180_of_match,
	.bind		= qcom_cc_bind,
	.flags		= DM_FLAG_PRE_RELOC | DM_FLAG_DEFAULT_PD_CTRL_OFF,
};
