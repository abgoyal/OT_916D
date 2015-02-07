

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/spinlock.h>
#include <linux/videodev2.h>

#include <media/v4l2-device.h>

#include <sound/core.h>
#include <sound/control.h>
#include <sound/tlv.h>

#include "cx18-alsa.h"
#include "cx18-driver.h"

static inline int dB_to_cx18_av_vol(int dB)
{
	if (dB < -96)
		dB = -96;
	else if (dB > 8)
		dB = 8;
	return (dB + 119) << 9;
}

static inline int cx18_av_vol_to_dB(int v)
{
	if (v < (23 << 9))
		v = (23 << 9);
	else if (v > (127 << 9))
		v = (127 << 9);
	return (v >> 9) - 119;
}

static int snd_cx18_mixer_tv_vol_info(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	/* We're already translating values, just keep this control in dB */
	uinfo->value.integer.min  = -96;
	uinfo->value.integer.max  =   8;
	uinfo->value.integer.step =   1;
	return 0;
}

static int snd_cx18_mixer_tv_vol_get(struct snd_kcontrol *kctl,
				     struct snd_ctl_elem_value *uctl)
{
	struct snd_cx18_card *cxsc = snd_kcontrol_chip(kctl);
	struct cx18 *cx = to_cx18(cxsc->v4l2_dev);
	struct v4l2_control vctrl;
	int ret;

	vctrl.id = V4L2_CID_AUDIO_VOLUME;
	vctrl.value = dB_to_cx18_av_vol(uctl->value.integer.value[0]);

	snd_cx18_lock(cxsc);
	ret = v4l2_subdev_call(cx->sd_av, core, g_ctrl, &vctrl);
	snd_cx18_unlock(cxsc);

	if (!ret)
		uctl->value.integer.value[0] = cx18_av_vol_to_dB(vctrl.value);
	return ret;
}

static int snd_cx18_mixer_tv_vol_put(struct snd_kcontrol *kctl,
				     struct snd_ctl_elem_value *uctl)
{
	struct snd_cx18_card *cxsc = snd_kcontrol_chip(kctl);
	struct cx18 *cx = to_cx18(cxsc->v4l2_dev);
	struct v4l2_control vctrl;
	int ret;

	vctrl.id = V4L2_CID_AUDIO_VOLUME;
	vctrl.value = dB_to_cx18_av_vol(uctl->value.integer.value[0]);

	snd_cx18_lock(cxsc);

	/* Fetch current state */
	ret = v4l2_subdev_call(cx->sd_av, core, g_ctrl, &vctrl);

	if (ret ||
	    (cx18_av_vol_to_dB(vctrl.value) != uctl->value.integer.value[0])) {

		/* Set, if needed */
		vctrl.value = dB_to_cx18_av_vol(uctl->value.integer.value[0]);
		ret = v4l2_subdev_call(cx->sd_av, core, s_ctrl, &vctrl);
		if (!ret)
			ret = 1; /* Indicate control was changed w/o error */
	}
	snd_cx18_unlock(cxsc);

	return ret;
}


/* This is a bit of overkill, the slider is already in dB internally */
static DECLARE_TLV_DB_SCALE(snd_cx18_mixer_tv_vol_db_scale, -9600, 100, 0);

static struct snd_kcontrol_new snd_cx18_mixer_tv_vol __initdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Analog TV Capture Volume",
	.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
		  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
	.info = snd_cx18_mixer_tv_volume_info,
	.get = snd_cx18_mixer_tv_volume_get,
	.put = snd_cx18_mixer_tv_volume_put,
	.tlv.p = snd_cx18_mixer_tv_vol_db_scale
};


/* FIXME - add stereo, lang1, lang2, mono menu */
/* FIXME - add CS5345 I2S volume for HVR-1600 */

int __init snd_cx18_mixer_create(struct snd_cx18_card *cxsc)
{
	struct v4l2_device *v4l2_dev = cxsc->v4l2_dev;
	struct snd_card *sc = cxsc->sc;
	int ret;

	strlcpy(sc->mixername, "CX23418 Mixer", sizeof(sc->mixername));

	ret = snd_ctl_add(sc, snd_ctl_new1(snd_cx18_mixer_tv_vol, cxsc));
	if (ret) {
		CX18_ALSA_WARN("%s: failed to add %s control, err %d\n",
				__func__, snd_cx18_mixer_tv_vol.name, ret);
	}
	return ret;
}
