/*
 * Copyright (c) 2026 HyperStrike
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * GD32 USBHS (Synopsys DWC2) vendor quirks glue.
 *
 * Brings up the controller-side bus clock, the USB 3.3 V regulator, the
 * embedded USB PHY and the USB PLL before the generic DWC2 driver initialises
 * the core.
 *
 * The bring-up sequence and every register value below are transcribed from
 * the GigaDevice GD32H757 bare-metal USBHS example (experiment 39-2, USB HID
 * keyboard, HS): Middlewares/USB/USB_APP/gd32h7xx_usb_hw.c::usb_rcu_config()
 * and Drivers/.../gd32h7xx_rcu.c. This targets the embedded HS PHY:
 *   PLLUSBHS0 = HXTAL / 5 * 96 / 10  ->  48 MHz USB clock.
 *
 * The GD32 USBHS core does not expose the GHWCFG hardware-config registers
 * (they read back zero), so the generic driver falls back to the GHWCFG values
 * in the devicetree node. This glue powers the PHY and, because VBUS sensing is
 * left off, forces B-peripheral session valid (post_enable) so the core starts
 * a device session — mirroring the bare-metal device-mode init.
 */

#ifndef ZEPHYR_DRIVERS_USB_UDC_DWC2_GD32_USBHS_H
#define ZEPHYR_DRIVERS_USB_UDC_DWC2_GD32_USBHS_H

#include <zephyr/sys/util.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>

/*
 * GD32H7 USBHS / RCU / PMU registers, defined locally so this glue does not
 * pull the full GigaDevice standard-peripheral HAL headers into the DWC2
 * translation unit. Addresses from the GD32H7xx memory map:
 *   USBHS = AHB1_BUS_BASE(0x40020000) + 0x20000 = 0x40040000  (== dts reg)
 *   RCU   = AHB4_BUS_BASE(0x58020000) + 0x4400  = 0x58024400
 *   PMU   = APB4_BUS_BASE(0x58000000) + 0x5800  = 0x58005800
 */
#define GD32_USBHS_BASE			0x40040000UL
/* GUSBCFG (DWC2 +0x0C): GD32 embedded-PHY select. HS = bit5, FS = bit6. */
#define GD32_USBHS_GUSBCFG		(GD32_USBHS_BASE + 0x0CUL)
#define GD32_USBHS_GUSBCFG_EMBED_HS_PHY	BIT(5)
/* GCCFG (DWC2 +0x38): GD32 embedded-PHY power-up. */
#define GD32_USBHS_GCCFG		(GD32_USBHS_BASE + 0x38UL)
#define GD32_USBHS_GCCFG_PHY_PWRON	BIT(16)
/*
 * GOTGCS (DWC2 +0x00): with VBUS sensing off, force B-peripheral session valid
 * so the core enters a device session without a VBUS comparator input.
 */
#define GD32_USBHS_GOTGCS		(GD32_USBHS_BASE + 0x00UL)
#define GD32_USBHS_GOTGCS_BVOE		BIT(6)	/* B-valid override enable */
#define GD32_USBHS_GOTGCS_BVOV		BIT(7)	/* B-valid override value */
/*
 * PCGCCTL (DWC2 +0xE00): power & clock gating control. Cleared to ungate the
 * USB PHY / HCLK clocks. The generic driver only writes PCGCCTL on the
 * hibernation path (disabled here via ghwcfg4), so a warm reset that left the
 * stop-clock bits set from a prior suspend would gate the PHY clock and block
 * enumeration. The bare-metal device init clears it for the same reason.
 */
#define GD32_USBHS_PCGCCTL		(GD32_USBHS_BASE + 0xE00UL)

#define GD32_RCU_BASE			0x58024400UL
#define GD32_RCU_ADDCTL1		(GD32_RCU_BASE + 0xC4UL)
#define GD32_RCU_USBCLKCTL		(GD32_RCU_BASE + 0xD4UL)
#define GD32_RCU_PLLUSBCFG		(GD32_RCU_BASE + 0xD8UL)

#define GD32_RCU_ADDCTL1_PLLUSBHS0EN	BIT(28)
#define GD32_RCU_ADDCTL1_PLLUSBHS0STB	BIT(29)

/* RCU_USBCLKCTL fields. */
#define GD32_RCU_USBCLKCTL_PRESEL	BIT(3)		/* PLLUSBHS0 src: 0 = HXTAL */
#define GD32_RCU_USBCLKCTL_USB48MSEL	GENMASK(6, 5)	/* USBHS0 48M source */
#define GD32_RCU_USB48MSRC_PLLUSBHS	(1U << 5)	/* select CK_PLLUSBHS */

/*
 * RCU_PLLUSBCFG: PREDV(bits 0-3) | USBHS0DV(bits 4-6) | MF(bits 8-14).
 * Example values: HXTAL / 5 * 96 / 10. PREDV=5, MF=96 (96<<8), USBHS0DV=DIV10
 * (encoded 4 -> 4<<4). Mask = 0x7F7F, value = 0x6045.
 */
#define GD32_RCU_PLLUSBCFG_MASK		0x7F7FU
#define GD32_RCU_PLLUSBCFG_VAL		0x6045U	/* predv5 | div10 | mul96 */

#define GD32_PMU_BASE			0x58005800UL
#define GD32_PMU_CTL2			(GD32_PMU_BASE + 0x10UL)
#define GD32_PMU_CTL2_VUSB33DEN		BIT(24)	/* VDD33USB voltage detector */
#define GD32_PMU_CTL2_USBSEN		BIT(25)	/* USB voltage stabiliser */
#define GD32_PMU_CTL2_USB33RF		BIT(26)	/* USB supply ready flag */

/* Bounded spin so a missing/incorrect ready bit never hangs boot forever. */
#define GD32_USBHS_RDY_TIMEOUT		1000000U

struct gd32_usbhs_config {
	uint16_t clkid;
};

static inline int gd32_usbhs_wait(mem_addr_t reg, uint32_t mask)
{
	uint32_t spins;

	for (spins = 0U; spins < GD32_USBHS_RDY_TIMEOUT; spins++) {
		if (sys_read32(reg) & mask) {
			return 0;
		}
	}

	return -ETIMEDOUT;
}

static inline int gd32_usbhs_pre_enable(const struct gd32_usbhs_config *cfg)
{
	uint32_t reg;
	int ret;

	/* 1. USB 3.3 V regulator + voltage detector, then wait supply ready. */
	sys_set_bits(GD32_PMU_CTL2,
		     GD32_PMU_CTL2_USBSEN | GD32_PMU_CTL2_VUSB33DEN);

	ret = gd32_usbhs_wait(GD32_PMU_CTL2, GD32_PMU_CTL2_USB33RF);
	if (ret < 0) {
		return ret;
	}

	/* 2. USBHS0 bus clock (RCU_AHB1EN USBHS0EN) via the GD32 clock driver. */
	if (!device_is_ready(GD32_CLOCK_CONTROLLER)) {
		return -ENODEV;
	}

	ret = clock_control_on(GD32_CLOCK_CONTROLLER,
			       (clock_control_subsys_t)&cfg->clkid);
	if (ret < 0) {
		return ret;
	}

	/*
	 * Ungate the USB PHY / HCLK clocks before anything waits on the PHY
	 * clock. A warm reset may leave PCGCCTL's stop-clock bits set from a
	 * prior suspend; the generic driver only clears them on the (disabled)
	 * hibernation path.
	 */
	sys_write32(0U, GD32_USBHS_PCGCCTL);

	/* 3. Power up the embedded HS PHY (GUSBCFG embedded-PHY + GCCFG PWRON). */
	sys_set_bits(GD32_USBHS_GUSBCFG, GD32_USBHS_GUSBCFG_EMBED_HS_PHY);
	sys_set_bits(GD32_USBHS_GCCFG, GD32_USBHS_GCCFG_PHY_PWRON);

	/* 4. Configure PLLUSBHS0 = HXTAL / 5 * 96 / 10 (-> 48 MHz). */
	sys_clear_bits(GD32_RCU_USBCLKCTL, GD32_RCU_USBCLKCTL_PRESEL);

	reg = sys_read32(GD32_RCU_PLLUSBCFG);
	reg &= ~GD32_RCU_PLLUSBCFG_MASK;
	reg |= GD32_RCU_PLLUSBCFG_VAL;
	sys_write32(reg, GD32_RCU_PLLUSBCFG);

	/* 5. Enable PLLUSBHS0 and wait for it to stabilise. */
	sys_set_bits(GD32_RCU_ADDCTL1, GD32_RCU_ADDCTL1_PLLUSBHS0EN);

	ret = gd32_usbhs_wait(GD32_RCU_ADDCTL1, GD32_RCU_ADDCTL1_PLLUSBHS0STB);
	if (ret < 0) {
		return ret;
	}

	/* 6. Select CK_PLLUSBHS as the USBHS0 48 MHz clock source. */
	reg = sys_read32(GD32_RCU_USBCLKCTL);
	reg &= ~GD32_RCU_USBCLKCTL_USB48MSEL;
	reg |= GD32_RCU_USB48MSRC_PLLUSBHS;
	sys_write32(reg, GD32_RCU_USBCLKCTL);

	return 0;
}

static inline int gd32_usbhs_post_enable(const struct gd32_usbhs_config *cfg)
{
	ARG_UNUSED(cfg);

	/*
	 * The generic DWC2 controller init runs a core soft reset and, with VBUS
	 * sensing off, leaves the device session invalid. Re-assert the PHY
	 * power and force B-peripheral session valid (mirrors the GigaDevice
	 * bare-metal device-mode init) so the core operates once the generic
	 * driver clears soft-disconnect.
	 */
	sys_set_bits(GD32_USBHS_GCCFG, GD32_USBHS_GCCFG_PHY_PWRON);
	sys_set_bits(GD32_USBHS_GOTGCS,
		     GD32_USBHS_GOTGCS_BVOE | GD32_USBHS_GOTGCS_BVOV);

	return 0;
}

static inline int gd32_usbhs_disable(const struct gd32_usbhs_config *cfg)
{
	ARG_UNUSED(cfg);

	sys_clear_bits(GD32_RCU_ADDCTL1, GD32_RCU_ADDCTL1_PLLUSBHS0EN);
	sys_clear_bits(GD32_USBHS_GCCFG, GD32_USBHS_GCCFG_PHY_PWRON);
	sys_clear_bits(GD32_PMU_CTL2,
		       GD32_PMU_CTL2_USBSEN | GD32_PMU_CTL2_VUSB33DEN);

	return 0;
}

/*
 * Advertise High-Speed capability. The generic driver leaves data->caps.hs
 * false unless a vendor caps quirk sets it (it cannot read the GD32's GHWCFG,
 * which reads back zero). The GD32 USBHS embedded PHY is HS-capable, so the
 * device stack may register a High-Speed configuration.
 */
static int gd32_usbhs_caps(const struct device *dev)
{
	struct udc_data *data = dev->data;

	data->caps.hs = true;

	return 0;
}

#define QUIRK_GD32_USBHS_DEFINE(n)						\
	static const struct gd32_usbhs_config gd32_usbhs_cfg_##n = {		\
		.clkid = DT_INST_CLOCKS_CELL(n, id),				\
	};									\
										\
	static int gd32_usbhs_pre_enable_##n(const struct device *dev)		\
	{									\
		ARG_UNUSED(dev);						\
		return gd32_usbhs_pre_enable(&gd32_usbhs_cfg_##n);		\
	}									\
										\
	static int gd32_usbhs_post_enable_##n(const struct device *dev)		\
	{									\
		ARG_UNUSED(dev);						\
		return gd32_usbhs_post_enable(&gd32_usbhs_cfg_##n);		\
	}									\
										\
	static int gd32_usbhs_disable_##n(const struct device *dev)		\
	{									\
		ARG_UNUSED(dev);						\
		return gd32_usbhs_disable(&gd32_usbhs_cfg_##n);			\
	}									\
										\
	const struct dwc2_vendor_quirks dwc2_vendor_quirks_##n = {		\
		.pre_enable = gd32_usbhs_pre_enable_##n,			\
		.post_enable = gd32_usbhs_post_enable_##n,			\
		.disable = gd32_usbhs_disable_##n,				\
		.caps = gd32_usbhs_caps,					\
	};

DT_INST_FOREACH_STATUS_OKAY(QUIRK_GD32_USBHS_DEFINE)

#endif /* ZEPHYR_DRIVERS_USB_UDC_DWC2_GD32_USBHS_H */
