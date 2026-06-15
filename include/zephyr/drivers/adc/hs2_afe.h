/*
 * Copyright (c) 2026 HyperStrike
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_ADC_HS2_AFE_H_
#define ZEPHYR_INCLUDE_DRIVERS_ADC_HS2_AFE_H_

#include <stdint.h>
#include <zephyr/device.h>

/* Six axes in driver order: LX, LY, RX, RY, L2, R2. */
#define HS2_AFE_AXES 6U

/*
 * Index of the most-recently-completed frame in a circular ring of `frames`
 * frames of `ranks` items each, given `remaining` = the DMA transfer counter
 * (items still to transfer this full cycle, 0..frames*ranks). The DMA is
 * writing frame `cur`; the freshest finished frame is the one before it.
 */
static inline uint32_t hs2_afe_latest_frame(uint32_t frames, uint32_t ranks,
					    uint32_t remaining)
{
	uint32_t total = frames * ranks;
	uint32_t done = (remaining <= total) ? (total - remaining) : 0U;
	uint32_t cur = (done / ranks) % frames;

	return (cur + frames - 1U) % frames;
}

/*
 * Assemble axis-ordered raw[6] from one frame of each ring.
 * ADC0 frame = [LX, LY, L2]; ADC1 frame = [RX, RY, R2].
 * Output order = [LX, LY, RX, RY, L2, R2] (matches hs2_stick_axis_codes).
 */
static inline void hs2_afe_assemble(const uint16_t *adc0, const uint16_t *adc1,
				    int32_t raw[HS2_AFE_AXES])
{
	raw[0] = adc0[0];	/* LX */
	raw[1] = adc0[1];	/* LY */
	raw[2] = adc1[0];	/* RX */
	raw[3] = adc1[1];	/* RY */
	raw[4] = adc0[2];	/* L2 */
	raw[5] = adc1[2];	/* R2 */
}

/* Fill raw[6] with the latest complete frame. Returns 0, or -ENODEV if the
 * front-end has not produced a full cycle yet / DMA is stopped.
 */
int hs2_afe_read_snapshot(const struct device *afe, int32_t raw[HS2_AFE_AXES]);

/* True once both rings hold at least one complete scan. */
bool hs2_afe_ready(const struct device *afe);

#endif /* ZEPHYR_INCLUDE_DRIVERS_ADC_HS2_AFE_H_ */
