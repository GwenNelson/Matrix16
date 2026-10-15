OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(basic_entry)

SECTIONS
{
	. = 0;

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

	__basic_end = .;

	ASSERT(basic_entry == 0,
	       "BASIC task entry must begin at offset zero")
	ASSERT(__basic_end <= 0xF000,
	       "BASIC code and fixed pools leave less than 4 KiB for the task stack")
}
