#include <stdio.h>
#include <string.h>
#include <stdarg.h>
extern int _vsnprintf(char *, int, const char *, va_list);
static int format(char *buf, int size, const char *fmt, ...)
{
    int n;
    va_list ap;
    va_start(ap, fmt); n = _vsnprintf(buf, size, fmt, ap); va_end(ap);
    return n;
}
int main(void)
{
    struct { char buf[64]; unsigned char guard[32]; } test;
    char text[4096];
    int i, n;
    memset(text, 'x', sizeof(text)); text[sizeof(text)-1] = 0;
    memset(&test, 0xa5, sizeof(test));
    n = format(test.buf, sizeof(test.buf), "%s", text);
    if (n != 63 || test.buf[63] != 0) return 1;
    for (i = 0; i < sizeof(test.guard); i++) if (test.guard[i] != 0xa5) return 2;
    format(test.buf, sizeof(test.buf), "%d %u %08lx %hd %hu", -17, 19U, 0x1234UL, -3, 65530);
    puts(test.buf);
    if (strcmp(test.buf, "-17 19 00001234 -3 65530")) return 3;
    puts("PASS: linked kernel formatter truncates long strings and formats PPC integers");
    return 0;
}
