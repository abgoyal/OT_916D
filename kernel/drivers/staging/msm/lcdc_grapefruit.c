

#include "msm_fb.h"

#ifdef CONFIG_FB_MSM_TRY_MDDI_CATCH_LCDC_PRISM
#include "mddihosti.h"
#endif

static int __init lcdc_grapefruit_init(void)
{
	int ret;
	struct msm_panel_info pinfo;

#ifdef CONFIG_FB_MSM_TRY_MDDI_CATCH_LCDC_PRISM
	if (msm_fb_detect_client("lcdc_grapefruit_vga"))
		return 0;
#endif

	pinfo.xres = 1024;
	pinfo.yres = 600;
	pinfo.type = LCDC_PANEL;
	pinfo.pdest = DISPLAY_1;
	pinfo.wait_cycle = 0;
	pinfo.bpp = 18;
	pinfo.fb_num = 2;
	pinfo.clk_rate = 40000000;

	pinfo.lcdc.h_back_porch = 88;
	pinfo.lcdc.h_front_porch = 40;
	pinfo.lcdc.h_pulse_width = 128;
	pinfo.lcdc.v_back_porch = 23;
	pinfo.lcdc.v_front_porch = 1;
	pinfo.lcdc.v_pulse_width = 4;
	pinfo.lcdc.border_clr = 0;	/* blk */
	pinfo.lcdc.underflow_clr = 0xff;	/* blue */
	pinfo.lcdc.hsync_skew = 0;

	ret = lcdc_device_register(&pinfo);
	if (ret)
		printk(KERN_ERR "%s: failed to register device!\n", __func__);

	return ret;
}

module_init(lcdc_grapefruit_init);
