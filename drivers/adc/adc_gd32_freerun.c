/*
 * Copyright (c) 2026 HyperStrike
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT hs2_gd32_adc_freerun

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/gd32.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/drivers/adc/hs2_afe.h>
#include <zephyr/linker/devicetree_regions.h>
#include <zephyr/logging/log.h>

#include <gd32_adc.h>

LOG_MODULE_REGISTER(adc_gd32_freerun, CONFIG_ADC_LOG_LEVEL);

#define AFE_NADC  3U
#define AFE_RANKS 2U  /* channels per ADC */
#define AFE_SMP	  240U /* generous: high-impedance pot source */

BUILD_ASSERT(AFE_NADC * AFE_RANKS == HS2_AFE_AXES, "AFE axis count mismatch");

/* DRES code: 14b=0, 12b=1, 10b=2, 8b=3 (matches adc_gd32.c). */
static inline uint32_t afe_dres(uint8_t resolution)
{
	switch (resolution) {
	case 14U:
		return 0U;
	case 12U:
		return 1U;
	case 10U:
		return 2U;
	default:
		return 3U;
	}
}

struct afe_adc {
	uint32_t reg;
	uint16_t clkid;
	struct reset_dt_spec reset;
	const struct device *dma_dev;
	uint32_t dma_ch;
	uint32_t dma_slot;
	uint8_t ch[AFE_RANKS];
	uint8_t resolution;
	bool clk_master; /* configures the SYNCCTL conversion clock */
	uint16_t *ring;
};

struct afe_config {
	struct afe_adc adc[AFE_NADC];
	const struct pinctrl_dev_config *pcfg;
	uint32_t frames;
};

struct afe_data {
	bool ready;
};

/* Mirror adc_gd32.c's bounded calibration (proven to boot on this H7 tree). */
static void afe_calibrate(uint32_t reg)
{
	uint32_t cnt;

	ADC_CTL1(reg) |= ADC_CTL1_RSTCLB;
	for (cnt = 0U; (ADC_CTL1(reg) & ADC_CTL1_RSTCLB) && cnt < 1000000U; cnt++) {
	}
	ADC_CTL1(reg) |= ADC_CTL1_CLB;
	for (cnt = 0U; (ADC_CTL1(reg) & ADC_CTL1_CLB) && cnt < 1000000U; cnt++) {
	}
}

static void afe_adc_freerun_config(const struct afe_adc *a)
{
	uint32_t reg = a->reg;

	/* resolution + scan + continuous */
	ADC_CTL0(reg) &= ~ADC_CTL0_DRES;
	ADC_CTL0(reg) |= CTL0_DRES(afe_dres(a->resolution));
	ADC_CTL0(reg) |= ADC_CTL0_SM;
	ADC_CTL1(reg) |= ADC_CTL1_CTN;

	/* sequence length 2 (RL = len - 1) */
	ADC_RSQ0(reg) &= ~ADC_RSQ0_RL;
	ADC_RSQ0(reg) |= RSQ0_RL(AFE_RANKS - 1U);

	/* rank0 -> RSQ8 field0; rank1 -> RSQ7 field0 */
	ADC_RSQ8(reg) = SQX_SMP(AFE_SMP) | a->ch[0];
	ADC_RSQ7(reg) = SQX_SMP(AFE_SMP) | a->ch[1];

	/* DMA + request-after-last so circular DMA keeps getting requests */
	ADC_CTL1(reg) |= ADC_CTL1_DMA | ADC_CTL1_DDM;
}

static void afe_adc_init(const struct afe_adc *a)
{
	uint32_t reg = a->reg;

	(void)clock_control_on(GD32_CLOCK_CONTROLLER,
			       (clock_control_subsys_t)&a->clkid);
	(void)reset_line_toggle_dt(&a->reset);

	/*
	 * ADC0/ADC1 share the SYNCCTL conversion clock (master = ADC0); ADC2 is
	 * independent (its own master). The slave (ADC1) must not reprogram it.
	 */
	if (a->clk_master) {
		ADC_SYNCCTL(reg) &= ~(ADC_SYNCCTL_ADCCK | ADC_SYNCCTL_ADCSCK);
		ADC_SYNCCTL(reg) |= ADC_CLK_SYNC_HCLK_DIV16;
	}

	afe_adc_freerun_config(a);

	ADC_CTL1(reg) |= ADC_CTL1_ADCON;
	k_busy_wait(1000); /* stabilize before calibration */
	afe_calibrate(reg);
}

static int afe_dma_start(const struct afe_adc *a, uint32_t items)
{
	struct dma_block_config blk = {
		.source_address = (uint32_t)&ADC_RDATA(a->reg),
		.dest_address = (uint32_t)a->ring,
		.source_addr_adj = DMA_ADDR_ADJ_NO_CHANGE,
		.dest_addr_adj = DMA_ADDR_ADJ_INCREMENT,
		.block_size = items, /* CHxCNT = item count (gd32 convention) */
	};
	struct dma_config cfg = {
		.channel_direction = PERIPHERAL_TO_MEMORY,
		.source_data_size = 2,
		.dest_data_size = 2,
		.block_count = 1,
		.head_block = &blk,
		.cyclic = 1,
		.dma_slot = a->dma_slot,
		.channel_priority = 2,
	};
	int ret;

	if (!device_is_ready(a->dma_dev)) {
		return -ENODEV;
	}
	ret = dma_config(a->dma_dev, a->dma_ch, &cfg);
	if (ret < 0) {
		return ret;
	}
	return dma_start(a->dma_dev, a->dma_ch);
}

/* Fire the regular-group software trigger once; free-run does the rest. */
static inline void afe_software_trigger(uint32_t reg)
{
	ADC_CTL1(reg) |= ADC_CTL1_SWRCST;
}

static int afe_init(const struct device *dev)
{
	const struct afe_config *cfg = dev->config;
	struct afe_data *data = dev->data;
	uint32_t items = cfg->frames * AFE_RANKS;
	int ret;

	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	for (uint32_t i = 0; i < AFE_NADC; i++) {
		afe_adc_init(&cfg->adc[i]);
	}
	for (uint32_t i = 0; i < AFE_NADC; i++) {
		ret = afe_dma_start(&cfg->adc[i], items);
		if (ret < 0) {
			LOG_ERR("AFE DMA %u start failed (%d)", i, ret);
			return ret;
		}
	}
	for (uint32_t i = 0; i < AFE_NADC; i++) {
		afe_software_trigger(cfg->adc[i].reg);
	}

	/* Wait until every ring has been written at least once (bounded). */
	for (int t = 0; t < 1000; t++) {
		bool all_ready = true;

		for (uint32_t i = 0; i < AFE_NADC; i++) {
			struct dma_status s;

			if (dma_get_status(cfg->adc[i].dma_dev, cfg->adc[i].dma_ch,
					   &s) != 0 ||
			    s.pending_length >= items) {
				all_ready = false;
				break;
			}
		}
		if (all_ready) {
			data->ready = true;
			break;
		}
		k_busy_wait(10);
	}

	LOG_INF("AFE 3-ADC free-running (ready=%d)", data->ready);
	return 0;
}

bool hs2_afe_ready(const struct device *afe)
{
	const struct afe_data *data = afe->data;

	return data->ready;
}

int hs2_afe_read_snapshot(const struct device *afe, int32_t raw[HS2_AFE_AXES])
{
	const struct afe_config *cfg = afe->config;
	struct afe_data *data = afe->data;
	uint32_t items = cfg->frames * AFE_RANKS;
	const uint16_t *frame[AFE_NADC];

	if (!data->ready) {
		return -ENODEV;
	}

	for (uint32_t i = 0; i < AFE_NADC; i++) {
		struct dma_status s;
		uint32_t f;

		if (dma_get_status(cfg->adc[i].dma_dev, cfg->adc[i].dma_ch, &s) < 0) {
			return -EIO;
		}
		f = hs2_afe_latest_frame(cfg->frames, AFE_RANKS, s.pending_length);
		frame[i] = &cfg->adc[i].ring[f * AFE_RANKS];
	}

	hs2_afe_assemble(frame[0], frame[1], frame[2], raw);
	return 0;
}

#define AFE_RING(inst, n)							\
	static uint16_t afe_ring##n##_##inst[DT_INST_PROP(inst, frames) * AFE_RANKS] \
		__aligned(32) __attribute__((__section__(			\
			LINKER_DT_NODE_REGION_NAME(DT_NODELABEL(sram1)))))

#define AFE_ADC_ENTRY(inst, n, prop, dname, master)				\
	{									\
		.reg = DT_REG_ADDR(DT_INST_PHANDLE_BY_IDX(inst, adcs, n)),	\
		.clkid = DT_CLOCKS_CELL(DT_INST_PHANDLE_BY_IDX(inst, adcs, n), id), \
		.reset = RESET_DT_SPEC_GET(DT_INST_PHANDLE_BY_IDX(inst, adcs, n)), \
		.dma_dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(inst, dname)), \
		.dma_ch = DT_INST_DMAS_CELL_BY_NAME(inst, dname, channel),	\
		.dma_slot = DT_INST_DMAS_CELL_BY_NAME(inst, dname, slot),	\
		.ch = DT_INST_PROP(inst, prop),					\
		.resolution = DT_INST_PROP_BY_IDX(inst, resolutions, n),	\
		.clk_master = (master),						\
		.ring = afe_ring##n##_##inst,					\
	}

#define AFE_INIT(inst)								\
	PINCTRL_DT_INST_DEFINE(inst);						\
	AFE_RING(inst, 0);							\
	AFE_RING(inst, 1);							\
	AFE_RING(inst, 2);							\
	static struct afe_data afe_data_##inst;					\
	static const struct afe_config afe_cfg_##inst = {			\
		.adc = {							\
			AFE_ADC_ENTRY(inst, 0, adc0_channels, adc0, true),	\
			AFE_ADC_ENTRY(inst, 1, adc1_channels, adc1, false),	\
			AFE_ADC_ENTRY(inst, 2, adc2_channels, adc2, true),	\
		},								\
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),			\
		.frames = DT_INST_PROP(inst, frames),				\
	};									\
	DEVICE_DT_INST_DEFINE(inst, afe_init, NULL, &afe_data_##inst,		\
			      &afe_cfg_##inst, POST_KERNEL,			\
			      CONFIG_ADC_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(AFE_INIT)
