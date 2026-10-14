BITS 16

section .shell_vectors progbits alloc exec nowrite align=1

extern shell_start
extern shell_return

global shell_vector
global shell_return_vector

shell_vector:
	; The task begins at offset 0000h. Jump over the reserved vector and
	; environment regions to the shell's real entry point at 0800h or above.
	jmp near shell_start

shell_return_vector:
	; Commands jump to this fixed vector at 0003h. Keep the actual context
	; restoration routine with the rest of the resident shell code.
	jmp near shell_return
