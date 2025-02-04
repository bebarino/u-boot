// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2017-2018, The Linux foundation. All rights reserved.

#include <asm/io.h>
#include <clk.h>
#include <dm.h>
#include <errno.h>
#include <linux/iopoll.h>
#include <spi.h>
#include <log.h>
#include <dm/device.h>
#include <dm/device_compat.h>
#include <dm/ofnode.h>
#include <dm/read.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/bitops.h>
#include <time.h>
#include <soc/qcom/geni-se.h>

/* SPI SE specific registers and respective register fields */
#define SE_SPI_CPHA		0x224
#define CPHA			BIT(0)

#define SE_SPI_LOOPBACK		0x22c
#define LOOPBACK_ENABLE		0x1
#define NORMAL_MODE		0x0
#define LOOPBACK_MSK		GENMASK(1, 0)

#define SE_SPI_CPOL		0x230
#define CPOL			BIT(2)

#define SE_SPI_DEMUX_OUTPUT_INV	0x24c

#define SE_SPI_DEMUX_SEL	0x250

#define SE_SPI_TRANS_CFG	0x25c
#define CS_TOGGLE		BIT(1)

#define SE_SPI_WORD_LEN		0x268
#define WORD_LEN_MSK		GENMASK(9, 0)
#define MIN_WORD_LEN		4

#define SE_SPI_TX_TRANS_LEN	0x26c
#define SE_SPI_RX_TRANS_LEN	0x270
#define TRANS_LEN_MSK		GENMASK(23, 0)

#define SE_SPI_PRE_POST_CMD_DLY	0x274

#define SE_SPI_DELAY_COUNTERS	0x278
#define SPI_INTER_WORDS_DELAY_MSK	GENMASK(9, 0)
#define SPI_CS_CLK_DELAY_MSK		GENMASK(19, 10)
#define SPI_CS_CLK_DELAY_SHFT		10

#define SE_GENI_M_IRQ_CLEAR	0x618
#define SE_GENI_M_IRQ_STATUS	0x610

/* M_CMD OP codes for SPI */
#define SPI_TX_ONLY		1
#define SPI_RX_ONLY		2
#define SPI_TX_RX		7
#define SPI_CS_ASSERT		8
#define SPI_CS_DEASSERT		9
#define SPI_SCK_ONLY		10
/* M_CMD params for SPI */
#define SPI_PRE_CMD_DELAY	BIT(0)
#define TIMESTAMP_BEFORE	BIT(1)
#define FRAGMENTATION		BIT(2)
#define TIMESTAMP_AFTER		BIT(3)
#define POST_CMD_DELAY		BIT(4)

#define BYTES_PER_FIFO_WORD	4U

struct geni_spi_priv {
	fdt_addr_t wrapper;
	phys_addr_t base;
	struct clk clk;
	u32 tx_depth;
	bool cs_high;
};

static void geni_se_setup_m_cmd(struct geni_spi_priv *priv, u32 cmd, u32 params)
{
	u32 m_cmd;

	debug("%s: cmd=%#x, parms=%#x\n", __func__, cmd, params);
	m_cmd = (cmd << M_OPCODE_SHFT) | (params & M_PARAMS_MSK);
	writel(m_cmd, priv->base + SE_GENI_M_CMD0);
}

static void handle_se_timeout(struct geni_spi_priv *priv)
{
	int ret;
	u32 m_irq;

	writel(0, priv->base + SE_GENI_TX_WATERMARK_REG);

	writel(M_CMD_CANCEL_EN, priv->base + SE_GENI_M_CMD_CTRL_REG);

	ret = readl_poll_timeout(priv->base + SE_GENI_M_IRQ_STATUS, m_irq,
				 (m_irq & M_CMD_CANCEL_EN) == M_CMD_CANCEL_EN,
				 100);
	writel(M_CMD_CANCEL_EN, priv->base + SE_GENI_M_IRQ_CLEAR);
	if (ret < 0) {
		printf("spi-geni-qcom: Cancel failed. Abort the operation\n");

		writel_relaxed(M_CMD_ABORT_EN, priv->base + SE_GENI_M_CMD_CTRL_REG);
		ret = readl_poll_timeout(priv->base + SE_GENI_M_IRQ_STATUS, m_irq,
					 (m_irq & M_CMD_ABORT_EN) == M_CMD_ABORT_EN,
					 100);
		writel(M_CMD_ABORT_EN, priv->base + SE_GENI_M_IRQ_CLEAR);
		if (ret < 0)
			printf("spi-geni-qcom: Abort failed\n");
	}
}

static int geni_spi_set_speed(struct udevice *dev, uint speed)
{
	/* TODO: Set a clk frequency or change divider here */
	return 0;
}

static int geni_spi_set_mode(struct udevice *bus, uint mode)
{
	struct geni_spi_priv *priv = dev_get_priv(bus);
	u32 loopback_cfg = 0, cpol = 0, cpha = 0;

	if (mode & SPI_LOOP)
		loopback_cfg = LOOPBACK_ENABLE;

	if (mode & SPI_CPOL)
		cpol = CPOL;

	if (mode & SPI_CPHA)
		cpha = CPHA;

	if (mode & SPI_CS_HIGH)
		priv->cs_high = true;

	writel(loopback_cfg, priv->base + SE_SPI_LOOPBACK);
	writel(cpha, priv->base + SE_SPI_CPHA);
	writel(cpol, priv->base + SE_SPI_CPOL);

	return 0;
}

static void geni_spi_reset(struct udevice *dev)
{
	struct udevice *bus = dev_get_parent(dev);
	struct geni_spi_priv *priv = dev_get_priv(bus);

	/* Driver may not be probed yet */
	if (!priv)
		return;
}

#define NUM_PACKING_VECTORS 4
#define PACKING_START_SHIFT 5
#define PACKING_DIR_SHIFT 4
#define PACKING_LEN_SHIFT 1
#define PACKING_STOP_BIT BIT(0)
#define PACKING_VECTOR_SHIFT 10
static void geni_spi_config_packing(struct geni_spi_priv *geni, int bpw,
				    int pack_words, bool msb_to_lsb,
				    bool tx_cfg, bool rx_cfg)
{
	u32 cfg0, cfg1, cfg[NUM_PACKING_VECTORS] = {0};
	int len;
	int temp_bpw = bpw;
	int idx_start = msb_to_lsb ? bpw - 1 : 0;
	int idx = idx_start;
	int idx_delta = msb_to_lsb ? -BITS_PER_BYTE : BITS_PER_BYTE;
	int ceil_bpw = ALIGN(bpw, BITS_PER_BYTE);
	int iter = (ceil_bpw * pack_words) / BITS_PER_BYTE;
	int i;

	if (iter <= 0 || iter > NUM_PACKING_VECTORS)
		return;

	for (i = 0; i < iter; i++) {
		len = min_t(int, temp_bpw, BITS_PER_BYTE) - 1;
		cfg[i] = idx << PACKING_START_SHIFT;
		cfg[i] |= msb_to_lsb << PACKING_DIR_SHIFT;
		cfg[i] |= len << PACKING_LEN_SHIFT;

		if (temp_bpw <= BITS_PER_BYTE) {
			idx = ((i + 1) * BITS_PER_BYTE) + idx_start;
			temp_bpw = bpw;
		} else {
			idx = idx + idx_delta;
			temp_bpw = temp_bpw - BITS_PER_BYTE;
		}
	}
	cfg[iter - 1] |= PACKING_STOP_BIT;
	cfg0 = cfg[0] | (cfg[1] << PACKING_VECTOR_SHIFT);
	cfg1 = cfg[2] | (cfg[3] << PACKING_VECTOR_SHIFT);

	if (tx_cfg) {
		writel(cfg0, geni->base + SE_GENI_TX_PACKING_CFG0);
		writel(cfg1, geni->base + SE_GENI_TX_PACKING_CFG1);
	}
	if (rx_cfg) {
		writel(cfg0, geni->base + SE_GENI_RX_PACKING_CFG0);
		writel(cfg1, geni->base + SE_GENI_RX_PACKING_CFG1);
	}

	/*
	 * Number of protocol words in each FIFO entry
	 * 0 - 4x8, four words in each entry, max word size of 8 bits
	 * 1 - 2x16, two words in each entry, max word size of 16 bits
	 * 2 - 1x32, one word in each entry, max word size of 32 bits
	 * 3 - undefined
	 */
	if (pack_words || bpw == 32)
		writel(bpw / 16, geni->base + SE_GENI_BYTE_GRAN);
}

static u32 geni_spi_get_tx_fifo_depth(struct geni_spi_priv *geni)
{
	u32 val, hw_version, hw_major, hw_minor, tx_fifo_depth_mask;

	hw_version = readl(geni->wrapper + QUP_HW_VER_REG);
	hw_major = GENI_SE_VERSION_MAJOR(hw_version);
	hw_minor = GENI_SE_VERSION_MINOR(hw_version);

	if ((hw_major == 3 && hw_minor >= 10) || hw_major > 3)
		tx_fifo_depth_mask = TX_FIFO_DEPTH_MSK_256_BYTES;
	else
		tx_fifo_depth_mask = TX_FIFO_DEPTH_MSK;

	val = readl(geni->base + SE_HW_PARAM_0);

	return (val & tx_fifo_depth_mask) >> TX_FIFO_DEPTH_SHFT;
}

static void geni_spi_drain_rx(struct geni_spi_priv *geni)
{
	u32 rx_fifo_status;
	unsigned int rx_bytes;
	unsigned int rx_last_byte_valid;
	unsigned int i;

	rx_fifo_status = readl(geni->base + SE_GENI_RX_FIFO_STATUS);
	rx_bytes = (rx_fifo_status & RX_FIFO_WC_MSK) * BYTES_PER_FIFO_WORD;
	if (rx_fifo_status & RX_LAST) {
		rx_last_byte_valid = rx_fifo_status & RX_LAST_BYTE_VALID_MSK;
		rx_last_byte_valid >>= RX_LAST_BYTE_VALID_SHFT;
		if (rx_last_byte_valid && rx_last_byte_valid < 4)
			rx_bytes -= BYTES_PER_FIFO_WORD - rx_last_byte_valid;
	}

	for (i = 0; i < DIV_ROUND_UP(rx_bytes, BYTES_PER_FIFO_WORD); i++)
		readl(geni->base + SE_GENI_RX_FIFOn);
}

static void geni_spi_hw_init(struct udevice *dev)
{
	struct udevice *bus = dev_get_parent(dev);
	struct geni_spi_priv *geni = dev_get_priv(bus);
	u32 demux_output_inv = 0;
	u32 val, demux_sel;

	writel(0, geni->base + SE_GSI_EVENT_EN);
	writel(0xffffffff, geni->base + SE_GENI_M_IRQ_CLEAR);
	writel(0xffffffff, geni->base + SE_GENI_S_IRQ_CLEAR);
	writel(0xffffffff, geni->base + SE_IRQ_EN);

	val = readl(geni->base + GENI_CGC_CTRL);
	val |= DEFAULT_CGC_EN;
	writel(val, geni->base + GENI_CGC_CTRL);

	writel(DEFAULT_IO_OUTPUT_CTRL_MSK, geni->base + GENI_OUTPUT_CTRL);
	writel(FORCE_DEFAULT, geni->base + GENI_FORCE_DEFAULT_REG);

	val = readl(geni->base + SE_IRQ_EN);
	val |= GENI_M_IRQ_EN | GENI_S_IRQ_EN;
	writel(val, geni->base + SE_IRQ_EN);

	val = readl(geni->base + SE_GENI_DMA_MODE_EN);
	val &= ~GENI_DMA_MODE_EN;
	writel(val, geni->base + SE_GENI_DMA_MODE_EN);

	writel(0, geni->base + SE_GSI_EVENT_EN);

	writel(geni->tx_depth - 3, geni->base + SE_GENI_RX_WATERMARK_REG);
	writel(geni->tx_depth - 2, geni->base + SE_GENI_RX_RFR_WATERMARK_REG);

	val = readl(geni->base + SE_GENI_M_IRQ_EN);
	val |= M_COMMON_GENI_M_IRQ_EN;
	val |= M_CMD_DONE_EN | M_TX_FIFO_WATERMARK_EN;
	val |= M_RX_FIFO_WATERMARK_EN | M_RX_FIFO_LAST_EN;
	writel(val, geni->base + SE_GENI_M_IRQ_EN);

	val = readl(geni->base + SE_GENI_S_IRQ_EN);
	val |= S_COMMON_GENI_S_IRQ_EN;
	writel(val, geni->base + SE_GENI_S_IRQ_EN);

	if (geni->cs_high)
		demux_output_inv = BIT(spi_chip_select(dev));
	demux_sel = spi_chip_select(dev);
	writel(demux_sel, geni->base + SE_SPI_DEMUX_SEL);
	writel(demux_output_inv, geni->base + SE_SPI_DEMUX_OUTPUT_INV);

	writel(((8 - MIN_WORD_LEN) & WORD_LEN_MSK), geni->base + SE_SPI_WORD_LEN);
	geni_spi_config_packing(geni, BITS_PER_BYTE, 4, true, true, true);

	geni_spi_drain_rx(geni);
}

static int geni_spi_claim_bus(struct udevice *dev)
{
	geni_spi_hw_init(dev);
	return 0;
}

static int geni_spi_release_bus(struct udevice *dev)
{
	/* Reset the SPI hardware */
	geni_spi_reset(dev);

	return 0;
}

static int geni_spi_set_cs(struct udevice *bus, bool enable)
{
	struct geni_spi_priv *priv = dev_get_priv(bus);
	u32 m_cmd = 0, m_irq;
	int ret;

	debug("%s: enable=%d\n", __func__, enable);

	m_cmd = enable ? SPI_CS_ASSERT : SPI_CS_DEASSERT;
	geni_se_setup_m_cmd(priv, m_cmd, 0);

	ret = readl_poll_timeout(priv->base + SE_GENI_M_IRQ_STATUS, m_irq,
				 (m_irq & M_CMD_DONE_EN) == M_CMD_DONE_EN,
				 100);
	writel(M_CMD_DONE_EN, priv->base + SE_GENI_M_IRQ_CLEAR);
	if (ret) {
		printf("spi-geni-qcom: Timeout setting cs\n");
		handle_se_timeout(priv);
		return ret;
	}

	return 0;
}

static unsigned int
geni_spi_handle_tx(struct geni_spi_priv *geni, const u8 *dout, unsigned int tx_rem_bytes)
{
	unsigned int max_bytes;
	unsigned int i = 0;

	max_bytes = (geni->tx_depth - 1) * BYTES_PER_FIFO_WORD;
	if (tx_rem_bytes < max_bytes)
		max_bytes = tx_rem_bytes;

	while (i < max_bytes) {
		unsigned int j;
		unsigned int bytes_to_write;
		u32 fifo_word = 0;
		u8 *fifo_byte = (u8 *)&fifo_word;

		bytes_to_write = min(BYTES_PER_FIFO_WORD, max_bytes - i);
		for (j = 0; j < bytes_to_write; j++)
			fifo_byte[j] = dout[i++];
		iowrite32_rep((void *)(geni->base + SE_GENI_TX_FIFOn), &fifo_word, 1);
	}
	tx_rem_bytes -= max_bytes;
	if (!tx_rem_bytes)
		writel(0, geni->base + SE_GENI_TX_WATERMARK_REG);

	return max_bytes;
}

static int geni_spi_handle_rx(struct geni_spi_priv *geni, u8 *din, unsigned int rx_rem_bytes)
{
	u32 rx_fifo_status;
	unsigned int rx_bytes;
	unsigned int rx_last_byte_valid;
	unsigned int i = 0;

	rx_fifo_status = readl(geni->base + SE_GENI_RX_FIFO_STATUS);
	rx_bytes = (rx_fifo_status & RX_FIFO_WC_MSK) * BYTES_PER_FIFO_WORD;
	if (rx_fifo_status & RX_LAST) {
		rx_last_byte_valid = rx_fifo_status & RX_LAST_BYTE_VALID_MSK;
		rx_last_byte_valid >>= RX_LAST_BYTE_VALID_SHFT;
		if (rx_last_byte_valid && rx_last_byte_valid < 4)
			rx_bytes -= BYTES_PER_FIFO_WORD - rx_last_byte_valid;
	}

	if (rx_rem_bytes < rx_bytes)
		rx_bytes = rx_rem_bytes;

	while (i < rx_bytes) {
		u32 fifo_word = 0;
		u8 *fifo_byte = (u8 *)&fifo_word;
		unsigned int bytes_to_read;
		unsigned int j;

		bytes_to_read = min(BYTES_PER_FIFO_WORD, rx_bytes - i);
		ioread32_rep((void *)(geni->base + SE_GENI_RX_FIFOn), &fifo_word, 1);
		for (j = 0; j < bytes_to_read; j++)
			din[i++] = fifo_byte[j];
	}

	return rx_bytes;
}

static int geni_spi_xfer(struct udevice *dev, unsigned int bitlen,
			 const void *dout, void *din, unsigned long flags)
{
	struct udevice *bus = dev_get_parent(dev);
	struct geni_spi_priv *priv = dev_get_priv(bus);
	unsigned int len = bitlen >> 3;
	unsigned int rx_rem_bytes = din ? len : 0;
	unsigned int tx_rem_bytes = dout ? len : 0;
	int ret = 0;
	u32 m_cmd = 0, m_irq;
	ulong start;

	if (len & ~TRANS_LEN_MSK) {
		printf("spi-geni-qcom: transfer length too long (%d)\n", len);
		return -EINVAL;
	}

	if (flags & SPI_XFER_BEGIN) {
		geni_spi_hw_init(dev);
		ret = geni_spi_set_cs(bus, true);
		if (ret != 0)
			return ret;
	}

	if (len) {
		if (din) {
			m_cmd |= SPI_RX_ONLY;
			writel(len, priv->base + SE_SPI_RX_TRANS_LEN);
		}
		if (dout) {
			m_cmd |= SPI_TX_ONLY;
			writel(len, priv->base + SE_SPI_TX_TRANS_LEN);
			writel(1, priv->base + SE_GENI_TX_WATERMARK_REG);
		}

		geni_se_setup_m_cmd(priv, m_cmd, FRAGMENTATION);

		start = get_timer(0);
		do {
			ret = readl_poll_timeout(priv->base + SE_GENI_M_IRQ_STATUS, m_irq,
						 m_irq != 0, 1000);
			if ((m_irq & M_RX_FIFO_WATERMARK_EN) || (m_irq & M_RX_FIFO_LAST_EN)) {
				rx_rem_bytes -= geni_spi_handle_rx(priv, din + len - rx_rem_bytes,
								   rx_rem_bytes);
			}
			if (m_irq & M_TX_FIFO_WATERMARK_EN) {
				tx_rem_bytes -= geni_spi_handle_tx(priv, dout + len - tx_rem_bytes,
								   tx_rem_bytes);
			}
			writel(m_irq, priv->base + SE_GENI_M_IRQ_CLEAR);
			if (m_irq & M_CMD_DONE_EN)
				break;
		} while (get_timer(start) < 100000);

		if (!(m_irq & M_CMD_DONE_EN) || tx_rem_bytes || rx_rem_bytes) {
			printf("spi-geni-qcom: Transfer failed\n");
			handle_se_timeout(priv);
			return -ETIMEDOUT;
		}
	}

	if (flags & SPI_XFER_END) {
		ret = geni_spi_set_cs(bus, false);
		if (ret != 0)
			return ret;
	}

	return ret;
}

static const struct dm_spi_ops geni_spi_ops = {
	.claim_bus	= geni_spi_claim_bus,
	.release_bus	= geni_spi_release_bus,
	.xfer		= geni_spi_xfer,
	.set_speed	= geni_spi_set_speed,
	.set_mode	= geni_spi_set_mode,
};

static int geni_spi_probe(struct udevice *dev)
{
	ofnode parent_node = ofnode_get_parent(dev_ofnode(dev));
	struct geni_spi_priv *priv = dev_get_priv(dev);
	int ret;

	priv->base = dev_read_addr(dev);
	if (priv->base == FDT_ADDR_T_NONE)
		return -EINVAL;

	priv->wrapper = ofnode_get_addr(parent_node);
	if (priv->wrapper == FDT_ADDR_T_NONE)
		return -EINVAL;

	ret = clk_get_by_index(dev, 0, &priv->clk);
	if (ret)
		return ret;

	ret = clk_enable(&priv->clk);
	if (ret < 0)
		return ret;

	priv->tx_depth = geni_spi_get_tx_fifo_depth(priv);

	return 0;
}

static const struct udevice_id spi_geni_ids[] = {
	{ .compatible = "qcom,geni-spi" },
	{ /* sentinel */ }
};

U_BOOT_DRIVER(geni_spi) = {
	.name		= "geni_spi",
	.id		= UCLASS_SPI,
	.of_match	= spi_geni_ids,
	.ops		= &geni_spi_ops,
	.priv_auto	= sizeof(struct geni_spi_priv),
	.probe		= geni_spi_probe,
};
