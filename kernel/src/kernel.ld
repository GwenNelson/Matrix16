ENTRY(kernel_start)

SECTIONS
{
    . = 0;

    .text : {
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .data : {
        *(.data)
        *(.data.*)
    }

    .bss : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

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
}
