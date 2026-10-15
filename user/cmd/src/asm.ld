OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(asm_entry)

SECTIONS
{
	. = 0x8000;

	.text : {
		KEEP(*(.text.entry))
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

	.bss (NOLOAD) : {
		__bss_start = .;
		*(.bss)
		*(.bss.*)
		*(COMMON)
		__bss_end = .;
	}

	__asm_end = .;

	ASSERT(asm_entry == 0x8000,
	       "ASM.CMD entry must begin at offset 8000h")
	ASSERT(__asm_end <= 0xF000,
	       "ASM.CMD and its static data extend beyond the command region")
}
