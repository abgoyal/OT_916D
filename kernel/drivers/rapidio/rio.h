

#include <linux/device.h>
#include <linux/list.h>
#include <linux/rio.h>

/* Functions internal to the RIO core code */

extern u32 rio_mport_get_feature(struct rio_mport *mport, int local, u16 destid,
				 u8 hopcount, int ftr);
extern u32 rio_mport_get_physefb(struct rio_mport *port, int local,
				 u16 destid, u8 hopcount);
extern u32 rio_mport_get_efb(struct rio_mport *port, int local, u16 destid,
			     u8 hopcount, u32 from);
extern int rio_create_sysfs_dev_files(struct rio_dev *rdev);
extern int rio_enum_mport(struct rio_mport *mport);
extern int rio_disc_mport(struct rio_mport *mport);
extern int rio_std_route_add_entry(struct rio_mport *mport, u16 destid,
				   u8 hopcount, u16 table, u16 route_destid,
				   u8 route_port);
extern int rio_std_route_get_entry(struct rio_mport *mport, u16 destid,
				   u8 hopcount, u16 table, u16 route_destid,
				   u8 *route_port);
extern int rio_std_route_clr_table(struct rio_mport *mport, u16 destid,
				   u8 hopcount, u16 table);
extern int rio_set_port_lockout(struct rio_dev *rdev, u32 pnum, int lock);

/* Structures internal to the RIO core code */
extern struct device_attribute rio_dev_attrs[];
extern spinlock_t rio_global_list_lock;

extern struct rio_switch_ops __start_rio_switch_ops[];
extern struct rio_switch_ops __end_rio_switch_ops[];

/* Helpers internal to the RIO core code */
#define DECLARE_RIO_SWITCH_SECTION(section, name, vid, did, init_hook) \
	static const struct rio_switch_ops __rio_switch_##name __used \
	__section(section) = { vid, did, init_hook };

#define DECLARE_RIO_SWITCH_INIT(vid, did, init_hook)		\
	DECLARE_RIO_SWITCH_SECTION(.rio_switch_ops, vid##did, \
			vid, did, init_hook)

#define RIO_GET_DID(size, x)	(size ? (x & 0xffff) : ((x & 0x00ff0000) >> 16))
#define RIO_SET_DID(size, x)	(size ? (x & 0xffff) : ((x & 0x000000ff) << 16))
