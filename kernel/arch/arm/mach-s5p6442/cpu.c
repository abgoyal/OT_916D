

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/sysdev.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/irq.h>

#include <asm/proc-fns.h>

#include <mach/hardware.h>
#include <mach/map.h>
#include <asm/irq.h>

#include <plat/regs-serial.h>
#include <mach/regs-clock.h>

#include <plat/cpu.h>
#include <plat/devs.h>
#include <plat/clock.h>
#include <plat/s5p6442.h>

/* Initial IO mappings */

static struct map_desc s5p6442_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)S5P_VA_SYSTIMER,
		.pfn		= __phys_to_pfn(S5P6442_PA_SYSTIMER),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}, {
		.virtual	= (unsigned long)VA_VIC2,
		.pfn		= __phys_to_pfn(S5P6442_PA_VIC2),
		.length		= SZ_16K,
		.type		= MT_DEVICE,
	}
};

static void s5p6442_idle(void)
{
	if (!need_resched())
		cpu_do_idle();

	local_irq_enable();
}


void __init s5p6442_map_io(void)
{
	iotable_init(s5p6442_iodesc, ARRAY_SIZE(s5p6442_iodesc));
}

void __init s5p6442_init_clocks(int xtal)
{
	printk(KERN_DEBUG "%s: initializing clocks\n", __func__);

	s3c24xx_register_baseclocks(xtal);
	s5p_register_clocks(xtal);
	s5p6442_register_clocks();
	s5p6442_setup_clocks();
}

void __init s5p6442_init_irq(void)
{
	/* S5P6442 supports 3 VIC */
	u32 vic[3];

	/* VIC0, VIC1, and VIC2: some interrupt reserved */
	vic[0] = 0x7fefffff;
	vic[1] = 0X7f389c81;
	vic[2] = 0X1bbbcfff;

	s5p_init_irq(vic, ARRAY_SIZE(vic));
}

struct sysdev_class s5p6442_sysclass = {
	.name	= "s5p6442-core",
};

static struct sys_device s5p6442_sysdev = {
	.cls	= &s5p6442_sysclass,
};

static int __init s5p6442_core_init(void)
{
	return sysdev_class_register(&s5p6442_sysclass);
}

core_initcall(s5p6442_core_init);

int __init s5p6442_init(void)
{
	printk(KERN_INFO "S5P6442: Initializing architecture\n");

	/* set idle function */
	pm_idle = s5p6442_idle;

	return sysdev_register(&s5p6442_sysdev);
}
