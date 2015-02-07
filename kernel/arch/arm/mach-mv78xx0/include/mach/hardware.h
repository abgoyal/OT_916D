

#ifndef __ASM_ARCH_HARDWARE_H
#define __ASM_ARCH_HARDWARE_H

#include "mv78xx0.h"

#define pcibios_assign_all_busses()	1

#define PCIBIOS_MIN_IO			0x00001000
#define PCIBIOS_MIN_MEM			0x01000000
#define PCIMEM_BASE			MV78XX0_PCIE_MEM_PHYS_BASE /* mem base for VGA */


#endif
