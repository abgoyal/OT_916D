

/* #define VERBOSE_DEBUG */

#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/usb/android_composite.h>

#include "u_serial.h"
#include "gadget_chips.h"



struct acm_ep_descs {
	struct usb_endpoint_descriptor	*in;
	struct usb_endpoint_descriptor	*out;
	struct usb_endpoint_descriptor	*notify;
};

struct f_acm {
	struct gserial			port;
	u8				ctrl_id, data_id;
	u8				port_num;

	u8				pending;

	/* lock is mostly for pending and notify_req ... they get accessed
	 * by callbacks both from tty (open/close/break) under its spinlock,
	 * and notify_req.complete() which can't use that lock.
	 */
	spinlock_t			lock;

	struct acm_ep_descs		fs;
	struct acm_ep_descs		hs;

	struct usb_ep			*notify;
	struct usb_endpoint_descriptor	*notify_desc;
	struct usb_request		*notify_req;

	struct usb_cdc_line_coding	port_line_coding;	/* 8-N-1 etc */

	/* SetControlLineState request -- CDC 1.1 section 6.2.14 (INPUT) */
	u16				port_handshake_bits;
#define ACM_CTRL_RTS	(1 << 1)	/* unused with full duplex */
#define ACM_CTRL_DTR	(1 << 0)	/* host is ready for data r/w */

	/* SerialState notification -- CDC 1.1 section 6.3.5 (OUTPUT) */
	u16				serial_state;
#define ACM_CTRL_OVERRUN	(1 << 6)
#define ACM_CTRL_PARITY		(1 << 5)
#define ACM_CTRL_FRAMING	(1 << 4)
#define ACM_CTRL_RI		(1 << 3)
#define ACM_CTRL_BRK		(1 << 2)
#define ACM_CTRL_DSR		(1 << 1)
#define ACM_CTRL_DCD		(1 << 0)
};

static inline struct f_acm *func_to_acm(struct usb_function *f)
{
	return container_of(f, struct f_acm, port.func);
}

static inline struct f_acm *port_to_acm(struct gserial *p)
{
	return container_of(p, struct f_acm, port);
}

/*-------------------------------------------------------------------------*/

/* notification endpoint uses smallish and infrequent fixed-size messages */

#define GS_LOG2_NOTIFY_INTERVAL		5	/* 1 << 5 == 32 msec */
#define GS_NOTIFY_MAXPACKET		10	/* notification + 2 bytes */

/* interface and class descriptors: */

static struct usb_interface_assoc_descriptor
acm_iad_descriptor = {
	.bLength =		sizeof acm_iad_descriptor,
	.bDescriptorType =	USB_DT_INTERFACE_ASSOCIATION,

	/* .bFirstInterface =	DYNAMIC, */
	.bInterfaceCount = 	2,	// control + data
	.bFunctionClass =	USB_CLASS_COMM,
	.bFunctionSubClass =	USB_CDC_SUBCLASS_ACM,
	.bFunctionProtocol =	USB_CDC_PROTO_NONE,
	/* .iFunction =		DYNAMIC */
};


static struct usb_interface_descriptor acm_control_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	/* .bInterfaceNumber = DYNAMIC */
	.bNumEndpoints =	1,
	.bInterfaceClass =	USB_CLASS_COMM,
	.bInterfaceSubClass =	USB_CDC_SUBCLASS_ACM,
	.bInterfaceProtocol =	USB_CDC_ACM_PROTO_AT_V25TER,
	/* .iInterface = DYNAMIC */
};

static struct usb_interface_descriptor acm_data_interface_desc = {
	.bLength =		USB_DT_INTERFACE_SIZE,
	.bDescriptorType =	USB_DT_INTERFACE,
	/* .bInterfaceNumber = DYNAMIC */
	.bNumEndpoints =	2,
	.bInterfaceClass =	USB_CLASS_CDC_DATA,
	.bInterfaceSubClass =	0,
	.bInterfaceProtocol =	0,
	/* .iInterface = DYNAMIC */
};

static struct usb_cdc_header_desc acm_header_desc = {
	.bLength =		sizeof(acm_header_desc),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_HEADER_TYPE,
	.bcdCDC =		cpu_to_le16(0x0110),
};

static struct usb_cdc_call_mgmt_descriptor
acm_call_mgmt_descriptor = {
	.bLength =		sizeof(acm_call_mgmt_descriptor),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_CALL_MANAGEMENT_TYPE,
	.bmCapabilities =	0,
	/* .bDataInterface = DYNAMIC */
};

static struct usb_cdc_acm_descriptor acm_descriptor = {
	.bLength =		sizeof(acm_descriptor),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_ACM_TYPE,
	.bmCapabilities =	USB_CDC_CAP_LINE,
};

static struct usb_cdc_union_desc acm_union_desc = {
	.bLength =		sizeof(acm_union_desc),
	.bDescriptorType =	USB_DT_CS_INTERFACE,
	.bDescriptorSubType =	USB_CDC_UNION_TYPE,
	/* .bMasterInterface0 =	DYNAMIC */
	/* .bSlaveInterface0 =	DYNAMIC */
};

/* full speed support: */

static struct usb_endpoint_descriptor acm_fs_notify_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	cpu_to_le16(GS_NOTIFY_MAXPACKET),
	.bInterval =		1 << GS_LOG2_NOTIFY_INTERVAL,
};

static struct usb_endpoint_descriptor acm_fs_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor acm_fs_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_OUT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *acm_fs_function[] = {
	(struct usb_descriptor_header *) &acm_iad_descriptor,
	(struct usb_descriptor_header *) &acm_control_interface_desc,
	(struct usb_descriptor_header *) &acm_header_desc,
	(struct usb_descriptor_header *) &acm_call_mgmt_descriptor,
	(struct usb_descriptor_header *) &acm_descriptor,
	(struct usb_descriptor_header *) &acm_union_desc,
	(struct usb_descriptor_header *) &acm_fs_notify_desc,
	(struct usb_descriptor_header *) &acm_data_interface_desc,
	(struct usb_descriptor_header *) &acm_fs_in_desc,
	(struct usb_descriptor_header *) &acm_fs_out_desc,
	NULL,
};

/* high speed support: */

static struct usb_endpoint_descriptor acm_hs_notify_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bEndpointAddress =	USB_DIR_IN,
	.bmAttributes =		USB_ENDPOINT_XFER_INT,
	.wMaxPacketSize =	cpu_to_le16(GS_NOTIFY_MAXPACKET),
	.bInterval =		GS_LOG2_NOTIFY_INTERVAL+4,
};

static struct usb_endpoint_descriptor acm_hs_in_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_endpoint_descriptor acm_hs_out_desc = {
	.bLength =		USB_DT_ENDPOINT_SIZE,
	.bDescriptorType =	USB_DT_ENDPOINT,
	.bmAttributes =		USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =	cpu_to_le16(512),
};

static struct usb_descriptor_header *acm_hs_function[] = {
	(struct usb_descriptor_header *) &acm_iad_descriptor,
	(struct usb_descriptor_header *) &acm_control_interface_desc,
	(struct usb_descriptor_header *) &acm_header_desc,
	(struct usb_descriptor_header *) &acm_call_mgmt_descriptor,
	(struct usb_descriptor_header *) &acm_descriptor,
	(struct usb_descriptor_header *) &acm_union_desc,
	(struct usb_descriptor_header *) &acm_hs_notify_desc,
	(struct usb_descriptor_header *) &acm_data_interface_desc,
	(struct usb_descriptor_header *) &acm_hs_in_desc,
	(struct usb_descriptor_header *) &acm_hs_out_desc,
	NULL,
};

/* string descriptors: */

#define ACM_CTRL_IDX	0
#define ACM_DATA_IDX	1
#define ACM_IAD_IDX	2

/* static strings, in UTF-8 */
static struct usb_string acm_string_defs[] = {
	[ACM_CTRL_IDX].s = "CDC Abstract Control Model (ACM)",
	[ACM_DATA_IDX].s = "CDC ACM Data",
	[ACM_IAD_IDX ].s = "CDC Serial",
	{  /* ZEROES END LIST */ },
};

static struct usb_gadget_strings acm_string_table = {
	.language =		0x0409,	/* en-us */
	.strings =		acm_string_defs,
};

static struct usb_gadget_strings *acm_strings[] = {
	&acm_string_table,
	NULL,
};

/*-------------------------------------------------------------------------*/


static void acm_complete_set_line_coding(struct usb_ep *ep,
		struct usb_request *req)
{
	struct f_acm	*acm = ep->driver_data;
	struct usb_composite_dev *cdev = acm->port.func.config->cdev;

	if (req->status != 0) {
		DBG(cdev, "acm ttyGS%d completion, err %d\n",
				acm->port_num, req->status);
		return;
	}

	/* normal completion */
	if (req->actual != sizeof(acm->port_line_coding)) {
		DBG(cdev, "acm ttyGS%d short resp, len %d\n",
				acm->port_num, req->actual);
		usb_ep_set_halt(ep);
	} else {
		struct usb_cdc_line_coding	*value = req->buf;

		/* REVISIT:  we currently just remember this data.
		 * If we change that, (a) validate it first, then
		 * (b) update whatever hardware needs updating,
		 * (c) worry about locking.  This is information on
		 * the order of 9600-8-N-1 ... most of which means
		 * nothing unless we control a real RS232 line.
		 */
		acm->port_line_coding = *value;
	}
}

static int acm_setup(struct usb_function *f, const struct usb_ctrlrequest *ctrl)
{
	struct f_acm		*acm = func_to_acm(f);
	struct usb_composite_dev *cdev = f->config->cdev;
	struct usb_request	*req = cdev->req;
	int			value = -EOPNOTSUPP;
	u16			w_index = le16_to_cpu(ctrl->wIndex);
	u16			w_value = le16_to_cpu(ctrl->wValue);
	u16			w_length = le16_to_cpu(ctrl->wLength);

	/* composite driver infrastructure handles everything except
	 * CDC class messages; interface activation uses set_alt().
	 *
	 * Note CDC spec table 4 lists the ACM request profile.  It requires
	 * encapsulated command support ... we don't handle any, and respond
	 * to them by stalling.  Options include get/set/clear comm features
	 * (not that useful) and SEND_BREAK.
	 */
	switch ((ctrl->bRequestType << 8) | ctrl->bRequest) {

	/* SET_LINE_CODING ... just read and save what the host sends */
	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_CDC_REQ_SET_LINE_CODING:
		if (w_length != sizeof(struct usb_cdc_line_coding)
				|| w_index != acm->ctrl_id)
			goto invalid;

		value = w_length;
		cdev->gadget->ep0->driver_data = acm;
		req->complete = acm_complete_set_line_coding;
		break;

	/* GET_LINE_CODING ... return what host sent, or initial value */
	case ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_CDC_REQ_GET_LINE_CODING:
		if (w_index != acm->ctrl_id)
			goto invalid;

		value = min_t(unsigned, w_length,
				sizeof(struct usb_cdc_line_coding));
		memcpy(req->buf, &acm->port_line_coding, value);
		break;

	/* SET_CONTROL_LINE_STATE ... save what the host sent */
	case ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
			| USB_CDC_REQ_SET_CONTROL_LINE_STATE:
		if (w_index != acm->ctrl_id)
			goto invalid;

		value = 0;

		/* FIXME we should not allow data to flow until the
		 * host sets the ACM_CTRL_DTR bit; and when it clears
		 * that bit, we should return to that no-flow state.
		 */
		acm->port_handshake_bits = w_value;
		break;

	default:
invalid:
		VDBG(cdev, "invalid control req%02x.%02x v%04x i%04x l%d\n",
			ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
	}

	/* respond with data transfer or status phase? */
	if (value >= 0) {
		DBG(cdev, "acm ttyGS%d req%02x.%02x v%04x i%04x l%d\n",
			acm->port_num, ctrl->bRequestType, ctrl->bRequest,
			w_value, w_index, w_length);
		req->zero = 0;
		req->length = value;
		value = usb_ep_queue(cdev->gadget->ep0, req, GFP_ATOMIC);
		if (value < 0)
			ERROR(cdev, "acm response on ttyGS%d, err %d\n",
					acm->port_num, value);
	}

	/* device either stalls (value < 0) or reports success */
	return value;
}

static int acm_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct f_acm		*acm = func_to_acm(f);
	struct usb_composite_dev *cdev = f->config->cdev;

	/* we know alt == 0, so this is an activation or a reset */

	if (intf == acm->ctrl_id) {
		if (acm->notify->driver_data) {
			VDBG(cdev, "reset acm control interface %d\n", intf);
			usb_ep_disable(acm->notify);
		} else {
			VDBG(cdev, "init acm ctrl interface %d\n", intf);
		}
		acm->notify_desc = ep_choose(cdev->gadget,
				acm->hs.notify,
				acm->fs.notify);
		usb_ep_enable(acm->notify, acm->notify_desc);
		acm->notify->driver_data = acm;

	} else if (intf == acm->data_id) {
		if (acm->port.in->driver_data) {
			DBG(cdev, "reset acm ttyGS%d\n", acm->port_num);
			gserial_disconnect(&acm->port);
		} else {
			DBG(cdev, "activate acm ttyGS%d\n", acm->port_num);
		}
		acm->port.in_desc = ep_choose(cdev->gadget,
				acm->hs.in, acm->fs.in);
		acm->port.out_desc = ep_choose(cdev->gadget,
				acm->hs.out, acm->fs.out);
		gserial_connect(&acm->port, acm->port_num);

	} else
		return -EINVAL;

	return 0;
}

static void acm_disable(struct usb_function *f)
{
	struct f_acm	*acm = func_to_acm(f);
	struct usb_composite_dev *cdev = f->config->cdev;

	DBG(cdev, "acm ttyGS%d deactivated\n", acm->port_num);
	gserial_disconnect(&acm->port);
	usb_ep_disable(acm->notify);
	acm->notify->driver_data = NULL;
}

#ifdef CONFIG_USB_DYNAMIC_INTERFACE
static int acm_update_intf_num(struct usb_function *f, int nxt_intf_num)
{
    struct usb_cdc_cs_desc {
    	__u8	bLength;
    	__u8	bDescriptorType;
    	__u8	bDescriptorSubType;
    } __attribute__ ((packed));    
	struct f_acm	*acm = func_to_acm(f);
	struct usb_descriptor_header **descriptors;
	struct usb_descriptor_header *descriptor;
    struct usb_interface_descriptor *intf, *ctrl = NULL, *data = NULL;
    struct usb_interface_assoc_descriptor *acm_iad = NULL;
    struct usb_cdc_union_desc *acm_union = NULL;
    struct usb_cdc_call_mgmt_descriptor *acm_call = NULL;
    struct usb_cdc_cs_desc *cs_desc;
    struct usb_descriptor_header **desc_list[] = {f->descriptors, f->hs_descriptors};
    int idx;

	acm->ctrl_id = nxt_intf_num++;
	acm->data_id = nxt_intf_num++;

    for (idx = 0; idx < ARRAY_SIZE(desc_list); idx++) {
        if (!desc_list[idx])
            continue;
        else
            descriptors = desc_list[idx];
    	while ((descriptor = *descriptors++) != NULL) {
            intf = (struct usb_interface_descriptor*)descriptor;
            if (intf->bDescriptorType == USB_DT_INTERFACE_ASSOCIATION) {
                acm_iad = (struct usb_interface_assoc_descriptor*)intf;
            } else if (intf->bDescriptorType == USB_DT_INTERFACE) {
                if (intf->bInterfaceClass == USB_CLASS_COMM)
                    ctrl = intf;
                else if (intf->bInterfaceClass == USB_CLASS_CDC_DATA)
                    data = intf;
            } else if (intf->bDescriptorType == USB_DT_CS_INTERFACE) {
                cs_desc = (struct usb_cdc_cs_desc*)intf;
                if (cs_desc->bDescriptorSubType == USB_CDC_UNION_TYPE)
                    acm_union = (struct usb_cdc_union_desc*)cs_desc;
                else if (cs_desc->bDescriptorSubType == USB_CDC_CALL_MANAGEMENT_TYPE)
                    acm_call = (struct usb_cdc_call_mgmt_descriptor*)cs_desc;
            }
    	}
        ctrl->bInterfaceNumber = acm->ctrl_id;
        acm_iad->bFirstInterface = acm->ctrl_id;
        acm_union->bMasterInterface0 = acm->ctrl_id;
        
        data->bInterfaceNumber = acm->data_id;
        acm_union->bSlaveInterface0 = acm->data_id;
        acm_call->bDataInterface = acm->data_id;
    }

    printk("%s: %d %d\n", __func__, acm->ctrl_id, acm->data_id);
    return nxt_intf_num;    
}
#endif  /*CONFIG_USB_DYNAMIC_INTERFACE*/
/*-------------------------------------------------------------------------*/

static int acm_cdc_notify(struct f_acm *acm, u8 type, u16 value,
		void *data, unsigned length)
{
	struct usb_ep			*ep = acm->notify;
	struct usb_request		*req;
	struct usb_cdc_notification	*notify;
	const unsigned			len = sizeof(*notify) + length;
	void				*buf;
	int				status;

	req = acm->notify_req;
	acm->notify_req = NULL;
	acm->pending = false;

	req->length = len;
	notify = req->buf;
	buf = notify + 1;

	notify->bmRequestType = USB_DIR_IN | USB_TYPE_CLASS
			| USB_RECIP_INTERFACE;
	notify->bNotificationType = type;
	notify->wValue = cpu_to_le16(value);
	notify->wIndex = cpu_to_le16(acm->ctrl_id);
	notify->wLength = cpu_to_le16(length);
	memcpy(buf, data, length);

	/* ep_queue() can complete immediately if it fills the fifo... */
	spin_unlock(&acm->lock);
	status = usb_ep_queue(ep, req, GFP_ATOMIC);
	spin_lock(&acm->lock);

	if (status < 0) {
		ERROR(acm->port.func.config->cdev,
				"acm ttyGS%d can't notify serial state, %d\n",
				acm->port_num, status);
		acm->notify_req = req;
	}

	return status;
}

static int acm_notify_serial_state(struct f_acm *acm)
{
	struct usb_composite_dev *cdev = acm->port.func.config->cdev;
	int			status;

	spin_lock(&acm->lock);
	if (acm->notify_req) {
		DBG(cdev, "acm ttyGS%d serial state %04x\n",
				acm->port_num, acm->serial_state);
		status = acm_cdc_notify(acm, USB_CDC_NOTIFY_SERIAL_STATE,
				0, &acm->serial_state, sizeof(acm->serial_state));
	} else {
		acm->pending = true;
		status = 0;
	}
	spin_unlock(&acm->lock);
	return status;
}

static void acm_cdc_notify_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_acm		*acm = req->context;
	u8			doit = false;

	/* on this call path we do NOT hold the port spinlock,
	 * which is why ACM needs its own spinlock
	 */
	spin_lock(&acm->lock);
	if (req->status != -ESHUTDOWN)
		doit = acm->pending;
	acm->notify_req = req;
	spin_unlock(&acm->lock);

	if (doit)
		acm_notify_serial_state(acm);
}

/* connect == the TTY link is open */

static void acm_connect(struct gserial *port)
{
	struct f_acm		*acm = port_to_acm(port);

	acm->serial_state |= ACM_CTRL_DSR | ACM_CTRL_DCD;
	acm_notify_serial_state(acm);
}

static void acm_disconnect(struct gserial *port)
{
	struct f_acm		*acm = port_to_acm(port);

	acm->serial_state &= ~(ACM_CTRL_DSR | ACM_CTRL_DCD);
	acm_notify_serial_state(acm);
}

static int acm_send_break(struct gserial *port, int duration)
{
	struct f_acm		*acm = port_to_acm(port);
	u16			state;

	state = acm->serial_state;
	state &= ~ACM_CTRL_BRK;
	if (duration)
		state |= ACM_CTRL_BRK;

	acm->serial_state = state;
	return acm_notify_serial_state(acm);
}

/*-------------------------------------------------------------------------*/

/* ACM function driver setup/binding */
static int
acm_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct f_acm		*acm = func_to_acm(f);
	int			status;
	struct usb_ep		*ep;

	/* allocate instance-specific interface IDs, and patch descriptors */
	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	acm->ctrl_id = status;
	acm_iad_descriptor.bFirstInterface = status;

	acm_control_interface_desc.bInterfaceNumber = status;
	acm_union_desc .bMasterInterface0 = status;

	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	acm->data_id = status;

	acm_data_interface_desc.bInterfaceNumber = status;
	acm_union_desc.bSlaveInterface0 = status;
	acm_call_mgmt_descriptor.bDataInterface = status;

	status = -ENODEV;

	/* allocate instance-specific endpoints */
	ep = usb_ep_autoconfig(cdev->gadget, &acm_fs_in_desc);
	if (!ep)
		goto fail;
	acm->port.in = ep;
	ep->driver_data = cdev;	/* claim */

	ep = usb_ep_autoconfig(cdev->gadget, &acm_fs_out_desc);
	if (!ep)
		goto fail;
	acm->port.out = ep;
	ep->driver_data = cdev;	/* claim */

	ep = usb_ep_autoconfig(cdev->gadget, &acm_fs_notify_desc);
	if (!ep)
		goto fail;
	acm->notify = ep;
	ep->driver_data = cdev;	/* claim */

	/* allocate notification */
	acm->notify_req = gs_alloc_req(ep,
			sizeof(struct usb_cdc_notification) + 2,
			GFP_KERNEL);
	if (!acm->notify_req)
		goto fail;

	acm->notify_req->complete = acm_cdc_notify_complete;
	acm->notify_req->context = acm;

	/* copy descriptors, and track endpoint copies */
	f->descriptors = usb_copy_descriptors(acm_fs_function);
	if (!f->descriptors)
		goto fail;

	acm->fs.in = usb_find_endpoint(acm_fs_function,
			f->descriptors, &acm_fs_in_desc);
	acm->fs.out = usb_find_endpoint(acm_fs_function,
			f->descriptors, &acm_fs_out_desc);
	acm->fs.notify = usb_find_endpoint(acm_fs_function,
			f->descriptors, &acm_fs_notify_desc);

	/* support all relevant hardware speeds... we expect that when
	 * hardware is dual speed, all bulk-capable endpoints work at
	 * both speeds
	 */
	if (gadget_is_dualspeed(c->cdev->gadget)) {
		acm_hs_in_desc.bEndpointAddress =
				acm_fs_in_desc.bEndpointAddress;
		acm_hs_out_desc.bEndpointAddress =
				acm_fs_out_desc.bEndpointAddress;
		acm_hs_notify_desc.bEndpointAddress =
				acm_fs_notify_desc.bEndpointAddress;

		/* copy descriptors, and track endpoint copies */
		f->hs_descriptors = usb_copy_descriptors(acm_hs_function);

		acm->hs.in = usb_find_endpoint(acm_hs_function,
				f->hs_descriptors, &acm_hs_in_desc);
		acm->hs.out = usb_find_endpoint(acm_hs_function,
				f->hs_descriptors, &acm_hs_out_desc);
		acm->hs.notify = usb_find_endpoint(acm_hs_function,
				f->hs_descriptors, &acm_hs_notify_desc);
	}

	DBG(cdev, "acm ttyGS%d: %s speed IN/%s OUT/%s NOTIFY/%s\n",
			acm->port_num,
			gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full",
			acm->port.in->name, acm->port.out->name,
			acm->notify->name);
	return 0;

fail:
	if (acm->notify_req)
		gs_free_req(acm->notify, acm->notify_req);

	/* we might as well release our claims on endpoints */
	if (acm->notify)
		acm->notify->driver_data = NULL;
	if (acm->port.out)
		acm->port.out->driver_data = NULL;
	if (acm->port.in)
		acm->port.in->driver_data = NULL;

	ERROR(cdev, "%s/%p: can't bind, err %d\n", f->name, f, status);

	return status;
}

static void
acm_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct f_acm		*acm = func_to_acm(f);

	if (gadget_is_dualspeed(c->cdev->gadget))
		usb_free_descriptors(f->hs_descriptors);
	usb_free_descriptors(f->descriptors);
	gs_free_req(acm->notify, acm->notify_req);
	kfree(acm);
}

/* Some controllers can't support CDC ACM ... */
static inline bool can_support_cdc(struct usb_configuration *c)
{
	/* everything else is *probably* fine ... */
	return true;
}

int acm_bind_config(struct usb_configuration *c, u8 port_num)
{
	struct f_acm	*acm;
	int		status;

	if (!can_support_cdc(c))
		return -EINVAL;

	/* REVISIT might want instance-specific strings to help
	 * distinguish instances ...
	 */

	/* maybe allocate device-global string IDs, and patch descriptors */
	if (acm_string_defs[ACM_CTRL_IDX].id == 0) {
		status = usb_string_id(c->cdev);
		if (status < 0)
			return status;
		acm_string_defs[ACM_CTRL_IDX].id = status;

		acm_control_interface_desc.iInterface = status;

		status = usb_string_id(c->cdev);
		if (status < 0)
			return status;
		acm_string_defs[ACM_DATA_IDX].id = status;

		acm_data_interface_desc.iInterface = status;

		status = usb_string_id(c->cdev);
		if (status < 0)
			return status;
		acm_string_defs[ACM_IAD_IDX].id = status;

		acm_iad_descriptor.iFunction = status;
	}

	/* allocate and initialize one new instance */
	acm = kzalloc(sizeof *acm, GFP_KERNEL);
	if (!acm)
		return -ENOMEM;

	spin_lock_init(&acm->lock);

	acm->port_num = port_num;

	acm->port.connect = acm_connect;
	acm->port.disconnect = acm_disconnect;
	acm->port.send_break = acm_send_break;

	acm->port.func.name = "acm";
	acm->port.func.strings = acm_strings;
	/* descriptors are per-instance copies */
	acm->port.func.bind = acm_bind;
	acm->port.func.unbind = acm_unbind;
	acm->port.func.set_alt = acm_set_alt;
	acm->port.func.setup = acm_setup;
	acm->port.func.disable = acm_disable;
#ifdef CONFIG_USB_DYNAMIC_INTERFACE
    acm->port.func.update_intf_num = acm_update_intf_num;
#endif

    /* start disabled */
    acm->port.func.disabled = 1;

	status = usb_add_function(c, &acm->port.func);
	if (status)
		kfree(acm);
	return status;
}

#ifdef CONFIG_USB_ANDROID_ACM

int acm_function_bind_config(struct usb_configuration *c)
{
	int ret = acm_bind_config(c, 0);
	if (ret == 0)
		gserial_setup(c->cdev->gadget, 1);
	return ret;
}

static struct android_usb_function acm_function = {
	.name = "acm",
	.bind_config = acm_function_bind_config,
};

static int __init init(void)
{
	printk(KERN_INFO "f_acm init\n");
	android_register_function(&acm_function);
	return 0;
}
//module_init(init);
late_initcall(init);

#endif /* CONFIG_USB_ANDROID_ACM */
