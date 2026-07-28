#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define sphaira_USE_LOG 1

#include <stdarg.h>

#if sphaira_USE_LOG
bool log_file_init();
bool log_nxlink_init();
void log_file_exit();
bool log_is_init();

void log_nxlink_exit();
void log_write(const char* s, ...) __attribute__ ((format (printf, 1, 2)));
void log_write_arg(const char* s, va_list* v);

// appends to a dedicated error log that is written regardless of whether normal
// logging was enabled -- a failed install must always leave a trace. Rare by
// nature (once per failure), so it does its own blocking write and must never
// be called from a hot path. Also mirrored into the normal log when that is on.
void log_write_error(const char* s, ...) __attribute__ ((format (printf, 1, 2)));
#else
inline bool log_file_init() {
    return true;
}
inline bool log_nxlink_init() {
    return true;
}
#define log_file_exit()
#define log_nxlink_exit()
#define log_write(...)
#define log_write_arg(...)
#define log_write_error(...)
#endif

#ifdef __cplusplus
}
#endif
