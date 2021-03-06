

#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/pm.h>

#include <asm/reboot.h>
#include <asm/mach-au1x00/au1000.h>

#include <prom.h>

static void xxs1500_reset(char *c)
{
	/* Hit BCSR.SYSTEM_CONTROL[SW_RST] */
	au_writel(0x00000000, 0xAE00001C);
}

static void xxs1500_power_off(void)
{
	printk(KERN_ALERT "It's now safe to remove power\n");
	while (1)
		asm volatile (".set mips3 ; wait ; .set mips1");
}

void __init board_setup(void)
{
	u32 pin_func;

	pm_power_off = xxs1500_power_off;
	_machine_halt = xxs1500_power_off;
	_machine_restart = xxs1500_reset;

	alchemy_gpio1_input_enable();
	alchemy_gpio2_enable();

	/* Set multiple use pins (UART3/GPIO) to UART (it's used as UART too) */
	pin_func  = au_readl(SYS_PINFUNC) & ~SYS_PF_UR3;
	pin_func |= SYS_PF_UR3;
	au_writel(pin_func, SYS_PINFUNC);

	/* Enable UART */
	au_writel(0x01, UART3_ADDR + UART_MOD_CNTRL); /* clock enable (CE) */
	mdelay(10);
	au_writel(0x03, UART3_ADDR + UART_MOD_CNTRL); /* CE and "enable" */
	mdelay(10);

	/* Enable DTR = USB power up */
	au_writel(0x01, UART3_ADDR + UART_MCR); /* UART_MCR_DTR is 0x01??? */

#ifdef CONFIG_PCI
#if defined(__MIPSEB__)
	au_writel(0xf | (2 << 6) | (1 << 4), Au1500_PCI_CFG);
#else
	au_writel(0xf, Au1500_PCI_CFG);
#endif
#endif
}

static int __init xxs1500_init_irq(void)
{
	set_irq_type(AU1500_GPIO204_INT, IRQF_TRIGGER_HIGH);
	set_irq_type(AU1500_GPIO201_INT, IRQF_TRIGGER_LOW);
	set_irq_type(AU1500_GPIO202_INT, IRQF_TRIGGER_LOW);
	set_irq_type(AU1500_GPIO203_INT, IRQF_TRIGGER_LOW);
	set_irq_type(AU1500_GPIO205_INT, IRQF_TRIGGER_LOW);
	set_irq_type(AU1500_GPIO207_INT, IRQF_TRIGGER_LOW);

	set_irq_type(AU1500_GPIO0_INT, IRQF_TRIGGER_LOW);
	set_irq_type(AU1500_GPIO1_INT, IRQF_TRIGGER_LOW);
	set_irq_type(AU1500_GPIO2_INT, IRQF_TRIGGER_LOW);
	set_irq_type(AU1500_GPIO3_INT, IRQF_TRIGGER_LOW);
	set_irq_type(AU1500_GPIO4_INT, IRQF_TRIGGER_LOW); /* CF irq */
	set_irq_type(AU1500_GPIO5_INT, IRQF_TRIGGER_LOW);

	return 0;
}
arch_initcall(xxs1500_init_irq);
