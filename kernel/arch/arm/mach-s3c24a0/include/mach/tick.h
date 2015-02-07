

#define SUBSRC_TIMER4	(1 << (IRQ_TIMER4 - IRQ_S3CUART_RX0))

static inline int s3c24xx_ostimer_pending(void)
{
	return __raw_readl(S3C2410_SUBSRCPND) & SUBSRC_TIMER4;
}
