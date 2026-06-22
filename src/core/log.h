#ifndef DGE_LOG_H
#define DGE_LOG_H

typedef enum {
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} LogLevel;

void dge_log(LogLevel level, const char *fmt, ...);

#define LOG_INFO(...)  dge_log(LOG_INFO,  __VA_ARGS__)
#define LOG_WARN(...)  dge_log(LOG_WARN,  __VA_ARGS__)
#define LOG_ERROR(...) dge_log(LOG_ERROR, __VA_ARGS__)

#endif /* DGE_LOG_H */
