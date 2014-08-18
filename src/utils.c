#include "iotop.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *xprintf(const char *format, ...)
{
    static char buf[BUFSIZ];
    va_list args;

    memset(buf, 0, BUFSIZ);
    va_start(args, format);

    int j = vsnprintf(buf, BUFSIZ, format, args);

    va_end(args);

    return ((j >= 0) && (j < BUFSIZ)) ? buf : NULL;
}

const char *read_cmdline2(int pid)
{
    static char buf[BUFSIZ];
    FILE *fp = fopen(xprintf("/proc/%d/cmdline", pid), "rb");
    char *rv = NULL;

    if (fp) {
        size_t n = fread(buf, sizeof(char), BUFSIZ, fp);
        if (n > 0) {
            size_t k;
            for (k = 0; k < n - 1; k++)
                buf[k] = buf[k] ? buf[k] : ' ';
            rv = buf;
        }
        fclose(fp);
    }

    if (rv)
        return rv;

    fp = fopen(xprintf("/proc/%d/status", pid), "rb");
    if (fp) {
        size_t n = fread(buf, sizeof(char), BUFSIZ, fp);
        char *eol = NULL;

        if (n > 0
            && (eol = strchr(buf, '\n') - 1)
            && (eol > strchr(buf, '\t'))) {
            eol[0] = 0;
            strcpy(buf, xprintf("[%s]", strchr(buf, '\t') + 1));
            rv = buf;
        }
        fclose(fp);
    }

    return rv;
}
