

#ifndef _LGDT3305_H_
#define _LGDT3305_H_

#include <linux/i2c.h>
#include "dvb_frontend.h"


enum lgdt3305_mpeg_mode {
	LGDT3305_MPEG_PARALLEL = 0,
	LGDT3305_MPEG_SERIAL = 1,
};

enum lgdt3305_tp_clock_edge {
	LGDT3305_TPCLK_RISING_EDGE = 0,
	LGDT3305_TPCLK_FALLING_EDGE = 1,
};

enum lgdt3305_tp_valid_polarity {
	LGDT3305_TP_VALID_LOW = 0,
	LGDT3305_TP_VALID_HIGH = 1,
};

struct lgdt3305_config {
	u8 i2c_addr;

	/* user defined IF frequency in KHz */
	u16 qam_if_khz;
	u16 vsb_if_khz;

	/* AGC Power reference - defaults are used if left unset */
	u16 usref_8vsb;   /* default: 0x32c4 */
	u16 usref_qam64;  /* default: 0x5400 */
	u16 usref_qam256; /* default: 0x2a80 */

	/* disable i2c repeater - 0:repeater enabled 1:repeater disabled */
	unsigned int deny_i2c_rptr:1;

	/* spectral inversion - 0:disabled 1:enabled */
	unsigned int spectral_inversion:1;

	/* use RF AGC loop - 0:disabled 1:enabled */
	unsigned int rf_agc_loop:1;

	enum lgdt3305_mpeg_mode mpeg_mode;
	enum lgdt3305_tp_clock_edge tpclk_edge;
	enum lgdt3305_tp_valid_polarity tpvalid_polarity;
};

#if defined(CONFIG_DVB_LGDT3305) || (defined(CONFIG_DVB_LGDT3305_MODULE) && \
				     defined(MODULE))
extern
struct dvb_frontend *lgdt3305_attach(const struct lgdt3305_config *config,
				     struct i2c_adapter *i2c_adap);
#else
static inline
struct dvb_frontend *lgdt3305_attach(const struct lgdt3305_config *config,
				     struct i2c_adapter *i2c_adap)
{
	printk(KERN_WARNING "%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif /* CONFIG_DVB_LGDT3305 */

#endif /* _LGDT3305_H_ */
