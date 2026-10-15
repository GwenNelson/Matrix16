ENTRY(kernel_start)

/* Keep these values in sync with SEG_SYS and SEG_SYS_CAPACITY in kmemmap.h.
 * SYS addresses are offsets within SEG_SYS, whose physical base is 0x4000. */
SEG_SYS = 0x0400;
SEG_SYS_CAPACITY = 0xC000;
SEG_KERN = 0x1000;

MEMORY
{
    KERN (rwx) : ORIGIN = 0x0000, LENGTH = 0x10000
    SYS  (rw)  : ORIGIN = 0x0000, LENGTH = SEG_SYS_CAPACITY
}

SECTIONS
{
    . = 0;

    .text : {
        *(.text)
        *(.text.*)
    } > KERN

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    } > KERN

    .data : {
        *(.data)
        *(.data.*)
    } > KERN

    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    } > KERN

    /* Objects explicitly marked SEG_SYS_BSS live at these offsets in
     * SEG_SYS. This section has no load image and is cleared by kernel_start. */
    .auxbss ORIGIN(SYS) (NOLOAD) : {
        . = ALIGN(2);
        __aux_bss_start = .;
        *(.auxbss)
        *(.auxbss.*)
        __aux_bss_end = .;
    } > SYS
    __aux_bss_size = SIZEOF(.auxbss);

    __kernel_end = .;

    /* The bump heap occupies the unused space after static kernel storage.
     * Keep the final 2 KiB for the bootstrap/kernel-main stack. */
    kernel_heap_start = ALIGN(__bss_end, 2);
    kernel_heap_end = 0xF800;
    kernel_stack_start = kernel_heap_end;
    kernel_stack_end = 0x10000;
    kernel_stack_top = kernel_stack_end - 2;

    ASSERT(kernel_heap_start <= kernel_heap_end,
           "kernel static storage overlaps the reserved kernel stack")
    ASSERT(__aux_bss_end <= SEG_SYS_CAPACITY,
           "auxiliary BSS exceeds the available SEG_SYS range")
}
