#include "stdio.h"
#include "syscalls.h"

#include <stdarg.h>
#include <stdint.h>

void putc(char c) {
	sys_write(1,&c,1);
}

int print_uint(uint16_t n) {
	return 0;
}

int printf(const char *fmt, ...) {
    int written=0;
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            putc(*fmt++);
	    written++;
            continue;
        }

        fmt++;  /* Skip '%' */

        switch (*fmt) {
        case 'u':
            written = written + print_uint((uint16_t)va_arg(ap, unsigned int));
            break;

        case 's': {
            const char *s = va_arg(ap, const char *);
	    while (*s) {
                putc(*s++);
		written++;
	    }
           break;
        }

        case '%':
            putc('%');
	    written++;
            break;

        case '\0':
            goto done;

        default:
            putc('%');
            putc(*fmt);
	    written++;
            break;
        }

        fmt++;
    }

done:
    va_end(ap);
    return written;
}
