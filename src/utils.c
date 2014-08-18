#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

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

const char *file2str(const char *filepath)
{
    static char buf[BUFSIZ];
    FILE *fp = fopen(filepath, "r");
    char *rv = NULL;

    if (fp) {
        memset(buf, 0, BUFSIZ);
        rv = fgets(buf, BUFSIZ, fp);
        fclose(fp);
    }

    return (const char *) rv;
}

