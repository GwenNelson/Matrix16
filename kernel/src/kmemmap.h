#pragma once

#define SEG_SYS       0x0400 /* Physical base 0x4000: clears the first 16 KiB. */
#define SEG_SYS_PHYS_BASE 0x4000
#define SEG_SYS_CAPACITY  0xC000 /* Offsets 0x0000-0xBFFF end before SEG_KERN. */
/* Mark zero-initialized objects for .auxbss. Access them through a far pointer
 * using SEG_SYS and the object's linker offset; normal C accesses use DS. */
#define SEG_SYS_BSS __attribute__((section(".auxbss")))
#define SEG_KERN      0x1000
#define SEG_USER0     0x2000
#define SEG_USER1     0x3000
#define SEG_USER2     0x4000
#define SEG_USER3     0x5000
#define SEG_RESV_USER 0x6000
#define SEG_AUX       0x7000
