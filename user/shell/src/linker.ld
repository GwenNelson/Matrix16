OUTPUT_FORMAT("elf32-i386")
OUTPUT_ARCH(i386)
ENTRY(shell_vector)

SECTIONS
{
	/* Fixed task-segment regions from MATFS-SPEC-NEW.md. End symbols are
	 * exclusive, so each pair also describes the size of its region. */
	__shell_init_vectors_start = 0x0000;
	__shell_init_vectors_end = 0x0400;
	__shell_command_line_start = 0x0100;
	__shell_command_line_end = 0x0200;
	__shell_environment_start = 0x0400;
	__shell_environment_end = 0x0800;
	__shell_code_start = 0x0800;
	__shell_code_end = 0x8000;
	__shell_command_start = 0x8000;
	__shell_command_end = 0xF000;
	__shell_stack_start = 0xF000;
	__shell_stack_end = 0x10000;

	/* Offset zero contains the startup jump. The return-to-shell vector is
	 * the next three-byte entry, at the fixed offset 0003h. */
	.shell_vectors 0x0000 : {
		KEEP(*(.shell_vectors))
	}

	ASSERT(shell_return_vector == 0x0003,
	       "return-to-shell vector must be at offset 0003h")
	ASSERT(SIZEOF(.shell_vectors) == 6,
	       "shell vector table must contain two three-byte near jumps")

	. = __shell_code_start;

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

	ASSERT(__shell_end <= __shell_code_end,
	       "shell code, data, or BSS extends into the command region")
}
