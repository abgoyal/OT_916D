

#ifndef	_bcmsdh_h_
#define	_bcmsdh_h_

#define BCMSDH_ERROR_VAL	0x0001 /* Error */
#define BCMSDH_INFO_VAL		0x0002 /* Info */
extern const uint bcmsdh_msglevel;

#define BCMSDH_ERROR(x)
#define BCMSDH_INFO(x)

/* forward declarations */
typedef struct bcmsdh_info bcmsdh_info_t;
typedef void (*bcmsdh_cb_fn_t)(void *);

extern bcmsdh_info_t *bcmsdh_attach(osl_t *osh, void *cfghdl, void **regsva, uint irq);

/* Detach - freeup resources allocated in attach */
extern int bcmsdh_detach(osl_t *osh, void *sdh);

/* Query if SD device interrupts are enabled */
extern bool bcmsdh_intr_query(void *sdh);

/* Enable/disable SD interrupt */
extern int bcmsdh_intr_enable(void *sdh);
extern int bcmsdh_intr_disable(void *sdh);

/* Register/deregister device interrupt handler. */
extern int bcmsdh_intr_reg(void *sdh, bcmsdh_cb_fn_t fn, void *argh);
extern int bcmsdh_intr_dereg(void *sdh);

#if defined(DHD_DEBUG)
/* Query pending interrupt status from the host controller */
extern bool bcmsdh_intr_pending(void *sdh);
#endif

#ifdef BCMLXSDMMC
extern int bcmsdh_claim_host_and_lock(void *sdh);
extern int bcmsdh_release_host_and_unlock(void *sdh);
#endif /* BCMLXSDMMC */

extern int bcmsdh_devremove_reg(void *sdh, bcmsdh_cb_fn_t fn, void *argh);

extern uint8 bcmsdh_cfg_read(void *sdh, uint func, uint32 addr, int *err);
extern void bcmsdh_cfg_write(void *sdh, uint func, uint32 addr, uint8 data, int *err);

/* Read/Write 4bytes from/to cfg space */
extern uint32 bcmsdh_cfg_read_word(void *sdh, uint fnc_num, uint32 addr, int *err);
extern void bcmsdh_cfg_write_word(void *sdh, uint fnc_num, uint32 addr, uint32 data, int *err);

extern int bcmsdh_cis_read(void *sdh, uint func, uint8 *cis, uint length);

extern uint32 bcmsdh_reg_read(void *sdh, uint32 addr, uint size);
extern uint32 bcmsdh_reg_write(void *sdh, uint32 addr, uint size, uint32 data);

/* Indicate if last reg read/write failed */
extern bool bcmsdh_regfail(void *sdh);

typedef void (*bcmsdh_cmplt_fn_t)(void *handle, int status, bool sync_waiting);
extern int bcmsdh_send_buf(void *sdh, uint32 addr, uint fn, uint flags,
                           uint8 *buf, uint nbytes, void *pkt,
                           bcmsdh_cmplt_fn_t complete, void *handle);
extern int bcmsdh_recv_buf(void *sdh, uint32 addr, uint fn, uint flags,
                           uint8 *buf, uint nbytes, void *pkt,
                           bcmsdh_cmplt_fn_t complete, void *handle);

/* Flags bits */
#define SDIO_REQ_4BYTE	0x1	/* Four-byte target (backplane) width (vs. two-byte) */
#define SDIO_REQ_FIXED	0x2	/* Fixed address (FIFO) (vs. incrementing address) */
#define SDIO_REQ_ASYNC	0x4	/* Async request (vs. sync request) */

/* Pending (non-error) return code */
#define BCME_PENDING	1

extern int bcmsdh_rwdata(void *sdh, uint rw, uint32 addr, uint8 *buf, uint nbytes);

/* Issue an abort to the specified function */
extern int bcmsdh_abort(void *sdh, uint fn);

/* Start SDIO Host Controller communication */
extern int bcmsdh_start(void *sdh, int stage);

/* Stop SDIO Host Controller communication */
extern int bcmsdh_stop(void *sdh);

/* Returns the "Device ID" of target device on the SDIO bus. */
extern int bcmsdh_query_device(void *sdh);

/* Returns the number of IO functions reported by the device */
extern uint bcmsdh_query_iofnum(void *sdh);

/* Miscellaneous knob tweaker. */
extern int bcmsdh_iovar_op(void *sdh, const char *name,
                           void *params, int plen, void *arg, int len, bool set);

/* Reset and reinitialize the device */
extern int bcmsdh_reset(bcmsdh_info_t *sdh);

/* helper functions */

extern void *bcmsdh_get_sdioh(bcmsdh_info_t *sdh);

/* callback functions */
typedef struct {
	/* attach to device */
	void *(*attach)(uint16 vend_id, uint16 dev_id, uint16 bus, uint16 slot,
	                uint16 func, uint bustype, void * regsva, osl_t * osh,
	                void * param);
	/* detach from device */
	void (*detach)(void *ch);
} bcmsdh_driver_t;

/* platform specific/high level functions */
extern int bcmsdh_register(bcmsdh_driver_t *driver);
extern void bcmsdh_unregister(void);
extern bool bcmsdh_chipmatch(uint16 vendor, uint16 device);
extern void bcmsdh_device_remove(void * sdh);

#if defined(OOB_INTR_ONLY)
extern int bcmsdh_register_oob_intr(void * dhdp);
extern void bcmsdh_unregister_oob_intr(void);
extern void bcmsdh_oob_intr_set(bool enable);
#endif /* defined(OOB_INTR_ONLY) */
/* Function to pass device-status bits to DHD. */
extern uint32 bcmsdh_get_dstatus(void *sdh);

/* Function to return current window addr */
extern uint32 bcmsdh_cur_sbwad(void *sdh);

/* Function to pass chipid and rev to lower layers for controlling pr's */
extern void bcmsdh_chipinfo(void *sdh, uint32 chip, uint32 chiprev);


#endif	/* _bcmsdh_h_ */
