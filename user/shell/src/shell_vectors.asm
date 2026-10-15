BITS 16

section .shell_vectors progbits alloc exec nowrite align=1

extern shell_start
extern shell_return
extern getenv
extern setenv

global shell_vector
global shell_return_vector
global shell_getenv_vector
global shell_setenv_vector

shell_vector:
	; The task begins at offset 0000h. Jump over the reserved vector and
	; environment regions to the shell's real entry point at 0800h or above.
	jmp near shell_start

shell_return_vector:
	; Commands jump to this fixed vector at 0003h. Keep the actual context
	; restoration routine with the rest of the resident shell code.
	jmp near shell_return

shell_getenv_vector:
	; Commands call this fixed vector to use the shell's environment lookup.
	jmp near getenv

shell_setenv_vector:
	; Commands call this fixed vector to update the shell's environment.
	jmp near setenv
