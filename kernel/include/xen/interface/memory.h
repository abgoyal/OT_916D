

#ifndef __XEN_PUBLIC_MEMORY_H__
#define __XEN_PUBLIC_MEMORY_H__

#define XENMEM_increase_reservation 0
#define XENMEM_decrease_reservation 1
#define XENMEM_populate_physmap     6
struct xen_memory_reservation {

    /*
     * XENMEM_increase_reservation:
     *   OUT: MFN (*not* GMFN) bases of extents that were allocated
     * XENMEM_decrease_reservation:
     *   IN:  GMFN bases of extents to free
     * XENMEM_populate_physmap:
     *   IN:  GPFN bases of extents to populate with memory
     *   OUT: GMFN bases of extents that were allocated
     *   (NB. This command also updates the mach_to_phys translation table)
     */
    GUEST_HANDLE(ulong) extent_start;

    /* Number of extents, and size/alignment of each (2^extent_order pages). */
    unsigned long  nr_extents;
    unsigned int   extent_order;

    /*
     * Maximum # bits addressable by the user of the allocated region (e.g.,
     * I/O devices often have a 32-bit limitation even in 64-bit systems). If
     * zero then the user has no addressing restriction.
     * This field is not used by XENMEM_decrease_reservation.
     */
    unsigned int   address_bits;

    /*
     * Domain whose reservation is being changed.
     * Unprivileged domains can specify only DOMID_SELF.
     */
    domid_t        domid;

};
DEFINE_GUEST_HANDLE_STRUCT(xen_memory_reservation);

#define XENMEM_maximum_ram_page     2

#define XENMEM_current_reservation  3
#define XENMEM_maximum_reservation  4

#define XENMEM_machphys_mfn_list    5
struct xen_machphys_mfn_list {
    /*
     * Size of the 'extent_start' array. Fewer entries will be filled if the
     * machphys table is smaller than max_extents * 2MB.
     */
    unsigned int max_extents;

    /*
     * Pointer to buffer to fill with list of extent starts. If there are
     * any large discontiguities in the machine address space, 2MB gaps in
     * the machphys table will be represented by an MFN base of zero.
     */
    GUEST_HANDLE(ulong) extent_start;

    /*
     * Number of extents written to the above array. This will be smaller
     * than 'max_extents' if the machphys table is smaller than max_e * 2MB.
     */
    unsigned int nr_extents;
};
DEFINE_GUEST_HANDLE_STRUCT(xen_machphys_mfn_list);

#define XENMEM_add_to_physmap      7
struct xen_add_to_physmap {
    /* Which domain to change the mapping for. */
    domid_t domid;

    /* Source mapping space. */
#define XENMAPSPACE_shared_info 0 /* shared info page */
#define XENMAPSPACE_grant_table 1 /* grant table page */
    unsigned int space;

    /* Index into source mapping space. */
    unsigned long idx;

    /* GPFN where the source mapping page should appear. */
    unsigned long gpfn;
};
DEFINE_GUEST_HANDLE_STRUCT(xen_add_to_physmap);

#define XENMEM_translate_gpfn_list  8
struct xen_translate_gpfn_list {
    /* Which domain to translate for? */
    domid_t domid;

    /* Length of list. */
    unsigned long nr_gpfns;

    /* List of GPFNs to translate. */
    GUEST_HANDLE(ulong) gpfn_list;

    /*
     * Output list to contain MFN translations. May be the same as the input
     * list (in which case each input GPFN is overwritten with the output MFN).
     */
    GUEST_HANDLE(ulong) mfn_list;
};
DEFINE_GUEST_HANDLE_STRUCT(xen_translate_gpfn_list);

#endif /* __XEN_PUBLIC_MEMORY_H__ */
