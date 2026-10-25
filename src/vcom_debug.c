#include <stdarg.h>
#include <csp/arch/csp_system.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <console/console.h>
#include <sys/time.h>
#include <time.h>
#include <inttypes.h>
#include "vcom_debug.h"
#include "vcom_config.h"

static void vd_strip_ansi(char* text, size_t len) {
    size_t i = 0;
    while((text[i] != '\0') && (i < len)) {
        if(text[i] == '\x1b') {
            int sequence_len = 1;

            // check the next char
            if(text[i + 1] == '[') {
                // Control Sequence Introducer
                sequence_len++;

                // sequentially check following characters and find the sequence end
                for(size_t j = i + 2; j < len; j++) {
                    sequence_len++;
                    if(!((text[j] >= '0') && (text[j] <= '9')) && !(text[j] == ';')) {
                        break;
                    }
                }
            }

            memmove(&text[i], &text[i + sequence_len], len - i);
        
        } else if(text[i] == '\r') {
            if(text[i + 1] != '\n') {
                memmove(&text[i], &text[i + 1], len - i);
            } else {
                i++;
            }
            
            
        } else {
            i++;
        
        }
        
    }
}

static void vd_get_time_str(char* dest, int size) {
    struct timeval tval_now;
    gettimeofday(&tval_now, NULL);

    if (vcom_config.logopt.timestamp_mode == VCOM_TS_UTC) {
        struct tm tm;
        gmtime_r(&tval_now.tv_sec, &tm);
        size_t n = strftime(dest, size, "%Y-%m-%dT%H:%M:%S", &tm);
        snprintf(dest + n, size - n, ".%06jdZ", (intmax_t)tval_now.tv_usec);
    } else {
        struct timeval tval_diff;
        timersub(&tval_now, &vcom_config.logopt.tval_start, &tval_diff);
        snprintf(dest, size, "%jd.%06jd",
                 (intmax_t)tval_diff.tv_sec, (intmax_t)tval_diff.tv_usec);
    }
}

void vd_log_to_file(const char *text, size_t len) {
    // print time
    char time_str[40];
    vd_get_time_str(time_str, sizeof(time_str));
    const char *ts_fmt = (vcom_config.logopt.timestamp_mode == VCOM_TS_UTC)
                             ? "[%s] " : "%s: ";
    fprintf(vcom_config.logopt.fp, ts_fmt, time_str);
    
    // strip ANSI control sequences
    char* str = (char*)malloc(len + 1);
    memcpy(str, text, len);
    str[len] = '\0';
    vd_strip_ansi(str, len);
    fprintf(vcom_config.logopt.fp, "%s", str);
    fprintf(vcom_config.logopt.fp, "\n");
    free(str);
    fflush( vcom_config.logopt.fp );
}

void do_vd_log(int level, const char *format, ...) {
    va_list args, log_args;
    va_start(args, format);
    va_copy(log_args, args);

    static const int LEVEL_COLORS[] = {
        COLOR_RESET,
        COLOR_RESET,
        COLOR_GREEN,
        COLOR_YELLOW,
        COLOR_RED,
    };

    assert(level < 5);

    // only print to console above configured level
    int len = 0;
    if(level >= vcom_config.logopt.console_level) {
        int color = vcom_config.colors == 0 ?
            COLOR_RESET : LEVEL_COLORS[level];

        csp_sys_set_color(color);
        len = vfprintf(stderr, format, args);
        csp_sys_set_color(COLOR_RESET);
        fprintf(stderr, "\n");
    } else {
        len = vsnprintf(NULL, 0, format, args);
    }
    va_end(args);

    if(len > 0 && vcom_config.logopt.enable && vcom_config.logopt.fp) {
        char* text = (char*)malloc(len + 1);
        vsprintf(text, format, log_args);
        vd_log_to_file(text, len);
        free(text);
    }
    va_end(log_args);
}


void vcom_csp_debug_hook(csp_debug_level_t level, const char *format, va_list args)
{
    va_list log_args;
    va_copy(log_args, args);

    const char *CSP_LEVEL_NAMES[] = {
            [CSP_ERROR] = "ERROR",
            [CSP_WARN] = "WARN",
            [CSP_INFO] = "INFO",
            [CSP_BUFFER] = "BUFFER",
            [CSP_PACKET] = "PACKET",
            [CSP_PROTOCOL] = "PROTOCOL",
            [CSP_LOCK] = "LOCK",
            [CSP_HEXDUMP] = "HEXDUMP",
    };

    int color = (level == CSP_ERROR) ? COLOR_RED :
                (level == CSP_WARN) ? COLOR_YELLOW :
                COLOR_RESET;

    if (vcom_config.use_nii || !console_context_available()) {
        if (vcom_config.colors) {
            fprintf(stderr, "\x1b[%dm", color==COLOR_RED?31:color==COLOR_YELLOW?33:0);
        }

        int csp_level_len = fprintf(stderr, "CSP [%s] ", CSP_LEVEL_NAMES[level]);

        int len = vfprintf(stderr, format, args);
        if (vcom_config.colors) {
            fprintf(stderr, "\x1b[m");
        }

        fputc('\n', stderr);

        if(len > 0 && vcom_config.logopt.enable && vcom_config.logopt.fp) {
            char* text = (char*)malloc(csp_level_len + len + 1);
            snprintf(text, csp_level_len + 1, "CSP [%s] ", CSP_LEVEL_NAMES[level]);
            vsprintf(&text[csp_level_len], format, log_args);
            vd_log_to_file(text, csp_level_len + len);
            free(text);
        }
    }

    console_printf_ctx(console_active_ctx, color, "CSP [%s] ", CSP_LEVEL_NAMES[level]);
    console_vprintf_ctx(console_active_ctx, color, format, args);
    console_write("\n", 1);
}
