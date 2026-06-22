#include "log.h"

#include <stdio.h>
#include <stdarg.h>

void dge_log(LogLevel level, const char *fmt, ...) {
    const char *prefix;
    FILE *stream;

    switch (level) {
        case LOG_INFO:  prefix = "[INFO] ";  stream = stdout; break;
        case LOG_WARN:  prefix = "[WARN] ";  stream = stderr; break;
        case LOG_ERROR: prefix = "[ERROR] "; stream = stderr; break;
        default:        prefix = "[?] ";     stream = stderr; break;
    }

    fputs(prefix, stream);

    va_list args;
    va_start(args, fmt);
    vfprintf(stream, fmt, args);
    va_end(args);

    fputc('\n', stream);
}
