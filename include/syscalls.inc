#pragma once

#define SYS_WRITE 1
#define SYS_READ  2
#define SYS_GET_CONSOLE 3
#define SYS_LOAD  4
#define SYS_OPEN  5
#define SYS_CLOSE 6
#define SYS_WRITE_BLOCK 7
#define SYS_READ_BLOCK  8
#define SYS_CREATE 9
#define SYS_DELETE 10
#define SYS_COPY 11
#define SYS_RENAME 12
#define SYS_GET_FREE_BLOCKS 13
#define SYS_EXEC 14
#define SYS_TERM 15
#define SYS_CLAIM_AUX 16
#define SYS_RELEASE_AUX 17

/* AUX calls take no arguments; AX returns 0 on success and 1 on failure. */
/* SYS_EXEC takes DS:BX=filename and CX=target. SYS_TERM takes BX=target. */
/* Targets are slots 0-3, or SYSCALL_TASK_SELF; slot 4 is reserved. */
#define SYSCALL_TASK_SELF 0x00FF
#define SYSCALL_TASK_SLOT_COUNT 4

#define SYSCALL_CONSOLE_INVALID 0xFFFF
#define SYSCALL_INVALID_HANDLE  0xFFFF
