/**
 * VCOM-specific debug logging
 *
 * Created on 2021/10/08.
 */

#ifndef VCOM_DEBUG_H
#define VCOM_DEBUG_H

#include <csp/csp_debug.h>

void vd_log_to_file(const char *text, size_t len);

void do_vd_log(int level, const char *format, ...) __attribute__ ((format (__printf__, 2, 3)));

#define vd_trace(format, ...) do_vd_log(0, format, ##__VA_ARGS__)
#define vd_debug(format, ...) do_vd_log(1, format, ##__VA_ARGS__)
#define vd_info(format, ...) do_vd_log(2, format, ##__VA_ARGS__)
#define vd_warn(format, ...) do_vd_log(3, format, ##__VA_ARGS__)
#define vd_error(format, ...) do_vd_log(4, format, ##__VA_ARGS__)

void vcom_csp_debug_hook(csp_debug_level_t level, const char *format, va_list args);

#endif //VCOM_DEBUG_H
