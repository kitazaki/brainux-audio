// SPDX-License-Identifier: GPL-2.0-only
/*
 * MXS + Yamaha YMU818B machine driver for SHARP Brain PW-SH1/PW-SH2.
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include "mxs-saif.h"

struct mxs_ymu818b_priv {
	struct clk *saif1_clk;
};

#define YMU818B_MCLK_RATE(rate)	(512 * (rate) + 2000)

static int mxs_ymu818b_hw_params(struct snd_pcm_substream *substream,
				 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned int rate = params_rate(params);
	unsigned int mclk = YMU818B_MCLK_RATE(rate);
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, 0, mclk, SND_SOC_CLOCK_IN);
	if (ret)
		return ret;

	ret = snd_soc_dai_set_sysclk(cpu_dai, MXS_SAIF_MCLK, mclk,
				     SND_SOC_CLOCK_OUT);
	if (ret)
		return ret;

	return 0;
}

static const struct snd_soc_ops mxs_ymu818b_hifi_ops = {
	.hw_params = mxs_ymu818b_hw_params,
};

#define MXS_YMU818B_DAI_FMT (SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF | \
			     SND_SOC_DAIFMT_CBS_CFS)

SND_SOC_DAILINK_DEFS(hifi_tx,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "ymu818b-hifi")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(hifi_rx,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "ymu818b-hifi")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

static struct snd_soc_dai_link mxs_ymu818b_dai[] = {
	{
		.name = "HiFi Tx",
		.stream_name = "HiFi Playback",
		.dai_fmt = MXS_YMU818B_DAI_FMT,
		.ops = &mxs_ymu818b_hifi_ops,
		.playback_only = true,
		SND_SOC_DAILINK_REG(hifi_tx),
	}, {
		.name = "HiFi Rx",
		.stream_name = "HiFi Capture",
		.dai_fmt = MXS_YMU818B_DAI_FMT,
		.ops = &mxs_ymu818b_hifi_ops,
		.capture_only = true,
		SND_SOC_DAILINK_REG(hifi_rx),
	},
};

static struct snd_soc_card mxs_ymu818b_card = {
	.name = "mxs_ymu818b",
	.owner = THIS_MODULE,
	.dai_link = mxs_ymu818b_dai,
	.num_links = ARRAY_SIZE(mxs_ymu818b_dai),
};

static int mxs_ymu818b_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &mxs_ymu818b_card;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *saif_np[2];
	struct device_node *codec_np;
	struct mxs_ymu818b_priv *priv;
	int ret;
	int i;

	saif_np[0] = of_parse_phandle(np, "saif-controllers", 0);
	saif_np[1] = of_parse_phandle(np, "saif-controllers", 1);
	codec_np = of_parse_phandle(np, "audio-codec", 0);
	if (!saif_np[0] || !codec_np) {
		ret = -EINVAL;
		goto out_put_nodes;
	}
	/* Only the CE-derived playback sequence is implemented so far. */
	card->num_links = 1;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto out_put_nodes;
	}
	snd_soc_card_set_drvdata(card, priv);

	for (i = 0; i < card->num_links; i++) {
		mxs_ymu818b_dai[i].codecs->name = NULL;
		mxs_ymu818b_dai[i].codecs->of_node = codec_np;
		mxs_ymu818b_dai[i].cpus->dai_name = NULL;
		mxs_ymu818b_dai[i].cpus->of_node = saif_np[i];
		mxs_ymu818b_dai[i].platforms->name = NULL;
		mxs_ymu818b_dai[i].platforms->of_node = saif_np[i];
	}

	card->dev = &pdev->dev;
	platform_set_drvdata(pdev, card);
	ret = snd_soc_of_parse_card_name(card, "model");
	if (ret)
		card->name = "mxs_ymu818b";

	ret = mxs_saif_get_mclk(0, YMU818B_MCLK_RATE(44100), 44100);
	if (ret)
		goto out_put_nodes;

	if (saif_np[1]) {
		priv->saif1_clk = of_clk_get(saif_np[1], 0);
		if (IS_ERR(priv->saif1_clk)) {
			ret = PTR_ERR(priv->saif1_clk);
			mxs_saif_put_mclk(0);
			goto out_put_nodes;
		}

		ret = clk_prepare_enable(priv->saif1_clk);
		if (ret) {
			clk_put(priv->saif1_clk);
			mxs_saif_put_mclk(0);
			goto out_put_nodes;
		}
	}

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		if (saif_np[1]) {
			clk_disable_unprepare(priv->saif1_clk);
			clk_put(priv->saif1_clk);
		}
		mxs_saif_put_mclk(0);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"snd_soc_register_card failed (%d)\n", ret);
	}

out_put_nodes:
	of_node_put(codec_np);
	of_node_put(saif_np[0]);
	of_node_put(saif_np[1]);

	return ret;
}

static int mxs_ymu818b_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);
	struct mxs_ymu818b_priv *priv = snd_soc_card_get_drvdata(card);

	if (priv && !IS_ERR_OR_NULL(priv->saif1_clk)) {
		clk_disable_unprepare(priv->saif1_clk);
		clk_put(priv->saif1_clk);
	}
	mxs_saif_put_mclk(0);

	return 0;
}

static const struct of_device_id mxs_ymu818b_dt_ids[] = {
	{ .compatible = "sharp,mxs-audio-ymu818b" },
	{ }
};
MODULE_DEVICE_TABLE(of, mxs_ymu818b_dt_ids);

static struct platform_driver mxs_ymu818b_audio_driver = {
	.driver = {
		.name = "mxs-ymu818b",
		.of_match_table = mxs_ymu818b_dt_ids,
	},
	.probe = mxs_ymu818b_probe,
	.remove = mxs_ymu818b_remove,
};

module_platform_driver(mxs_ymu818b_audio_driver);

MODULE_DESCRIPTION("MXS YMU818B ASoC machine driver");
MODULE_AUTHOR("Codex");
MODULE_LICENSE("GPL");
