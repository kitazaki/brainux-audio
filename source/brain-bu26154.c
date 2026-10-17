// SPDX-License-Identifier: GPL-2.0-only
/* Experimental Brain BU26154 bring-up, ROHM datasheet Rev.002 (2015).
 * Supports PW-SH3 through PW-SH7 DTs; hardware tested on PW-SH3.
 * Original implementation tested on PW-SA3 using the PW-SH3 DT.
 * PW-SH6/SH7 have DT-controlled reset and speaker mode; no flash writes.
 * Adapted from tslmy/buildbrain lmy/buildroot-audio for Linux 5.4.
 * Only 44.1 kHz, S16_LE, stereo SAIF slots, one stream at a time initially.
 */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "mxs-saif.h"

static struct i2c_client *codec_client;
static struct platform_device *card_device;
static bool codec_client_from_dt;

struct brain_codec {
	struct regmap *map;
	struct gpio_desc *reset;
	struct mutex lock;
	bool opened;
};

/* BU26154 uses even indexes for reads and odd indexes for writes (p.38).
 * Normal regmap-i2c cannot express the write-index bit independently.
 */
static int brain_reg_write(void *context, unsigned int reg, unsigned int val)
{
	struct i2c_client *client = context;
	u8 data[] = { reg | 1, val };
	int ret = i2c_master_send(client, data, sizeof(data));

	return ret == sizeof(data) ? 0 : ret < 0 ? ret : -EIO;
}

static int brain_reg_read(void *context, unsigned int reg, unsigned int *val)
{
	struct i2c_client *client = context;
	u8 index = reg & ~1, data;
	struct i2c_msg msgs[] = {
		{ .addr = client->addr, .len = 1, .buf = &index },
		{ .addr = client->addr, .flags = I2C_M_RD,
		  .len = 1, .buf = &data },
	};
	int ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));

	if (ret != ARRAY_SIZE(msgs))
		return ret < 0 ? ret : -EIO;
	*val = data;
	return 0;
}

static const struct regmap_bus brain_reg_bus = {
	.reg_write = brain_reg_write,
	.reg_read = brain_reg_read,
};

static const struct regmap_config brain_reg_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.reg_stride = 2,
	.max_register = 0xfe,
	.cache_type = REGCACHE_NONE,
};

static const char * const output_texts[] = { "Headphone", "Speaker" };
static SOC_ENUM_SINGLE_DECL(output_enum, 0x26, 1, output_texts);
static const struct snd_kcontrol_new brain_controls[] = {
	/* SPEN only; headphone amps remain enabled, no jack detection yet. */
	SOC_ENUM("Output Route", output_enum),
	SOC_SINGLE("Playback Volume", 0x3a, 0, 0x19, 0),
	SOC_SINGLE("Mic Capture Volume", 0x5a, 5, 7, 0),
};

static int brain_startup(struct snd_pcm_substream *s, struct snd_soc_dai *dai)
{
	struct brain_codec *codec =
		snd_soc_component_get_drvdata(dai->component);
	int ret = 0;

	mutex_lock(&codec->lock);
	if (codec->opened)
		ret = -EBUSY;
	else
		codec->opened = true;
	mutex_unlock(&codec->lock);
	return ret;
}

static int brain_hw_params(struct snd_pcm_substream *s,
			   struct snd_pcm_hw_params *params,
			   struct snd_soc_dai *dai)
{
	struct brain_codec *codec =
		snd_soc_component_get_drvdata(dai->component);
	/* 512fs MCLK, no PLL; 16-bit I2S, normal polarity, codec slave. */
	const struct reg_sequence settings[] = {
		{ 0x12, 0x00 }, { 0x0c, 0x01 }, { 0x00, 0x07 },
		{ 0x0e, 0x06 }, { 0x60, 0x80 }, { 0x62, 0x80 },
		{ 0x64, 0x00 }, { 0x66, 0x01 }, { 0x58, 0x20 },
		{ 0x0c, 0x05 },
	};

	if (params_rate(params) != 44100 ||
	    params_format(params) != SNDRV_PCM_FORMAT_S16_LE)
		return -EINVAL;
	return regmap_multi_reg_write(codec->map, settings,
				      ARRAY_SIZE(settings));
}

static int brain_prepare(struct snd_pcm_substream *s, struct snd_soc_dai *dai)
{
	struct brain_codec *codec =
		snd_soc_component_get_drvdata(dai->component);
	bool capture = s->stream == SNDRV_PCM_STREAM_CAPTURE;
	int ret;
	const struct reg_sequence power[] = {
		{ 0x12, 0x00 }, { 0x20, capture ? 0x0d : 0xc9 },
		{ 0x22, capture ? 0x0a : 0x00 },
		{ 0x24, capture ? 0x00 : 0x06 },
		{ 0x14, 0x20 }, { 0x68, 0x00 },
	};

	ret = regmap_multi_reg_write(codec->map, power, ARRAY_SIZE(power));
	if (ret)
		return ret;
	/* VMID fast charge, then normal mode; preserve output selection. */
	msleep(100);
	ret = regmap_write(codec->map, 0x20, capture ? 0x0e : 0xca);
	if (ret)
		return ret;
	ret = regmap_write(codec->map, 0x12, capture ? 1 : 2);
	if (!ret)
		msleep(100); /* Microphone charge and DSP initialization. */
	return ret;
}

static void brain_power_off(struct brain_codec *codec)
{
	regmap_write(codec->map, 0x12, 0);
	regmap_write(codec->map, 0x22, 0);
	regmap_write(codec->map, 0x24, 0);
	regmap_write(codec->map, 0x20, 0);
}

static void brain_shutdown(struct snd_pcm_substream *s, struct snd_soc_dai *dai)
{
	struct brain_codec *codec =
		snd_soc_component_get_drvdata(dai->component);

	brain_power_off(codec);
	mutex_lock(&codec->lock);
	codec->opened = false;
	mutex_unlock(&codec->lock);
}

static const struct snd_soc_dai_ops brain_dai_ops = {
	.startup = brain_startup,
	.hw_params = brain_hw_params,
	.prepare = brain_prepare,
	.shutdown = brain_shutdown,
};

static struct snd_soc_dai_driver brain_dai = {
	.name = "bu26154-hifi",
	.playback = {
		.stream_name = "Playback", .channels_min = 2, .channels_max = 2,
		.rates = SNDRV_PCM_RATE_44100,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "Capture", .channels_min = 2, .channels_max = 2,
		.rates = SNDRV_PCM_RATE_44100,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &brain_dai_ops,
};

static const struct snd_soc_component_driver brain_component = {
	.name = "bu26154",
	.controls = brain_controls,
	.num_controls = ARRAY_SIZE(brain_controls),
};

static int brain_i2c_probe(struct i2c_client *client)
{
	struct brain_codec *codec;
	unsigned int val;
	int ret;
	bool class_d = of_property_read_bool(client->dev.of_node,
					     "rohm,speaker-class-d");
	const struct reg_sequence defaults[] = {
		{ 0x12, 0 },
		/* MAP1: connect left analog output to speaker, with 0 dB gain. */
		{ 0x1c, 1 }, { 0x84, 0x04 }, { 0x1c, 0 },
		{ 0x60, 0x80 }, { 0x62, 0x80 }, /* 16-bit PCM */
		{ 0x26, class_d ? 0x95 : 0x15 }, /* speaker off */
		{ 0x3a, 0x02 }, /* -24 dB analog output */
		{ 0x3e, 0xe7 }, /* -12 dB digital output */
		{ 0x5a, 0xe0 }, /* 27 dB single-ended microphone gain */
	};

	codec = devm_kzalloc(&client->dev, sizeof(*codec), GFP_KERNEL);
	if (!codec)
		return -ENOMEM;
	codec->map = devm_regmap_init(&client->dev, &brain_reg_bus, client,
				    &brain_reg_config);
	if (IS_ERR(codec->map))
		return PTR_ERR(codec->map);
	mutex_init(&codec->lock);
	i2c_set_clientdata(client, codec);
	codec->reset = devm_gpiod_get_optional(&client->dev, "reset",
					     GPIOD_OUT_HIGH);
	if (IS_ERR(codec->reset))
		return PTR_ERR(codec->reset);
	if (codec->reset) {
		/* Active-low RESETB: assert, then release before I2C access. */
		msleep(10);
		gpiod_set_value_cansleep(codec->reset, 0);
		msleep(5);
	}
	/* No chip ID: accept both reset (24-bit) and CE's 16-bit format. */
	ret = regmap_read(codec->map, 0x1c, &val);
	if (ret || val != 0) {
		ret = ret ? ret : -ENODEV;
		goto reset;
	}
	ret = regmap_read(codec->map, 0x60, &val);
	if (ret || (val != 0x80 && val != 0xc0)) {
		ret = ret ? ret : -ENODEV;
		goto reset;
	}
	ret = regmap_multi_reg_write(codec->map, defaults,
				     ARRAY_SIZE(defaults));
	if (!ret)
		ret = devm_snd_soc_register_component(&client->dev,
						    &brain_component,
						    &brain_dai, 1);
	if (ret) {
		/* A failed MAP1 access must not leave later controls in MAP1. */
		regmap_write(codec->map, 0x1c, 0);
		brain_power_off(codec);
		goto reset;
	}
	dev_info(&client->dev, "speaker input enabled; reset %s, amplifier %s\n",
		 codec->reset ? "managed" : "not specified",
		 class_d ? "class D" : "class AB");
	return 0;
reset:
	if (codec->reset)
		gpiod_set_value_cansleep(codec->reset, 1);
	dev_err(&client->dev, "codec initialization failed: %d\n", ret);
	return ret;
}

static int brain_i2c_remove(struct i2c_client *client)
{
	struct brain_codec *codec = i2c_get_clientdata(client);

	brain_power_off(codec);
	if (codec->reset)
		gpiod_set_value_cansleep(codec->reset, 1);
	return 0;
}

static const struct i2c_device_id brain_i2c_ids[] = {
	{ "brain-bu26154", 0 }, { }
};
/* Legacy clients are created by this module itself. Advertising their I2C
 * alias would make udev reload us indefinitely if module init fails and
 * removes the just-created client. DT clients retain their OF aliases.
 */
static const struct of_device_id brain_of_ids[] = {
	{ .compatible = "rohm,bu26154" }, { }
};
MODULE_DEVICE_TABLE(of, brain_of_ids);
static struct i2c_driver brain_i2c_driver = {
	.driver.name = "brain-bu26154",
	.driver.of_match_table = brain_of_ids,
	.probe_new = brain_i2c_probe,
	.remove = brain_i2c_remove,
	.id_table = brain_i2c_ids,
};

static int brain_machine_params(struct snd_pcm_substream *s,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = s->private_data;

	return snd_soc_dai_set_sysclk(rtd->cpu_dai, 0,
				      512 * params_rate(params), 0);
}

static const struct snd_soc_ops brain_machine_ops = {
	.hw_params = brain_machine_params,
};

static struct snd_soc_dai_link_component cpus[2], platforms[2];
static struct snd_soc_dai_link_component codec_link = {
	.name = "brain-bu26154.0-001a",
	.dai_name = "bu26154-hifi",
};
static struct snd_soc_dai_link links[2];
static struct snd_soc_card card = {
	.name = "Brain-BU26154", .owner = THIS_MODULE,
	.dai_link = links, .num_links = ARRAY_SIZE(links),
};

static void brain_release_client(void)
{
	if (codec_client_from_dt)
		put_device(&codec_client->dev);
	else
		i2c_unregister_device(codec_client);
	codec_client = NULL;
}

static int __init brain_init(void)
{
	struct i2c_adapter *adapter;
	struct i2c_board_info info = { I2C_BOARD_INFO("brain-bu26154", 0x1a) };
	struct device_node *np;
	int ret, i;

	if (!of_machine_is_compatible("sharp,pw-sh3") &&
	    !of_machine_is_compatible("sharp,pw-sh4") &&
	    !of_machine_is_compatible("sharp,pw-sh5") &&
	    !of_machine_is_compatible("sharp,pw-sh6") &&
	    !of_machine_is_compatible("sharp,pw-sh7"))
		return -ENODEV;
	ret = i2c_add_driver(&brain_i2c_driver);
	if (ret)
		return ret;
	np = of_find_compatible_node(NULL, NULL, "rohm,bu26154");
	codec_client_from_dt = np && of_device_is_available(np);
	if (codec_client_from_dt) {
		/* I2C core owns the DT client: do not create a duplicate at 0x1a. */
		codec_client = of_find_i2c_device_by_node(np);
		of_node_put(np);
		if (!codec_client) {
			ret = -EPROBE_DEFER;
			goto del_driver;
		}
	} else {
		of_node_put(np);
		adapter = i2c_get_adapter(0);
		if (!adapter) {
			ret = -ENODEV;
			goto del_driver;
		}
		codec_client = i2c_new_client_device(adapter, &info);
		i2c_put_adapter(adapter);
		if (IS_ERR(codec_client)) {
			ret = PTR_ERR(codec_client);
			goto del_driver;
		}
	}
	if (!codec_client->dev.driver) {
		ret = -ENODEV;
		goto del_client;
	}
	for (i = 0; i < 2; i++) {
		np = of_find_node_by_path(i ?
			"/apb@80000000/apbx@80040000/saif@80046000" :
			"/apb@80000000/apbx@80040000/saif@80042000");
		if (!np) {
			ret = -ENODEV;
			goto put_nodes;
		}
		cpus[i].of_node = platforms[i].of_node = np;
		links[i].name = i ? "HiFi Rx" : "HiFi Tx";
		links[i].stream_name = i ? "Capture" : "Playback";
		links[i].cpus = &cpus[i]; links[i].num_cpus = 1;
		links[i].platforms = &platforms[i]; links[i].num_platforms = 1;
		links[i].codecs = &codec_link; links[i].num_codecs = 1;
		links[i].playback_only = !i; links[i].capture_only = i;
		links[i].dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
				   SND_SOC_DAIFMT_CBS_CFS;
		links[i].ops = &brain_machine_ops;
	}
	card_device = platform_device_register_simple("brain-audio", -1,
						       NULL, 0);
	if (IS_ERR(card_device)) {
		ret = PTR_ERR(card_device);
		goto put_nodes;
	}
	card.dev = &card_device->dev;
	ret = mxs_saif_get_mclk(0, 44100 * 512, 44100);
	if (ret)
		goto del_platform;
	ret = snd_soc_register_card(&card);
	if (!ret)
		return 0;
	mxs_saif_put_mclk(0);
del_platform:
	platform_device_unregister(card_device);
put_nodes:
	for (i = 0; i < 2; i++)
		of_node_put(cpus[i].of_node);
del_client:
	brain_release_client();
del_driver:
	i2c_del_driver(&brain_i2c_driver);
	return ret;
}

static void __exit brain_exit(void)
{
	snd_soc_unregister_card(&card);
	brain_release_client();
	platform_device_unregister(card_device);
	of_node_put(cpus[0].of_node);
	of_node_put(cpus[1].of_node);
	mxs_saif_put_mclk(0);
	i2c_del_driver(&brain_i2c_driver);
}
module_init(brain_init);
module_exit(brain_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Experimental SHARP Brain BU26154 playback/capture");
