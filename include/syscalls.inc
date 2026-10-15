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

#define SYSCALL_CONSOLE_INVALID 0xFFFF
#define SYSCALL_INVALID_HANDLE  0xFFFF
