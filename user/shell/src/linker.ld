OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(shell_start)

SECTIONS
{
	. = 0;

	/* taskgen starts a new task at offset zero. */
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

	__shell_load_end = .;

	.bss (NOLOAD) : {
		__bss_start = .;
		*(.bss)
		*(.bss.*)
		*(COMMON)
		__bss_end = .;
	}

	__shell_end = .;

	/* SP begins at FFFEh and grows down. Keep at least 4 KiB free for it. */
	ASSERT(__shell_end <= 0xF000,
	       "shell image leaves less than 4 KiB for its stack")
}
