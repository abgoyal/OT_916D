

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <mach/hardware.h>
#include <mach/gpio.h>
#include <mach/iomux-mx3.h>

#define IOMUX_BASE	MX31_IO_ADDRESS(MX31_IOMUXC_BASE_ADDR)
#define IOMUXINT_OBS1	(IOMUX_BASE + 0x000)
#define IOMUXINT_OBS2	(IOMUX_BASE + 0x004)
#define IOMUXGPR	(IOMUX_BASE + 0x008)
#define IOMUXSW_MUX_CTL	(IOMUX_BASE + 0x00C)
#define IOMUXSW_PAD_CTL	(IOMUX_BASE + 0x154)

static DEFINE_SPINLOCK(gpio_mux_lock);

#define IOMUX_REG_MASK (IOMUX_PADNUM_MASK & ~0x3)

unsigned long mxc_pin_alloc_map[NB_PORTS * 32 / BITS_PER_LONG];
int mxc_iomux_mode(unsigned int pin_mode)
{
	u32 field, l, mode, ret = 0;
	void __iomem *reg;

	reg = IOMUXSW_MUX_CTL + (pin_mode & IOMUX_REG_MASK);
	field = pin_mode & 0x3;
	mode = (pin_mode & IOMUX_MODE_MASK) >> IOMUX_MODE_SHIFT;

	spin_lock(&gpio_mux_lock);

	l = __raw_readl(reg);
	l &= ~(0xff << (field * 8));
	l |= mode << (field * 8);
	__raw_writel(l, reg);

	spin_unlock(&gpio_mux_lock);

	return ret;
}
EXPORT_SYMBOL(mxc_iomux_mode);

void mxc_iomux_set_pad(enum iomux_pins pin, u32 config)
{
	u32 field, l;
	void __iomem *reg;

	pin &= IOMUX_PADNUM_MASK;
	reg = IOMUXSW_PAD_CTL + (pin + 2) / 3 * 4;
	field = (pin + 2) % 3;

	pr_debug("%s: reg offset = 0x%x, field = %d\n",
			__func__, (pin + 2) / 3, field);

	spin_lock(&gpio_mux_lock);

	l = __raw_readl(reg);
	l &= ~(0x1ff << (field * 10));
	l |= config << (field * 10);
	__raw_writel(l, reg);

	spin_unlock(&gpio_mux_lock);
}
EXPORT_SYMBOL(mxc_iomux_set_pad);

int mxc_iomux_alloc_pin(const unsigned int pin, const char *label)
{
	unsigned pad = pin & IOMUX_PADNUM_MASK;

	if (pad >= (PIN_MAX + 1)) {
		printk(KERN_ERR "mxc_iomux: Attempt to request nonexistant pin %u for \"%s\"\n",
			pad, label ? label : "?");
		return -EINVAL;
	}

	if (test_and_set_bit(pad, mxc_pin_alloc_map)) {
		printk(KERN_ERR "mxc_iomux: pin %u already used. Allocation for \"%s\" failed\n",
			pad, label ? label : "?");
		return -EBUSY;
	}
	mxc_iomux_mode(pin);

	return 0;
}
EXPORT_SYMBOL(mxc_iomux_alloc_pin);

int mxc_iomux_setup_multiple_pins(unsigned int *pin_list, unsigned count,
		const char *label)
{
	unsigned int *p = pin_list;
	int i;
	int ret = -EINVAL;

	for (i = 0; i < count; i++) {
		ret = mxc_iomux_alloc_pin(*p, label);
		if (ret)
			goto setup_error;
		p++;
	}
	return 0;

setup_error:
	mxc_iomux_release_multiple_pins(pin_list, i);
	return ret;
}
EXPORT_SYMBOL(mxc_iomux_setup_multiple_pins);

void mxc_iomux_release_pin(const unsigned int pin)
{
	unsigned pad = pin & IOMUX_PADNUM_MASK;

	if (pad < (PIN_MAX + 1))
		clear_bit(pad, mxc_pin_alloc_map);
}
EXPORT_SYMBOL(mxc_iomux_release_pin);

void mxc_iomux_release_multiple_pins(unsigned int *pin_list, int count)
{
	unsigned int *p = pin_list;
	int i;

	for (i = 0; i < count; i++) {
		mxc_iomux_release_pin(*p);
		p++;
	}
}
EXPORT_SYMBOL(mxc_iomux_release_multiple_pins);

void mxc_iomux_set_gpr(enum iomux_gp_func gp, bool en)
{
	u32 l;

	spin_lock(&gpio_mux_lock);
	l = __raw_readl(IOMUXGPR);
	if (en)
		l |= gp;
	else
		l &= ~gp;

	__raw_writel(l, IOMUXGPR);
	spin_unlock(&gpio_mux_lock);
}
EXPORT_SYMBOL(mxc_iomux_set_gpr);
