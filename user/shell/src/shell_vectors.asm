BITS 16

section .shell_vectors progbits alloc exec nowrite align=1

extern shell_start

global shell_vector

shell_vector:
	; The task begins at offset 0000h. Jump over the reserved vector and
	; environment regions to the shell's real entry point at 0800h or above.
	jmp near shell_start
