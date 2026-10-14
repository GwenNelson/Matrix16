#include "stdio.h"
#include "syscalls.h"

#include <stdarg.h>
#include <stdint.h>

void putc(char c) {
	sys_write(1,&c,1);
}

int print_uint(uint16_t n) {
	char digits[5];
	uint8_t length = 0;
	uint8_t i;

	do {
		digits[length++] = '0' + n % 10;
		n /= 10;
	} while(n != 0);

	for(i = length; i != 0; i--) putc(digits[i - 1]);
	return length;
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
