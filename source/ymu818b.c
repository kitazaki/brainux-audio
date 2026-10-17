// SPDX-License-Identifier: GPL-2.0-only
/*
 * Yamaha YMU818B codec driver.
 *
 * The PW-SH1/PW-SH2 Windows CE wavedev driver uses an I2C protocol where register
 * addresses below 0x80 are sent as one address byte and higher addresses as
 * two address bytes.  The register map is not public, so board-specific
 * initialization can be supplied from device tree.
 */

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

struct ymu818b {
	struct i2c_client *client;
	struct gpio_desc *reset_gpio;
	struct mutex lock;
	unsigned int transfer_delay_us;
	unsigned int sysclk;
	bool sh2;
	bool configured;
	bool playback_started;
};

struct ymu818b_init_block {
	unsigned int reg;
	const u8 *vals;
	unsigned int count;
	unsigned int delay_us_after;
};

#define YMU818B_I2C_RETRIES	5
#define YMU818B_I2C_RETRY_US	10000

#define MC1N2_MCI_BASE_ADR	0x01
#define MC1N2_MCI_BASE_WINDOW	0x02
#define MC1N2_MCI_ANA_ADR	0x0c
#define MC1N2_MCI_ANA_WINDOW	0x0d
#define MC1N2_MCI_MIX_ADR	0x10
#define MC1N2_MCI_MIX_WINDOW	0x11
#define MC1N2_MCI_CODEC_ADR	0x04
#define MC1N2_MCI_CODEC_WINDOW	0x05

#define MC1N2_MCI_SD_MSK_1	0x0a
#define MC1N2_MCI_BCLK_MSK_1	0x0c
#define MC1N2_MCI_PA_MSK	0x1a
#define MC1N2_MCI_SOURCE	0x6e
#define MC1N2_MCI_DIX0_START	0x89
#define MC1N2_MCI_DIMODE0	0x82
#define MC1N2_MCI_DIRSRC_RATE0_MSB	0x83
#define MC1N2_MCI_DIRSRC_RATE0_LSB	0x84
#define MC1N2_MCI_DI_FS0	0x87
#define MC1N2_MCI_DI0_SRC	0x88
#define MC1N2_MCI_DIX0_FMT	0x8e
#define MC1N2_MCI_DIR0_CH	0x8f
#define MC1N2_MCI_DIR0_VOLL	0x1c
#define MC1N2_MCI_DIR0_VOLR	0x1d
#define MC1N2_MCI_MASTER_OUTL	0x4f
#define MC1N2_MCI_MASTER_OUTR	0x50
#define MC1N2_MCI_DAC_ATTL	0x59
#define MC1N2_MCI_DAC_ATTR	0x5a
#define MC1N2_MCI_SPL_MIX	0x40
#define MC1N2_MCI_SPL_MONO	0x41
#define MC1N2_MCI_SPR_MIX	0x42
#define MC1N2_MCI_SPR_MONO	0x43
#define MC1N2_MCI_PWM_ANALOG_0	0x02
#define MC1N2_MCI_SP_MODE	0x2c
#define MC1N2_MCI_PWM_ANALOG_1	0x03
#define MC1N2_MCI_PWM_ANALOG_3	0x05
#define MC1N2_MCI_APMOFF	0x10
#define MC1N2_MCI_SPVOL_L	0x25
#define MC1N2_MCI_SPVOL_R	0x26
#define MC1N2_MCI_DAC_CONFIG	0x4e

#define MC1N2_DAC_SOURCE_MIX	0x70
#define MC1N2_DAC_SOURCE_DIR0	0x30
#define MC1N2_DIR0_START	0x0c
#define MC1N2_DIO_DA		0x00
#define MC1N2_DIRSRC_RATE_44100_MSB	0x75
#define MC1N2_DIRSRC_RATE_44100_LSB	0x9a
#define MC1N2_DI_FS_44100_BCK32	0x21
#define MC1N2_DI_SRC_RATE_SET	0x01
#define MC1N2_DIX0_FMT_I2S_16	0x04
#define MC1N2_DIR0_CH_STEREO	0x10
#define MC1N2_DIG_VOL_0DB_LATCH	0xe0
#define MC1N2_DIG_VOL_0DB	0x60
#define MC1N2_DAC_MIX		0x40
#define MC1N2_DAC_MONO		0x40
#define MC1N2_SDIN0_UNMASK	0x00
#define MC1N2_BCLK0_UNMASK	0x00
#define MC1N2_PA_MASK_DEFAULT	0x88
#define MC1N2_DAC_MIX_R		0x40
#define MC1N2_DAC_MONO_R	0x40
#define MC1N2_SP_MODE_NORMAL	0x00
#define MC1N2_PWM_ANALOG0_ON	0x00
#define MC1N2_PWM_SP_STAGE1	0xfa
#define MC1N2_PWM_SP_ON		0xf0
#define MC1N2_PWM_DAC_ON	0x07
#define MC1N2_APMOFF_MANUAL	0x07
#define MC1N2_SPVOL_L_MAX	0x7f
#define MC1N2_SPVOL_R_MAX	0x1f
#define MC1N2_DAC_CONFIG_NSMUTE	0x02
#define MC1N2_DAC_CONFIG_ON_NSMUTE	0x03
#define MC1N2_DAC_CONFIG_ON	0x01

#define YMU818B_PLAYBACK_NOTIFY_INDEX	(ARRAY_SIZE(ymu818b_playback_start) - 1)

static const u8 ymu818b_init_17[] = { 0x47, 0x01 };
static const u8 ymu818b_init_06[] = { 0x1e, 0x62 };
static const u8 ymu818b_init_00_power[] = { 0x1c, 0x18, 0x00, 0x80, 0x00 };

static const u8 ymu818b_init_a2[] = {
	0x00, 0x40, 0x00, 0x7f, 0xff, 0xc0, 0x00, 0x80, 0x00, 0x10, 0x00, 0x10,
	0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x3f, 0x5c, 0xe0, 0xa2, 0x00,
	0x08, 0x00, 0x10, 0x00, 0x08, 0x3e, 0x82, 0xe1, 0x76, 0x1f, 0xae, 0xc0,
	0xa4, 0x1f, 0xae, 0x3f, 0x5c, 0xe0, 0xa2, 0x04, 0xf0, 0x09, 0xe0, 0x04,
	0xf0, 0x13, 0xc6, 0xf8, 0x75, 0x17, 0xb9, 0xd0, 0x8e, 0x17, 0xb9, 0x2d,
	0x4e, 0xee, 0x68, 0x08, 0x13, 0x04, 0x13, 0x07, 0x3f, 0x04, 0x00, 0x11,
	0x47, 0x04, 0x00, 0x11, 0x47, 0x0a, 0x18, 0x0a, 0x18, 0x00, 0x00, 0x40,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x10, 0x00, 0xd8, 0xa0, 0xd2,
	0x94, 0xcb, 0x9a, 0xc3, 0x8d, 0xba, 0x45, 0xaf, 0x90, 0xa3, 0x37, 0x94,
	0xf9, 0x00, 0x80, 0x00, 0x00, 0xff, 0x95, 0x00, 0x12, 0x00, 0x0f, 0x00,
	0xd0, 0x00, 0xa0, 0x00, 0xa0, 0x00, 0x80, 0x08, 0x00, 0x3f, 0xff, 0x00,
	0x00, 0x3f, 0xff, 0x51, 0x40, 0x2e, 0xbf, 0x51, 0x40, 0x00, 0x01, 0x01,
	0x51, 0x01, 0x51, 0x7d, 0x5d, 0x08, 0x15, 0x04, 0x0c, 0x02, 0x07, 0x00,
	0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0x7f, 0xff, 0x7f,
	0xff, 0x16, 0x96, 0x01, 0x51, 0x01, 0x51, 0x7d, 0x5d, 0x00, 0x00, 0x10,
	0x00, 0x30, 0x00, 0x06, 0x8c, 0x03, 0x46, 0x06, 0x8c, 0x5a, 0xc9, 0xd7,
	0xe3, 0x17, 0x73, 0xe3, 0x34, 0x10, 0x00,
};

static const u8 ymu818b_init_e1[] = {
	0xe1, 0x46, 0x19, 0x0e, 0xbe, 0x1f, 0x1e, 0xb9, 0xe8, 0xf1, 0x41, 0xe2,
	0x10, 0x4c, 0xe2,
};
static const u8 ymu818b_init_f3[] = { 0xe2, 0xfc, 0x12 };
static const u8 ymu818b_init_f9[] = { 0x0d, 0x61, 0x40 };
static const u8 ymu818b_init_ff[] = { 0x1d, 0x03, 0xef };
static const u8 ymu818b_init_105[] = { 0xf2, 0x51, 0xdf };
static const u8 ymu818b_init_111[] = {
	0xee, 0xf2, 0x2a, 0x84, 0xb5, 0xfa, 0x03, 0xb1, 0x3b, 0x13, 0xb1, 0x3c,
	0x11, 0x0d, 0xd5, 0x7b, 0x4a, 0x08, 0xfc, 0x4e, 0xc4, 0xec, 0x4e, 0xc6,
};
static const u8 ymu818b_init_00_post[] = { 0x18, 0x1c };
static const u8 ymu818b_init_00_ac_pre[] = { 0x18 };
static const u8 ymu818b_init_00_1c[] = { 0x1c };
static const u8 ymu818b_init_00_1e[] = { 0x1e };
static const u8 ymu818b_init_00_3e[] = { 0x3e };
static const u8 ymu818b_init_00_1c18[] = { 0x1c, 0x18 };
static const u8 ymu818b_init_ac[] = { 0x02 };
static const u8 ymu818b_init_ad[] = { 0x22 };
static const u8 ymu818b_init_00_dsp_pre[] = { 0x10 };
static const u8 ymu818b_init_00_final[] = { 0x3f };

static const u8 ymu818b_playback_coef_a2[] = {
	0x04, 0x10, 0x00, 0x10, 0x00, 0x00, 0x08, 0x00, 0x10, 0x00, 0x08, 0x3e,
	0x82, 0xe1, 0x76, 0x00, 0x50, 0x00, 0xa0, 0x00, 0x50, 0x3b, 0x3d, 0xe4,
	0x70, 0x1f, 0x43, 0xc1, 0x7a, 0x1f, 0x43, 0x3e, 0x82, 0xe1, 0x76, 0x07,
	0x02, 0x0e, 0x04, 0x07, 0x02, 0x09, 0xce, 0xfa, 0x21, 0x19, 0x56, 0xcd,
	0x54, 0x19, 0x56, 0x31, 0x4b, 0xeb, 0xf2, 0x06, 0xdd, 0x0a, 0x6b, 0x18,
	0xe9, 0x02, 0xd7, 0x23, 0xd7, 0x02, 0xd7, 0x23, 0xd7, 0x0a, 0x18, 0x0a,
	0x18, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x10, 0x00, 0x10, 0x00, 0xf2, 0x3f, 0xed, 0xe9, 0xe8, 0x37, 0xe0,
	0xb7, 0xd6, 0xd7, 0xc9, 0xde, 0xb8, 0xd7, 0xa2, 0x5f, 0x00, 0x80, 0x00,
	0x00, 0xfd, 0x82, 0x01, 0x01, 0x00, 0x9f, 0x05, 0xa0, 0x03, 0x60, 0x02,
	0x00, 0x01, 0x40, 0x08, 0x00, 0x3f, 0xff, 0x00, 0x00, 0x3f, 0xb7, 0x51,
	0x40, 0x2e, 0xbf, 0x0f, 0x02, 0x00, 0x01, 0x01, 0x51, 0x01, 0x51, 0x7d,
	0x5d, 0x08, 0x15, 0x04, 0x0c, 0x02, 0x07, 0x00, 0x80, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x10, 0x00, 0x10, 0x00, 0x7f, 0xff, 0x20, 0xe1, 0x01,
	0x51, 0x01, 0x51, 0x7d, 0x5d, 0x00, 0x00, 0x10, 0x00, 0x40, 0x00, 0x00,
	0x21, 0x00, 0x10, 0x00, 0x21, 0x7b, 0xb1, 0xc3, 0xca, 0x00, 0x00, 0x00,
	0x00, 0x40, 0x00,
};

static const u8 ymu818b_playback_coef_c0[] = {
	0x0d, 0x68, 0x66, 0xf8, 0x8b, 0x84, 0x0b, 0xab, 0x31, 0x07, 0x74, 0x7d,
	0xf6, 0xec, 0x6a, 0x0e, 0xd8, 0x02, 0xe5, 0xdf, 0xad, 0x0e, 0x11, 0x6d,
	0x1a, 0x20, 0x54, 0xf3, 0x16, 0x92, 0x0f, 0xc1, 0xc8, 0xe0, 0xf5, 0x34,
	0x0f, 0x6f, 0x79, 0x1f, 0x0a, 0xcd, 0xf0, 0xce, 0xc0, 0x10, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x3d, 0xf2, 0x78, 0x79, 0x3f, 0xf8, 0x84, 0x1b, 0x0f,
	0x0d, 0x80, 0x11, 0x3d, 0xf2, 0x78, 0x79, 0x3f, 0xf8, 0x1f, 0x19, 0x78,
	0xf4, 0x49, 0xd0, 0xf0, 0xdb, 0x9a, 0x04, 0x43, 0x98,
};

/* PW-SH2 CE uses a different initial DSP coefficient and speaker EQ tail.
 * Reconstructed from the CE packet builder, not a raw register dump.
 */
static const u8 ymu818b_sh2_init_a2[] = {
	0x00, 0x40, 0x00, 0x7f, 0xff, 0xc0, 0x00, 0x80, 0x00, 0x10, 0x00, 0x10,
	0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x3f, 0x5c, 0xe0, 0xa2, 0x00,
	0x08, 0x00, 0x10, 0x00, 0x08, 0x3e, 0x82, 0xe1, 0x76, 0x1f, 0xae, 0xc0,
	0xa4, 0x1f, 0xae, 0x3f, 0x5c, 0xe0, 0xa2, 0x04, 0xf0, 0x09, 0xe0, 0x04,
	0xf0, 0x13, 0xc6, 0xf8, 0x75, 0x17, 0xb9, 0xd0, 0x8e, 0x17, 0xb9, 0x2d,
	0x4e, 0xee, 0x68, 0x08, 0x13, 0x04, 0x13, 0x07, 0x3f, 0x04, 0x00, 0x11,
	0x47, 0x04, 0x00, 0x11, 0x47, 0x0a, 0x18, 0x0a, 0x18, 0x00, 0x00, 0x40,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x10, 0x00, 0xd8, 0xa0, 0xd2,
	0x94, 0xcb, 0x9a, 0xc3, 0x8d, 0xba, 0x45, 0xaf, 0x90, 0xa3, 0x37, 0x94,
	0xf9, 0x00, 0x80, 0x00, 0x00, 0xff, 0x95, 0x00, 0x12, 0x00, 0x0f, 0x00,
	0xd0, 0x00, 0xa0, 0x00, 0xa0, 0x00, 0x80, 0x08, 0x00, 0x3f, 0xff, 0x00,
	0x00, 0x3f, 0xff, 0x51, 0x40, 0x2e, 0xbf, 0x51, 0x40, 0x00, 0x01, 0x01,
	0x51, 0x01, 0x51, 0x7d, 0x5d, 0x08, 0x15, 0x04, 0x0c, 0x02, 0x07, 0x00,
	0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0xff, 0x7f, 0xff, 0x7f,
	0xff, 0x16, 0x96, 0x01, 0x51, 0x01, 0x51, 0x7d, 0x5d, 0x00, 0x00, 0x10,
	0x00, 0x30, 0x00, 0x06, 0x8c, 0x03, 0x46, 0x06, 0x8c, 0x5a, 0xc9, 0xd7,
	0xd3, 0x17, 0x73, 0xe3, 0x34, 0x10, 0x00,
};

static const u8 ymu818b_sh2_playback_coef_c0[] = {
	0x0d, 0x68, 0x66, 0xf8, 0x8b, 0x84, 0x0b, 0xab, 0x31, 0x07, 0x74, 0x7d,
	0xf6, 0xec, 0x6a, 0x0e, 0xd8, 0x02, 0xe5, 0xdf, 0xad, 0x0e, 0x11, 0x6d,
	0x1a, 0x20, 0x54, 0xf3, 0x16, 0x92, 0x0f, 0xc1, 0xc8, 0xe0, 0xf5, 0x34,
	0x0f, 0x6f, 0x79, 0x1f, 0x0a, 0xcd, 0xf0, 0xce, 0xc0, 0x10, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x3d, 0x13, 0xac, 0x26, 0xb3, 0x97, 0x85, 0xd8, 0xa7,
	0xb2, 0x98, 0xd2, 0x3d, 0x13, 0xac, 0x26, 0xb3, 0x97, 0x1e, 0xa2, 0xe1,
	0x7e, 0x1d, 0xe8, 0xf1, 0x44, 0xde, 0x0f, 0x93, 0x2f,
};

static const u8 ymu818b_play_01_fe[] = { 0xfe };
static const u8 ymu818b_play_04_0b[] = { 0x0b };
static const u8 ymu818b_play_bd_03[] = { 0x03 };
static const u8 ymu818b_play_bd_01[] = { 0x01 };
static const u8 ymu818b_play_02_7f[] = { 0x7f };
static const u8 ymu818b_play_08[] = { 0x0a, 0x00 };
static const u8 ymu818b_play_8b[] = { 0x1f };
static const u8 ymu818b_play_90[] = { 0x1f };
static const u8 ymu818b_play_95[] = { 0x1f, 0x00, 0x04 };
/* CE input configuration for 44.1 kHz, S16_LE, two I2S slots.
 * The later 0x05e write starts the input after this configuration.
 */
static const u8 ymu818b_play_5c[] = {
	0x01, 0x00, 0x00, 0x19, 0x75, 0x8d,
};
static const u8 ymu818b_play_4a[] = { 0x40 };
static const u8 ymu818b_play_80[] = {
	0xcf, 0x4f, 0xfc, 0x7c, 0x00, 0x4f, 0x0f,
};
static const u8 ymu818b_play_5e[] = { 0x01 };
static const u8 ymu818b_play_a0_01[] = { 0x01 };
static const u8 ymu818b_play_129_01[] = { 0x01 };
static const u8 ymu818b_play_97_00[] = { 0x00 };
static const u8 ymu818b_play_a0_00_02[] = { 0x00, 0x02 };
static const u8 ymu818b_play_a1_00[] = { 0x00 };
static const u8 ymu818b_play_129_00[] = { 0x00 };
static const u8 ymu818b_play_97_04[] = { 0x04 };
static const u8 ymu818b_play_01_fc[] = { 0xfc };
static const u8 ymu818b_play_01_f4[] = { 0xf4 };
static const u8 ymu818b_play_49[] = { 0x40 };
static const u8 ymu818b_play_25_on[] = { 0x33 };
static const u8 ymu818b_play_25_off[] = { 0x20 };

static const struct ymu818b_init_block ymu818b_playback_start[] = {
	{ 0x000, ymu818b_init_00_3e, ARRAY_SIZE(ymu818b_init_00_3e), 0 },
	{ 0x000, ymu818b_init_00_1e, ARRAY_SIZE(ymu818b_init_00_1e), 2000 },
	{ 0x000, ymu818b_init_00_1c18, ARRAY_SIZE(ymu818b_init_00_1c18), 0 },
	{ 0x000, ymu818b_init_00_3e, ARRAY_SIZE(ymu818b_init_00_3e), 3000 },
	{ 0x000, ymu818b_init_00_ac_pre, ARRAY_SIZE(ymu818b_init_00_ac_pre), 0 },
	{ 0x000, ymu818b_init_00_1e, ARRAY_SIZE(ymu818b_init_00_1e), 3000 },
	{ 0x000, ymu818b_init_00_ac_pre, ARRAY_SIZE(ymu818b_init_00_ac_pre), 0 },
	{ 0x000, ymu818b_init_00_1c, ARRAY_SIZE(ymu818b_init_00_1c), 0 },
	{ 0x000, ymu818b_init_00_ac_pre, ARRAY_SIZE(ymu818b_init_00_ac_pre), 0 },
	{ 0x000, ymu818b_init_00_dsp_pre,
	  ARRAY_SIZE(ymu818b_init_00_dsp_pre), 0 },
	{ 0x001, ymu818b_play_01_fe, ARRAY_SIZE(ymu818b_play_01_fe), 0 },
	{ 0x004, ymu818b_play_04_0b, ARRAY_SIZE(ymu818b_play_04_0b), 15000 },
	{ 0x0bd, ymu818b_play_bd_03, ARRAY_SIZE(ymu818b_play_bd_03), 0 },
	{ 0x0bd, ymu818b_play_bd_01, ARRAY_SIZE(ymu818b_play_bd_01), 0 },
	{ 0x002, ymu818b_play_02_7f, ARRAY_SIZE(ymu818b_play_02_7f), 0 },
	{ 0x008, ymu818b_play_08, ARRAY_SIZE(ymu818b_play_08), 0 },
	{ 0x08b, ymu818b_play_8b, ARRAY_SIZE(ymu818b_play_8b), 0 },
	{ 0x090, ymu818b_play_90, ARRAY_SIZE(ymu818b_play_90), 0 },
	{ 0x095, ymu818b_play_95, ARRAY_SIZE(ymu818b_play_95), 0 },
	{ 0x05c, ymu818b_play_5c, ARRAY_SIZE(ymu818b_play_5c), 0 },
	{ 0x04a, ymu818b_play_4a, ARRAY_SIZE(ymu818b_play_4a), 0 },
	{ 0x080, ymu818b_play_80, ARRAY_SIZE(ymu818b_play_80), 0 },
	{ 0x05e, ymu818b_play_5e, ARRAY_SIZE(ymu818b_play_5e), 0 },
	{ 0x0a0, ymu818b_play_a0_01, ARRAY_SIZE(ymu818b_play_a0_01), 0 },
	{ 0x129, ymu818b_play_129_01, ARRAY_SIZE(ymu818b_play_129_01), 0 },
	{ 0x097, ymu818b_play_97_00, ARRAY_SIZE(ymu818b_play_97_00), 1000 },
	{ 0x0a0, ymu818b_play_a0_00_02, ARRAY_SIZE(ymu818b_play_a0_00_02), 0 },
	{ 0x0a1, ymu818b_play_a1_00, ARRAY_SIZE(ymu818b_play_a1_00), 0 },
	{ 0x129, ymu818b_play_129_00, ARRAY_SIZE(ymu818b_play_129_00), 0 },
	{ 0x0a2, ymu818b_playback_coef_a2, ARRAY_SIZE(ymu818b_playback_coef_a2), 0 },
	{ 0x129, ymu818b_play_129_01, ARRAY_SIZE(ymu818b_play_129_01), 0 },
	{ 0x0a0, ymu818b_play_a0_01, ARRAY_SIZE(ymu818b_play_a0_01), 0 },
	{ 0x097, ymu818b_play_97_04, ARRAY_SIZE(ymu818b_play_97_04), 0 },
	{ 0x001, ymu818b_play_01_fc, ARRAY_SIZE(ymu818b_play_01_fc), 0 },
	{ 0x001, ymu818b_play_01_f4, ARRAY_SIZE(ymu818b_play_01_f4), 0 },
	{ 0x049, ymu818b_play_49, ARRAY_SIZE(ymu818b_play_49), 0 },
	{ 0x025, ymu818b_play_25_on, ARRAY_SIZE(ymu818b_play_25_on), 0 },
};

static const struct ymu818b_init_block ymu818b_default_init[] = {
	{ 0x017, ymu818b_init_17, ARRAY_SIZE(ymu818b_init_17), 0 },
	{ 0x006, ymu818b_init_06, ARRAY_SIZE(ymu818b_init_06), 0 },
	{ 0x000, ymu818b_init_00_3e, ARRAY_SIZE(ymu818b_init_00_3e), 0 },
	{ 0x000, ymu818b_init_00_1e, ARRAY_SIZE(ymu818b_init_00_1e), 2000 },
	{ 0x000, ymu818b_init_00_power, ARRAY_SIZE(ymu818b_init_00_power), 0 },
	{ 0x0a2, ymu818b_init_a2, ARRAY_SIZE(ymu818b_init_a2), 0 },
	{ 0x0e1, ymu818b_init_e1, ARRAY_SIZE(ymu818b_init_e1), 0 },
	{ 0x0f3, ymu818b_init_f3, ARRAY_SIZE(ymu818b_init_f3), 0 },
	{ 0x0f9, ymu818b_init_f9, ARRAY_SIZE(ymu818b_init_f9), 0 },
	{ 0x0ff, ymu818b_init_ff, ARRAY_SIZE(ymu818b_init_ff), 0 },
	{ 0x105, ymu818b_init_105, ARRAY_SIZE(ymu818b_init_105), 0 },
	{ 0x111, ymu818b_init_111, ARRAY_SIZE(ymu818b_init_111), 0 },
	{ 0x000, ymu818b_init_00_post, ARRAY_SIZE(ymu818b_init_00_post), 0 },
	{ 0x02d, ymu818b_init_ad, ARRAY_SIZE(ymu818b_init_ad), 0 },
	{ 0x000, ymu818b_init_00_ac_pre, ARRAY_SIZE(ymu818b_init_00_ac_pre), 0 },
	{ 0x02c, ymu818b_init_ac, ARRAY_SIZE(ymu818b_init_ac), 0 },
	{ 0x000, ymu818b_init_00_dsp_pre,
	  ARRAY_SIZE(ymu818b_init_00_dsp_pre), 0 },
	{ 0x0a2, ymu818b_playback_coef_a2,
	  ARRAY_SIZE(ymu818b_playback_coef_a2), 0 },
	{ 0x0c0, ymu818b_playback_coef_c0,
	  ARRAY_SIZE(ymu818b_playback_coef_c0), 0 },
	{ 0x000, ymu818b_init_00_ac_pre, ARRAY_SIZE(ymu818b_init_00_ac_pre), 0 },
	{ 0x000, ymu818b_init_00_1c, ARRAY_SIZE(ymu818b_init_00_1c), 500 },
	{ 0x000, ymu818b_init_00_ac_pre, ARRAY_SIZE(ymu818b_init_00_ac_pre), 0 },
	{ 0x000, ymu818b_init_00_1e, ARRAY_SIZE(ymu818b_init_00_1e), 0 },
	{ 0x000, ymu818b_init_00_ac_pre, ARRAY_SIZE(ymu818b_init_00_ac_pre), 0 },
	{ 0x000, ymu818b_init_00_3e, ARRAY_SIZE(ymu818b_init_00_3e), 0 },
	{ 0x000, ymu818b_init_00_ac_pre, ARRAY_SIZE(ymu818b_init_00_ac_pre), 0 },
	{ 0x000, ymu818b_init_00_final, ARRAY_SIZE(ymu818b_init_00_final), 0 },
};

static int ymu818b_reg_addr(unsigned int reg, u8 *buf)
{
	if (reg > 0x1ff)
		return -EINVAL;

	if (reg < 0x80) {
		buf[0] = reg | 0x80;
		return 1;
	}

	buf[0] = reg & 0x7f;
	buf[1] = (reg >> 7) | 0x80;

	return 2;
}

static void ymu818b_transfer_delay(struct ymu818b *ymu818b)
{
	/* PW-SH2 CE waits 100 DIGCTL microseconds after every I2C transfer. */
	if (ymu818b->transfer_delay_us)
		usleep_range(ymu818b->transfer_delay_us,
			     ymu818b->transfer_delay_us + 100);
}

static int ymu818b_write(struct ymu818b *ymu818b, unsigned int reg,
			 unsigned int val)
{
	u8 buf[3];
	int len;
	int ret;
	int attempt;

	if (val > 0xff)
		return -EINVAL;

	len = ymu818b_reg_addr(reg, buf);
	if (len < 0)
		return len;

	buf[len++] = val;
	for (attempt = 0; attempt < YMU818B_I2C_RETRIES; attempt++) {
		ret = i2c_master_send(ymu818b->client, buf, len);
		ymu818b_transfer_delay(ymu818b);
		if (ret == len)
			return 0;

		if (ret >= 0)
			ret = -EIO;
		if (attempt + 1 < YMU818B_I2C_RETRIES)
			usleep_range(YMU818B_I2C_RETRY_US,
				     YMU818B_I2C_RETRY_US + 1000);
	}

	return ret;
}

static int ymu818b_raw_send(struct ymu818b *ymu818b, const u8 *buf,
			    unsigned int len)
{
	struct i2c_client *client = ymu818b->client;
	int ret;
	int attempt;

	for (attempt = 0; attempt < YMU818B_I2C_RETRIES; attempt++) {
		ret = i2c_master_send(client, buf, len);
		ymu818b_transfer_delay(ymu818b);
		if (ret == len)
			return 0;

		if (ret >= 0)
			ret = -EIO;
		if (attempt + 1 < YMU818B_I2C_RETRIES)
			usleep_range(YMU818B_I2C_RETRY_US,
				     YMU818B_I2C_RETRY_US + 1000);
	}

	return ret;
}

static int ymu818b_mc1n2_write_b(struct ymu818b *ymu818b, u8 adr, u8 window,
				 u8 reg, u8 val)
{
	u8 buf[] = {
		adr << 1,
		reg,
		window << 1,
		val,
	};

	return ymu818b_raw_send(ymu818b, buf, sizeof(buf));
}

static int ymu818b_read(struct ymu818b *ymu818b, unsigned int reg)
{
	struct i2c_client *client = ymu818b->client;
	u8 addr[2];
	u8 val;
	int len;
	int ret;
	struct i2c_msg msgs[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.buf = addr,
		}, {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = &val,
		},
	};

	len = ymu818b_reg_addr(reg, addr);
	if (len < 0)
		return len;

	msgs[0].len = len;
	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	ymu818b_transfer_delay(ymu818b);
	if (ret < 0)
		return ret;
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	return val;
}

static int ymu818b_write_block(struct ymu818b *ymu818b,
			       const struct ymu818b_init_block *block)
{
	const u8 *vals = block->vals;
	u8 *buf;
	int addr_len;
	int len;
	int ret;
	int attempt;

	if (!block->count)
		return 0;

	buf = kmalloc(block->count + 2, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	addr_len = ymu818b_reg_addr(block->reg, buf);
	if (addr_len < 0) {
		kfree(buf);
		return addr_len;
	}

	if (ymu818b->sh2) {
		if (vals == ymu818b_init_a2)
			vals = ymu818b_sh2_init_a2;
		else if (vals == ymu818b_playback_coef_c0)
			vals = ymu818b_sh2_playback_coef_c0;
	}
	memcpy(buf + addr_len, vals, block->count);
	len = addr_len + block->count;
	for (attempt = 0; attempt < YMU818B_I2C_RETRIES; attempt++) {
		ret = i2c_master_send(ymu818b->client, buf, len);
		ymu818b_transfer_delay(ymu818b);
		if (ret == len) {
			ret = 0;
			break;
		}

		if (ret >= 0)
			ret = -EIO;
		if (attempt + 1 < YMU818B_I2C_RETRIES)
			usleep_range(YMU818B_I2C_RETRY_US,
				     YMU818B_I2C_RETRY_US + 1000);
	}
	kfree(buf);
	if (ret)
		return ret;

	if (block->delay_us_after)
		usleep_range(block->delay_us_after,
			     block->delay_us_after + 1000);

	return 0;
}

static int ymu818b_apply_init_sequence(struct ymu818b *ymu818b);

static int ymu818b_apply_default_init_sequence(struct ymu818b *ymu818b)
{
	struct device *dev = &ymu818b->client->dev;
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(ymu818b_default_init); i++) {
		ret = ymu818b_write_block(ymu818b, &ymu818b_default_init[i]);
		if (ret) {
			dev_err(dev, "failed to write default init block 0x%x: %d\n",
				ymu818b_default_init[i].reg, ret);
			return ret;
		}
	}

	dev_info(dev, "applied Windows CE compatible default init sequence\n");

	return 0;
}

static int ymu818b_configure(struct ymu818b *ymu818b)
{
	int ret = 0;

	mutex_lock(&ymu818b->lock);
	if (!ymu818b->configured) {
		ret = ymu818b_apply_init_sequence(ymu818b);
		if (!ret)
			ymu818b->configured = true;
	}
	mutex_unlock(&ymu818b->lock);

	return ret;
}

static int ymu818b_wait_reg_clear(struct ymu818b *ymu818b, unsigned int reg,
				  unsigned int mask, unsigned int tries)
{
	int ret;

	while (tries--) {
		ret = ymu818b_read(ymu818b, reg);
		if (ret < 0)
			return ret;
		if (!(ret & mask))
			return 0;

		usleep_range(1000, 2000);
	}

	return -ETIMEDOUT;
}

static int __maybe_unused
ymu818b_apply_mc1n2_speaker_path(struct ymu818b *ymu818b)
{
	struct device *dev = &ymu818b->client->dev;
	int ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_BASE_ADR,
				    MC1N2_MCI_BASE_WINDOW,
				    MC1N2_MCI_SD_MSK_1,
				    MC1N2_SDIN0_UNMASK);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_BASE_ADR,
				    MC1N2_MCI_BASE_WINDOW,
				    MC1N2_MCI_BCLK_MSK_1,
				    MC1N2_BCLK0_UNMASK);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_BASE_ADR,
				    MC1N2_MCI_BASE_WINDOW,
				    MC1N2_MCI_PA_MSK,
				    MC1N2_PA_MASK_DEFAULT);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_MIX_ADR,
				    MC1N2_MCI_MIX_WINDOW,
				    MC1N2_MCI_DIMODE0,
				    MC1N2_DIO_DA);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_MIX_ADR,
				    MC1N2_MCI_MIX_WINDOW,
				    MC1N2_MCI_DIRSRC_RATE0_MSB,
				    MC1N2_DIRSRC_RATE_44100_MSB);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_MIX_ADR,
				    MC1N2_MCI_MIX_WINDOW,
				    MC1N2_MCI_DIRSRC_RATE0_LSB,
				    MC1N2_DIRSRC_RATE_44100_LSB);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_MIX_ADR,
				    MC1N2_MCI_MIX_WINDOW,
				    MC1N2_MCI_DI_FS0,
				    MC1N2_DI_FS_44100_BCK32);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_MIX_ADR,
				    MC1N2_MCI_MIX_WINDOW,
				    MC1N2_MCI_DI0_SRC,
				    MC1N2_DI_SRC_RATE_SET);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_MIX_ADR,
				    MC1N2_MCI_MIX_WINDOW,
				    MC1N2_MCI_DIX0_FMT,
				    MC1N2_DIX0_FMT_I2S_16);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_MIX_ADR,
				    MC1N2_MCI_MIX_WINDOW,
				    MC1N2_MCI_DIR0_CH,
				    MC1N2_DIR0_CH_STEREO);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_MIX_ADR,
				    MC1N2_MCI_MIX_WINDOW,
				    MC1N2_MCI_SOURCE,
				    MC1N2_DAC_SOURCE_DIR0);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_MIX_ADR,
				    MC1N2_MCI_MIX_WINDOW,
				    MC1N2_MCI_DIX0_START,
				    MC1N2_DIR0_START);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_MIX_ADR,
				    MC1N2_MCI_MIX_WINDOW,
				    MC1N2_MCI_DIR0_VOLL,
				    MC1N2_DIG_VOL_0DB_LATCH);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_MIX_ADR,
				    MC1N2_MCI_MIX_WINDOW,
				    MC1N2_MCI_DIR0_VOLR,
				    MC1N2_DIG_VOL_0DB);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_MIX_ADR,
				    MC1N2_MCI_MIX_WINDOW,
				    MC1N2_MCI_MASTER_OUTL,
				    MC1N2_DIG_VOL_0DB_LATCH);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_MIX_ADR,
				    MC1N2_MCI_MIX_WINDOW,
				    MC1N2_MCI_MASTER_OUTR,
				    MC1N2_DIG_VOL_0DB);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_MIX_ADR,
				    MC1N2_MCI_MIX_WINDOW,
				    MC1N2_MCI_DAC_ATTL,
				    MC1N2_DIG_VOL_0DB_LATCH);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_MIX_ADR,
				    MC1N2_MCI_MIX_WINDOW,
				    MC1N2_MCI_DAC_ATTR,
				    MC1N2_DIG_VOL_0DB);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_ANA_ADR,
				    MC1N2_MCI_ANA_WINDOW,
				    MC1N2_MCI_SPL_MIX,
				    MC1N2_DAC_MIX);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_ANA_ADR,
				    MC1N2_MCI_ANA_WINDOW,
				    MC1N2_MCI_SPL_MONO,
				    MC1N2_DAC_MONO);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_ANA_ADR,
				    MC1N2_MCI_ANA_WINDOW,
				    MC1N2_MCI_SPR_MIX,
				    MC1N2_DAC_MIX_R);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_ANA_ADR,
				    MC1N2_MCI_ANA_WINDOW,
				    MC1N2_MCI_SPR_MONO,
				    MC1N2_DAC_MONO_R);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_ANA_ADR,
				    MC1N2_MCI_ANA_WINDOW,
				    MC1N2_MCI_APMOFF,
				    MC1N2_APMOFF_MANUAL);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_ANA_ADR,
				    MC1N2_MCI_ANA_WINDOW,
				    MC1N2_MCI_PWM_ANALOG_0,
				    MC1N2_PWM_ANALOG0_ON);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_CODEC_ADR,
				    MC1N2_MCI_CODEC_WINDOW,
				    MC1N2_MCI_DAC_CONFIG,
				    MC1N2_DAC_CONFIG_NSMUTE);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_CODEC_ADR,
				    MC1N2_MCI_CODEC_WINDOW,
				    MC1N2_MCI_DAC_CONFIG,
				    MC1N2_DAC_CONFIG_ON_NSMUTE);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_CODEC_ADR,
				    MC1N2_MCI_CODEC_WINDOW,
				    MC1N2_MCI_DAC_CONFIG,
				    MC1N2_DAC_CONFIG_ON);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_ANA_ADR,
				    MC1N2_MCI_ANA_WINDOW,
				    MC1N2_MCI_PWM_ANALOG_3,
				    MC1N2_PWM_DAC_ON);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_ANA_ADR,
				    MC1N2_MCI_ANA_WINDOW,
				    MC1N2_MCI_SP_MODE,
				    MC1N2_SP_MODE_NORMAL);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_ANA_ADR,
				    MC1N2_MCI_ANA_WINDOW,
				    MC1N2_MCI_PWM_ANALOG_1,
				    MC1N2_PWM_SP_STAGE1);
	if (ret)
		return ret;

	usleep_range(150, 300);

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_ANA_ADR,
				    MC1N2_MCI_ANA_WINDOW,
				    MC1N2_MCI_PWM_ANALOG_1,
				    MC1N2_PWM_SP_ON);
	if (ret)
		return ret;

	msleep(50);

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_ANA_ADR,
				    MC1N2_MCI_ANA_WINDOW,
				    MC1N2_MCI_SPVOL_L,
				    MC1N2_SPVOL_L_MAX);
	if (ret)
		return ret;

	ret = ymu818b_mc1n2_write_b(ymu818b, MC1N2_MCI_ANA_ADR,
				    MC1N2_MCI_ANA_WINDOW,
				    MC1N2_MCI_SPVOL_R,
				    MC1N2_SPVOL_R_MAX);
	if (ret)
		return ret;

	dev_info(dev, "applied MC1N2 speaker path with analog power up\n");

	return 0;
}

static int ymu818b_start_playback(struct ymu818b *ymu818b)
{
	struct device *dev = &ymu818b->client->dev;
	unsigned int i;
	int ret;

	mutex_lock(&ymu818b->lock);

	for (i = 0; i < YMU818B_PLAYBACK_NOTIFY_INDEX; i++) {
		ret = ymu818b_write_block(ymu818b, &ymu818b_playback_start[i]);
		if (ret) {
			dev_err(dev, "failed to write playback block 0x%x: %d\n",
				ymu818b_playback_start[i].reg, ret);
			goto out_unlock;
		}

		if (ymu818b_playback_start[i].vals == ymu818b_play_97_00) {
			ret = ymu818b_wait_reg_clear(ymu818b, 0x096, 0xff, 50);
			if (ret) {
				dev_err(dev, "playback DSP wait timed out: %d\n",
					ret);
				goto out_unlock;
			}
		}
	}

	ret = ymu818b_write_block(ymu818b,
				  &ymu818b_playback_start
				  [YMU818B_PLAYBACK_NOTIFY_INDEX]);
	if (ret) {
		dev_err(dev, "failed to notify media playback start: %d\n", ret);
		goto out_unlock;
	}

	dev_info(dev, "applied Windows CE playback sequence\n");

out_unlock:
	mutex_unlock(&ymu818b->lock);

	return ret;
}

static int ymu818b_stop_playback(struct ymu818b *ymu818b)
{
	int ret;

	mutex_lock(&ymu818b->lock);
	ret = ymu818b_write(ymu818b, 0x025, ymu818b_play_25_off[0]);
	if (!ret)
		ret = ymu818b_wait_reg_clear(ymu818b, 0x012, 0x08, 50);
	mutex_unlock(&ymu818b->lock);

	return ret == -ETIMEDOUT ? 0 : ret;
}

static int ymu818b_apply_init_sequence(struct ymu818b *ymu818b)
{
	struct device *dev = &ymu818b->client->dev;
	struct device_node *np = dev->of_node;
	int count;
	int i;
	int ret;

	count = of_property_count_u32_elems(np, "yamaha,init-sequence");
	if (count == -EINVAL)
		return ymu818b_apply_default_init_sequence(ymu818b);
	if (count < 0)
		return count;
	if (count % 2) {
		dev_err(dev,
			"yamaha,init-sequence must contain register/value pairs\n");
		return -EINVAL;
	}

	for (i = 0; i < count; i += 2) {
		u32 reg;
		u32 val;

		ret = of_property_read_u32_index(np, "yamaha,init-sequence",
						 i, &reg);
		if (ret)
			return ret;

		ret = of_property_read_u32_index(np, "yamaha,init-sequence",
						 i + 1, &val);
		if (ret)
			return ret;

		ret = ymu818b_write(ymu818b, reg, val);
		if (ret) {
			dev_err(dev, "failed to write init register 0x%x: %d\n",
				reg, ret);
			return ret;
		}
	}

	return 0;
}

static void ymu818b_reset(struct ymu818b *ymu818b)
{
	if (!ymu818b->reset_gpio)
		return;

	gpiod_set_value_cansleep(ymu818b->reset_gpio, 0);
	msleep(10);
	gpiod_set_value_cansleep(ymu818b->reset_gpio, 1);
	msleep(20);
	gpiod_set_value_cansleep(ymu818b->reset_gpio, 0);
	msleep(10);
}

static int ymu818b_set_dai_sysclk(struct snd_soc_dai *dai, int clk_id,
				  unsigned int freq, int dir)
{
	struct ymu818b *ymu818b = snd_soc_component_get_drvdata(dai->component);

	ymu818b->sysclk = freq;

	return 0;
}

static int ymu818b_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ymu818b_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	if (params_rate(params) != 44100 ||
	    params_format(params) != SNDRV_PCM_FORMAT_S16_LE ||
	    params_channels(params) != 2)
		return -EINVAL;

	return 0;
}

static int ymu818b_prepare(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *dai)
{
	struct ymu818b *ymu818b = snd_soc_component_get_drvdata(dai->component);
	int ret;

	if (substream->stream != SNDRV_PCM_STREAM_PLAYBACK)
		return -EOPNOTSUPP;
	if (!ymu818b->sysclk)
		return -EINVAL;

	/* PCM prepare is repeated on XRUN recovery. The fixed-format codec
	 * path is still active: keep it instead of resetting the PLL and DSP.
	 * Shutdown clears both flags when the stream is closed.
	 */
	if (ymu818b->configured && ymu818b->playback_started)
		return 0;

	/* Machine probe/hw_params have enabled MCLK before resetting the codec. */
	ymu818b->configured = false;
	ymu818b_reset(ymu818b);
	ret = ymu818b_read(ymu818b, 0x05);
	if (ret < 0)
		return ret;
	if (ret != 0x60 && ret != 0x68) {
		dev_err(&ymu818b->client->dev, "unexpected chip ID: 0x%02x\n", ret);
		return -ENODEV;
	}
	dev_info(&ymu818b->client->dev, "prepare: chip ID 0x%02x, MCLK %u Hz\n",
		 ret, ymu818b->sysclk);
	ret = ymu818b_configure(ymu818b);
	if (ret)
		return ret;
	ret = ymu818b_start_playback(ymu818b);
	if (ret) {
		ymu818b_stop_playback(ymu818b);
		ymu818b->configured = false;
		return ret;
	}
	ymu818b->playback_started = true;
	return 0;
}

static void ymu818b_shutdown(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *dai)
{
	struct ymu818b *ymu818b = snd_soc_component_get_drvdata(dai->component);
	int ret;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK &&
	    ymu818b->playback_started) {
		ret = ymu818b_stop_playback(ymu818b);
		if (ret)
			dev_warn(&ymu818b->client->dev, "playback stop failed: %d\n", ret);
		ymu818b->playback_started = false;
		ymu818b->configured = false;
	}
}

static const struct snd_soc_dai_ops ymu818b_dai_ops = {
	.set_sysclk = ymu818b_set_dai_sysclk,
	.set_fmt = ymu818b_set_dai_fmt,
	.hw_params = ymu818b_hw_params,
	.prepare = ymu818b_prepare,
	.shutdown = ymu818b_shutdown,
};

static struct snd_soc_dai_driver ymu818b_dai = {
	.name = "ymu818b-hifi",
	.playback = {
		.stream_name = "HiFi Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_44100,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &ymu818b_dai_ops,
};

static const struct snd_soc_dapm_widget ymu818b_dapm_widgets[] = {
	SND_SOC_DAPM_OUTPUT("Headphone"),
	SND_SOC_DAPM_OUTPUT("Speaker"),
};

static const struct snd_soc_dapm_route ymu818b_dapm_routes[] = {
	{ "Headphone", NULL, "HiFi Playback" },
	{ "Speaker", NULL, "HiFi Playback" },
};

static const struct snd_soc_component_driver soc_component_dev_ymu818b = {
	.dapm_widgets = ymu818b_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ymu818b_dapm_widgets),
	.dapm_routes = ymu818b_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(ymu818b_dapm_routes),
	.idle_bias_on = 1,
	.use_pmdown_time = 1,
	.endianness = 1,
	.non_legacy_dai_naming = 1,
};

static int ymu818b_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	struct ymu818b *ymu818b;
	int ret;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -EIO;

	ymu818b = devm_kzalloc(&client->dev, sizeof(*ymu818b), GFP_KERNEL);
	if (!ymu818b)
		return -ENOMEM;

	ymu818b->client = client;
	ymu818b->sh2 = of_machine_is_compatible("sharp,pw-sh2");
	mutex_init(&ymu818b->lock);
	i2c_set_clientdata(client, ymu818b);

	ymu818b->reset_gpio = devm_gpiod_get_optional(&client->dev, "reset",
						      GPIOD_OUT_LOW);
	if (IS_ERR(ymu818b->reset_gpio))
		return PTR_ERR(ymu818b->reset_gpio);

	of_property_read_u32(client->dev.of_node, "yamaha,i2c-transfer-delay-us",
			     &ymu818b->transfer_delay_us);
	if (ymu818b->transfer_delay_us > 10000)
		return -EINVAL;

	/* Read the inherited CE state without resetting before MCLK is available. */
	ret = ymu818b_read(ymu818b, 0x05);
	if (ret < 0)
		dev_warn(&client->dev, "chip ID read failed: %d\n", ret);
	else
		dev_info(&client->dev, "register 0x05 = 0x%02x\n", ret);

	return devm_snd_soc_register_component(&client->dev,
					       &soc_component_dev_ymu818b,
					       &ymu818b_dai, 1);
}

static const struct of_device_id ymu818b_of_match[] = {
	{ .compatible = "yamaha,ymu818b" },
	{ }
};
MODULE_DEVICE_TABLE(of, ymu818b_of_match);

static const struct i2c_device_id ymu818b_i2c_id[] = {
	{ "ymu818b", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ymu818b_i2c_id);

static struct i2c_driver ymu818b_i2c_driver = {
	.driver = {
		.name = "ymu818b",
		.of_match_table = ymu818b_of_match,
	},
	.probe = ymu818b_i2c_probe,
	.id_table = ymu818b_i2c_id,
};

module_i2c_driver(ymu818b_i2c_driver);

MODULE_DESCRIPTION("ASoC Yamaha YMU818B codec driver");
MODULE_AUTHOR("Codex");
MODULE_LICENSE("GPL");
