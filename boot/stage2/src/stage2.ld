OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(stage2_start)

/*
 * Stage 2 is loaded at 7000:0000, so all linked addresses are offsets from
 * zero within that segment. The flat binary contains initialized sections;
 * the entry stub clears the separate NOLOAD .bss range using these symbols.
 */
SECTIONS
{
    . = 0;
    __stage2_start = .;

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

    __stage2_load_end = .;

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss)
        *(.bss.*)
        *(COMMON)
        __bss_end = .;
    }

    __stage2_end = .;

    ASSERT(__stage2_load_end <= 4096,
           "stage 2 initialized image exceeds eight sectors")
    ASSERT(__stage2_end <= 0x10000,
           "stage 2 memory image exceeds its 64 KiB segment")
}
