/*
 * vmSunConst.h --
 *
 *     	Virtual memory constants for the Sun.  See the "Sun 2.0 Architecture 
 *	Manual" for a definition of these constants.
 *
 * Copyright (C) 1985 Regents of the University of California
 * All rights reserved.
 *
 *
 * $Header: /cdrom/src/kernel/Cvsroot/kernel/vm/sun3.md/vmSunConst.h,v 9.3 92/10/26 12:01:56 elm Exp $ SPRITE (Berkeley)
 */

#ifndef _VMSUN3CONST
#define _VMSUN3CONST

/*
 * Sun3 page table entry masks.
 *
 *    VMMACH_RESIDENT_BIT	The page is physical resident.
 *    VMMACH_PROTECTION_FIELD	Access to the protection bits.
 *    VMMACH_KR_PROT		Kernel read only, user no access.
 *    VMMACH_KRW_PROT		Kernel read/write, user no access.
 *    VMMACH_UR_PROT		Kernel read only, user read only
 *    VMMACH_URW_PROT		Kernel read/write, user read/write.
 *    VMMACH_DONT_CACHE_BIT	Don't cache bit.
 *    VMMACH_TYPE_FIELD		The memory type field.
 *    VMMACH_REFERENCED_BIT	Referenced bit.
 *    VMMACH_MODIFIED_BIT	Modified bit.
 *    VMMACH_PAGE_FRAME_FIELD	The hardware page frame.
 */
#define	VMMACH_RESIDENT_BIT	0x80000000
#define	VMMACH_PROTECTION_FIELD	0x60000000
#define	VMMACH_UR_PROT		0x00000000
#define	VMMACH_KR_PROT		0x20000000
#define VMMACH_URW_PROT		0x40000000
#define	VMMACH_KRW_PROT		0x60000000
#define	VMMACH_DONT_CACHE_BIT	0x10000000
#define	VMMACH_TYPE_FIELD	0x0c000000
#define VMMACH_REFERENCED_BIT	0x02000000
#define	VMMACH_MODIFIED_BIT	0x01000000
#define VMMACH_PAGE_FRAME_FIELD	0x0007ffff	

/*
 * Shift to allow the type field to get set.
 */
#define	VmMachGetPageType(pte) (((pte) >> 26) & 3)
#define	VmMachSetPageType(pte, type) (pte |= (type << 26))

/*
 * Sun memory management unit constants:
 *
 * VMMACH_KERN_CONTEXT		The Kernel's context.
 * VMMACH_NUM_CONTEXTS		The number of contexts.
 * VMMACH_NUM_SEGS_PER_CONTEXT	The number of hardware segments per context.
 * VMMACH_NUM_PAGES_PER_SEG_INT	The number of pages per hardware segment.
 * VMMACH_NUM_PAGE_MAP_ENTRIES	The number of hardware page map entries.
 * VMMACH_NUM_PMEGS		The number of page clusters that there are 
 *				in the system.
 * VMMACH_INV_PMEG   		The page cluster number that is used to 
 *				represent an invalid Pmeg.
 * VMMACH_INV_SEG   		The hardware segment number that is used to
 *				represent an invalid hardware segment.
 * VMMACH_INV_CONTEXT   	The context number that is used to represent
 *				an invalid context.
 */
#define VMMACH_KERN_CONTEXT		0
#define VMMACH_NUM_CONTEXTS		8
#define VMMACH_NUM_SEGS_PER_CONTEXT	2048
#define VMMACH_NUM_PAGES_PER_SEG_INT	16
#define VMMACH_NUM_PAGE_MAP_ENTRIES	4096
#define	VMMACH_NUM_PMEGS		(VMMACH_NUM_PAGE_MAP_ENTRIES / VMMACH_NUM_PAGES_PER_SEG_INT)

/*
 * The following sets of constants define the kernel's virtual address space. 
 * Some of the constants are defined in machineConst.h.
 *
 * VMMACH_DEV_START_ADDR        The first virtual address that is used for
 *                              mapping devices.
 * VMMACH_DEV_END_ADDR		The last virtual address that could be used for 
 *				mapping devices.
 * VMMACH_DMA_START_ADDR	The first virtual address that is used for 
 *				DMA buffers by devices.
 * VMMACH_DMA_SIZE		The amount of space devoted to DMA buffers
 * VMMACH_NET_MAP_START		The beginning of space for mapping the Intel
 *				and AMD drivers.
 * VMMACH_NET_MEM_START		The beginning of space for memory for the Intel
 *				and AMD drivers.
 * VMMACH_FIRST_SPECIAL_SEG	The first hardware segment that is used for 
 *				mapping devices.
 *
 * The Sun3 address space looks like the following:  The hex addresses are
 *	valid on most sun3s.  Those with small memories may have their
 *	vmStackBaseAddr shifted down a bit (== e3fc000 on a 3/50).
 *	vmMapBaseAddr and vmBlockCacheBaseAddr are computed relative to
 *	the stack base, which is determined by max kernel size.
 *
 *	|-------------------------------|	0x00000000
 *	| Trap vectors & Invalid Page	|
 *	|-------------------------------|	0x00004000
 *	| User Virtual Address Space	|
 *     	|				|
 *	| Top of User stack		|
 *	|-------------------------------|	0x0dfe0000 VMMACH_MAP_SEG_ADDR
 *	| 1 Segment for user mappings	|	0x0e000000 MACH_KERN_START
 *	|-------------------------------|	0x0e000000 MACH_STACK_BOTTOM
 *	| 16K for (mapped) kernel stack	|
 *	|-------------------------------|	0x0e004000 MACH_CODE_START
 *     	|				|
 *     	| Kernel Code and Data		|	(VMMACH_MAX_KERN_SIZE)
 *     	|				|
 *	---------------------------------	0x0e800000 vmStackBaseAddr
 *	|				|	
 *	| Page frames for kernel stacks |
 *	|-------------------------------|	0x0ea00000 vmMapBaseAddr
 *	| Mapping for Vm_MakeAccessible	|	
 *	---------------------------------	0x0ea20000
 *	| Mapping for PMEG and PTE	|
 *	|_______________________________|	0x0ea60000 vmBlockCacheBaseAddr
 *	| 				|
 *	| FS Block cache		|
 *	| 				|	mack_KernEnd vmBlockCacheEndAddr
 *	|-------------------------------|	0x0fd00000 VMMACH_FRAME_BUFFER
 *	| Frame buffer			|
 *      |-------------------------------|	0x0fe00000 VMMACH_DEV_START_ADDR
 *	|				|
 *	| Mapped in devices		|
 *	|				|	VMMACH_DEV_END_ADDR
 *	|-------------------------------|	0x0ff00000 VMMACH_DMA_START_ADDR
 *	|				|
 *	| Space for mapping in DMA bufs |
 *	|				|
 *	|-------------------------------|	0x0ffc0000 VMMACH_NET_MAP_START
 *	| For mapping ethernet packets	|
 *	|-------------------------------|	0x0ffe0000 VMMACH_NET_MEM_START
 *	| For mapping ethernet memory	|
 *	|-------------------------------|	0x0fffffff end-o-virtual-sun3
 */

/*
 * There is a special area of a users VAS to allow copies between two user
 * address spaces.  This area sits between the beginning of the kernel and
 * the top of the user stack.  It is one hardware segment wide.
 */
#define	VMMACH_MAP_SEG_ADDR		(MACH_KERN_START - VMMACH_SEG_SIZE)

/*
 * VMMACH_MAX_KERN_SIZE defines the maximum size of the kernel code+data.
 *	A variable is initialized to this, and it may be further reduced
 *	so that this amount is never greater than all of physical memory.
 * VMMACH_BOOT_MAP_PAGES Defines number of pages that need to be mapped
 *		in during bootstrap ("pre VM setup").
 */
#ifdef sun3
#define VMMACH_MAX_KERN_SIZE		(8192 * 1024)
#define VMMACH_BOOT_MAP_PAGES		(512)
#endif
#ifdef sun2
#define VMMACH_MAX_KERN_SIZE		(2048 * 1024)
#define VMMACH_BOOT_MAP_PAGES		(1024)
#endif


#define VMMACH_FRAME_BUFFER_ADDR	0xFD00000
#define VMMACH_KERN_END			VMMACH_FRAME_BUFFER_ADDR
#define	VMMACH_FIRST_SPECIAL_SEG	(VMMACH_KERN_END >> VMMACH_SEG_SHIFT)
#define VMMACH_DEV_START_ADDR       	0xFE00000
#define	VMMACH_DEV_END_ADDR		0xFEFFFFF
#define	VMMACH_DMA_START_ADDR		0xFF00000
#define	VMMACH_DMA_SIZE			0xC0000
#define VMMACH_NET_MAP_START		0xFFC0000
#define VMMACH_NET_MEM_START		0xFFE0000
#define	VMMACH_INV_PMEG			(VMMACH_NUM_PMEGS - 1)
#define	VMMACH_INV_SEG			VMMACH_NUM_SEGS_PER_CONTEXT
#define	VMMACH_INV_CONTEXT		VMMACH_NUM_CONTEXTS

/*
 * Function code constants for access to the different address spaces as
 * defined by the Sun and 68010 hardware.  These constants determine which
 * address space is accessed when using a "movs" instruction.
 *
 * VMMACH_USER_DATA_SPACE		User data space.
 * VMMACH_USER_PROGRAM_SPACE	User program space
 * VMMACH_MMU_SPACE			Sun-2 memory map space.
 * VMMACH_KERN_DATA_SPACE		Kernel data space.
 * VMMACH_KERN_PROGRAM_SPACE	Kernel program space.
 * VMMACH_CPU_SPACE			Cpu space.
 */

#define	VMMACH_USER_DATA_SPACE		1
#define	VMMACH_USER_PROGRAM_SPACE	2
#define	VMMACH_MMU_SPACE		3
#define	VMMACH_KERN_DATA_SPACE		5
#define	VMMACH_KERN_PROGRAM_SPACE	6
#define	VMMACH_CPU_SPACE		7

/*
 * Masks for access to different parts of mmu space.
 *
 * VMMACH_PAGE_MAP_OFF	 	Offset to hardware page map entries
 * VMMACH_SEG_MAP_OFF	 	Offset to hardware segment map entries
 * VMMACH_CONTEXT_OFF	 	Offset to context register
 * VMMACH_DIAGNOSTIC_REG	The address of the diagnostic register.
 * VMMACH_BUS_ERROR_REG		The address of the bus error register.
 * VMMACH_SYSTEM_ENABLE_REG	The address of the system enable register.
 * VMMACH_ETHER_ADDR		Address of ethernet address in the id prom.
 * VMMACH_MACH_TYPE_ADDR	Address of machine type in the id prom.
 * VMMACH_IDPROM_INC		Amount to increment an address when stepping
 *				through the id prom.
 */

#define	VMMACH_PAGE_MAP_OFF		0x10000000
#define	VMMACH_SEG_MAP_OFF		0x20000000
#define	VMMACH_CONTEXT_OFF		0x30000000
#define VMMACH_SYSTEM_ENABLE_REG	0x40000000
#define VMMACH_BUS_ERROR_REG		0x60000000
#define VMMACH_DIAGNOSTIC_REG		0x70000000
#define VMMACH_ETHER_ADDR		0x02
#define VMMACH_MACH_TYPE_ADDR		0x01
#define VMMACH_IDPROM_INC		0x01

/*
 * The highest virtual address useable by the kernel for both machine type
 * 1 and machine type 2.
 */

#define	VMMACH_MACH_TYPE1_MEM_END	0xF40000
#define	VMMACH_MACH_TYPE2_MEM_END	0x1000000
#define	VMMACH_MACH_TYPE3_MEM_END	0x10000000

/*
 * Masks to extract good bits from the virtual addresses when accessing
 * the page and segment maps.
 */

#define	VMMACH_PAGE_MAP_MASK	0xFFFFe000
#define	VMMACH_SEG_MAP_MASK		0xFFFe0000

/*
 * Mask to get only the low order three bits of a context register.
 */

#define	VMMACH_CONTEXT_MASK		0x7

/*
 * Hardware dependent constants for pages and segments:
 *
 * VMMACH_CLUSTER_SIZE      The number of hardware pages per virtual page.
 * VMMACH_CLUSTER_SHIFT     The log base 2 of VMMACH_CLUSTER_SIZE.
 * VMMACH_PAGE_SIZE		The size of each virtual page.
 * VMMACH_PAGE_SIZE_INT		The size of each hardware page.
 * VMMACH_PAGE_SHIFT		The log base 2 of VMMACH_PAGE_SIZE.
 * VMMACH_PAGE_SHIFT_INT	The log base 2 of VMMACH_PAGE_SIZE_INT
 * VMMACH_OFFSET_MASK		Mask to get to the offset of a virtual address.
 * VMMACH_OFFSET_MASK_INT	Mask to get to the offset of a virtual address.
 * VMMACH_PAGE_MASK		Mask to get to the Hardware page number within a
 *				hardware segment.
 * VMMACH_SEG_SIZE		The size of each hardware segment.
 * VMMACH_SEG_SHIFT		The log base 2 of VMMACH_SEG_SIZE.
 * VMMACH_NUM_PAGES_PER_SEG	The number of virtual pages per segment.
 */

#define VMMACH_CLUSTER_SIZE     1
#define VMMACH_CLUSTER_SHIFT    0
#define VMMACH_PAGE_SIZE        (VMMACH_CLUSTER_SIZE * VMMACH_PAGE_SIZE_INT)
#define	VMMACH_PAGE_SIZE_INT	8192
#define VMMACH_PAGE_SHIFT	(VMMACH_CLUSTER_SHIFT + VMMACH_PAGE_SHIFT_INT)
#define VMMACH_PAGE_SHIFT_INT	13
#define VMMACH_OFFSET_MASK	0x1fff
#define VMMACH_OFFSET_MASK_INT	0x1fff
#define VMMACH_PAGE_MASK	0x1E000	
#define	VMMACH_SEG_SIZE		0x20000
#define VMMACH_SEG_SHIFT	17	
#define	VMMACH_NUM_PAGES_PER_SEG (VMMACH_NUM_PAGES_PER_SEG_INT / VMMACH_CLUSTER_SIZE)

/*
 * Sun3's don't have a cache.
 * Zero values here confuse lint.
 */

#ifdef lint
#define VMMACH_CACHE_LINE_SIZE	1
#define VMMACH_NUM_CACHE_LINES	1
#define VMMACH_CACHE_SIZE 	1
#else
#define VMMACH_CACHE_LINE_SIZE	0
#define VMMACH_NUM_CACHE_LINES	0
#define VMMACH_CACHE_SIZE 	0
#endif

/*
 * The size that page tables are to be allocated in.  This grows software
 * segments in 256K chunks.  The page tables must grow in chunks that are 
 * multiples of the hardware segment size.  This is because the heap and 
 * stack segments grow towards each other in VMMACH_PAGE_TABLE_INCREMENT 
 * sized chunks.  Thus if the increment wasn't a multiple of the hardware 
 * segment size then the heap and stack segments could overlap on a 
 * hardware segment.
 */

#define	VMMACH_PAGE_TABLE_INCREMENT	(((256 * 1024 - 1) / VMMACH_SEG_SIZE + 1) * VMMACH_NUM_PAGES_PER_SEG)

/*
 * The maximum number of kernel stacks.  There is a limit because these
 * use part of the kernel's virtual address space.
 */
#define VMMACH_MAX_KERN_STACKS	128

/*
 * Definitions for shared memory.
 * VMMACH_USER_SHARED_PAGES is the number of pages allocated for shared
 *	memory.
 * VMMACH_SHARED_BLOCK_SIZE is the block size in which shared memory is
 *	allocated.  This is a multiple of 128K to avoid cache aliasing.
 * VMMACH_SHARED_START ADDR is the address at which shared memory starts.
 */
#define VMMACH_USER_SHARED_PAGES	8192
#define VMMACH_SHARED_BLOCK_SIZE	0x20000
#define VMMACH_SHARED_START_ADDR	(VMMACH_MAP_SEG_ADDR-VMMACH_USER_SHARED_PAGES*VMMACH_PAGE_SIZE)
#define	VMMACH_SHARED_NUM_BLOCKS	(VMMACH_USER_SHARED_PAGES*VMMACH_PAGE_SIZE/VMMACH_SHARED_BLOCK_SIZE)

/*
 * Definitions for supporting the XBUS memory.
 */
#define VmMachIsXbusMem(addr) 0

#endif /* _VMSUN3CONST */
