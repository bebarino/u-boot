// SPDX-License-Identifier: GPL-2.0

#include <asm/io.h>
#include <debug_uart.h>
#include <linux/delay.h>

/*
 * Enable the UART8 clks and pinctrl settings for SC7180.
 *
 * The debug UART goes out over CCD. The debug uart clk setting is 36864000 to
 * make the buad rate calculation come out properly for the qcom geni serial
 * driver.
 */
void board_debug_uart_init(void)
{
	u32 val, mask;
	unsigned int timeout = 100;

	/* Enable wrap1_s2 clk */
	val = readl(0x100000 + 0x52008);
	val |= BIT(24);
	writel(val, 0x100000 + 0x52008);
	/* Wait for enable */
	do {
		val = readl(0x100000 + 0x18274);
		mask = GENMASK(30, 28) | BIT(31);
		val &= mask;
		if ((val & BIT(31)) == 0 || ((val >> 28) & 7) == 1)
			break;
		udelay(10);
	} while (timeout--);

	/*
	 * Configure GPIOs
	 * Bits: 0-1 = pull
	 * Bits: 2-4 = func
	 * Bits: 6-8 = drive strength
	 */
	writel(0x3 | (1 << 2), 0x3900000 + (0x1000 * 44)); // Pin 44 - PULL_UP, func1 (qup12), 2ma
	writel(0x3 | (1 << 2), 0x3900000 + (0x1000 * 45)); // Pin 45 - PULL_UP, func1 (qup12), 2ma
}
