#define _GNU_SOURCE // needed for asprintf

#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <tcpp/tcpp.h>
#include <string.h>
#include <commands/lp_txn_conf.h>
#include <csp/csp_debug.h>
#include <assert.h>
#include "json_dot.h"
#include "vcom_config.h"
#include "vcom_debug.h"

#include <time.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

char *read_file(const char *filename) {
    FILE *f = fopen(filename, "r");
    char *buffer = NULL;
    
    if (f) {
        // Measure file size
        fseek(f, 0, SEEK_END);
        long length = ftell(f);
        // Rewind
        fseek(f, 0, SEEK_SET);
        // Read to a buffer
        buffer = malloc(length + 1);
        if (buffer) {
            size_t really_read = fread(buffer, 1, length, f);
            buffer[really_read - 1] = 0; // ensure terminated
        }
        fclose(f);
    } else {
        vd_error("Failed to open file %s", filename);
        return 0;
    }
    return buffer;

}

void vcom_start_log() {
    struct stat st = { 0 };
    if (stat(vcom_config.logopt.dir, &st) == -1) {
        mkdir(vcom_config.logopt.dir, 0700);
    }

    char time_str[40];
    time_t current_time;
    struct tm * time_info;
    time(&current_time);
    struct tm tm_buf;
    const char *fname_fmt;
    if (vcom_config.logopt.timestamp_mode == VCOM_TS_UTC) {
        time_info = gmtime_r(&current_time, &tm_buf);
        fname_fmt = "%Y_%m_%d_%H_%M_%SZ";   // literal Z marks UTC
    } else {
        time_info = localtime(&current_time);
        fname_fmt = "%Y_%m_%d_%H_%M_%S";
    }
    strftime(time_str, sizeof(time_str), fname_fmt, time_info);
    char logfile_name[256];
    snprintf(logfile_name, 256, "%s/vcom_log_%s.log", vcom_config.logopt.dir, time_str);

    vcom_config.logopt.fp = fopen(logfile_name, "w");
    if(!vcom_config.logopt.fp) {
        fprintf(stderr, "Failed to create log file: %s!\n", logfile_name);
        exit(EXIT_FAILURE);
    }

    gettimeofday((struct timeval *)&vcom_config.logopt.tval_start, NULL);
    if (vcom_config.logopt.timestamp_mode == VCOM_TS_UTC) {
        strftime(time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", time_info);
        vd_info("Log start: %s.%06jdZ", time_str,
                (intmax_t)vcom_config.logopt.tval_start.tv_usec);
    } else {
        strftime(time_str, sizeof(time_str), "%Y/%m/%d %H:%M:%S", time_info);
        vd_info("Log start: %s.%06jd", time_str,
                (intmax_t)vcom_config.logopt.tval_start.tv_usec);
    }

    vd_info("Writing log to file: %s", logfile_name);

    vd_info("VCOM version information:");
    vd_info("Semantic version: %s", VCOM_VERSION);
    vd_info("Git revision:  " VCOM_GITREV);
    vd_info("CSP revision:  " GIT_REV);
    vd_info("Build time:    " __DATE__ " " __TIME__);
    vd_info("Commands type: " VCOM_COMMANDS_TYPE);
}

void vcom_conf_i2c_paths_init(const char *vs);

void vcom_load_config_from_file(const char *filename)
{  
    char *buffer = NULL;
    cJSON *json = NULL;
    buffer = read_file(filename);
    if (!buffer) {
        vd_error("Failed to read config file %s", filename);
        goto fail;
    }
    json = cJSON_Parse(buffer);
    if (!json) {
        vd_error("Error parsing config file %s", filename);
        goto fail;
    }

    vd_info("Reading config from %s", filename);

    int vi = 0;
    bool vb = false;
    const char *vs = 0;

    // setup logging as soon as possible
    {
        cJSON *logging = NULL;
        if (0 == dot_get_object(json, "logging", &logging)) {
            if (0 == dot_get_bool(logging, "enable", &vb)) {
                vcom_config.logopt.enable = vb;
            }
            if (0 == dot_get_string(logging, "dir", &vs)) {
                vcom_config.logopt.dir = strdup(vs);
            }
            if (0 == dot_get_int(logging, "console_level", &vi)) {
                vcom_config.logopt.console_level = vi;
            }
            if (0 == dot_get_string(logging, "timestamp_mode", &vs)) {
                vcom_config.logopt.timestamp_mode =
                    (0 == strcasecmp(vs, "UTC")) ? VCOM_TS_UTC : VCOM_TS_UPTIME;
            }
        }
    }

    // list of plugin .so files
    {
        cJSON *plugins = NULL;
        if (0 == dot_get_array(json, "plugins", &plugins)) {
            cJSON *pgn;
            size_t plugin_count = cJSON_GetArraySize(plugins);
            // N+1 pointer slots: load_plugins() relies on a NULL terminator
            vcom_config.plugin_libs = calloc(plugin_count + 1, sizeof(char*));
            vcom_config.plugin_hashes = calloc(plugin_count + 1, sizeof(char*));
            if (!vcom_config.plugin_libs) {
                goto fail;
            }
            size_t pos = 0;
            cJSON_ArrayForEach(pgn, plugins) {
                const char *checksum = NULL;
                const char *path = NULL;
                if (0 != dot_get_string(pgn, "path", &path)) {
                    vd_error("Missing \"path\" in plugin config!");
                    continue;
                } 
                else {
                    if (path) {
                        vcom_config.plugin_libs[pos] = strdup(path);
                    }
                }
                if (0 != dot_get_string(pgn, "md5sum", &checksum)) {
                    vd_warn("Missing \"md5sum\" in plugin config!");
                }
                else {
                    if  (checksum) {
                        vcom_config.plugin_hashes[pos] = strdup(checksum);
                    }
                }
            pos++;
            }
        }
    }

    {
        cJSON *colors = NULL;
        if (0 == dot_get_any(json, "colors", &colors)) {
            if (cJSON_IsTrue(colors)) {
                vcom_config.colors = 1;
            } else if (cJSON_IsFalse(colors)) {
                vcom_config.colors = 0;
            } else if (cJSON_IsString(colors)) {
                const char *s = cJSON_GetStringValue(colors);
                if (strcasecmp(s, "yes") == 0 || strcasecmp(s, "true") == 0 || strcasecmp(s, "always") == 0) {
                    vcom_config.colors = 1;
                } else if (strcasecmp(s, "no") == 0 || strcasecmp(s, "false") == 0 || strcasecmp(s, "never") == 0) {
                    vcom_config.colors = 0;
                } else if (strcasecmp(s, "auto") == 0) {
                    vcom_config.colors = VCOM_COLORS_AUTO;
                } else {
                    vd_error("Bad option for \"colors\": %s", s);
                }
            }
        }
    }

    if (0 == dot_get_string(json, "load_routes", &vs)) {
        vcom_config.initial_routes_file = strdup(vs);
    }
    {
        cJSON *csp = NULL;
        if (0 == dot_get_object(json, "csp", &csp)) {
            if (0 == dot_get_int(csp, "address", &vi)) {
                vcom_config.cspopt.address = vi;
            }

            if (0 == dot_get_string(csp, "hostname", &vs)) {
                vcom_config.cspopt.hostname = strdup(vs);
            }

            {
                cJSON *debug = NULL;
                if (0 == dot_get_object(csp, "debug", &debug)) {
                    if (0 == dot_get_bool(debug, "error", &vb)) {
                        vcom_config.csp_debug_level[CSP_ERROR] = vb;
                    }
                    if (0 == dot_get_bool(debug, "warn", &vb)) {
                        vcom_config.csp_debug_level[CSP_WARN] = vb;
                    }
                    if (0 == dot_get_bool(debug, "info", &vb)) {
                        vcom_config.csp_debug_level[CSP_INFO] = vb;
                    }
                    if (0 == dot_get_bool(debug, "buffer", &vb)) {
                        vcom_config.csp_debug_level[CSP_BUFFER] = vb;
                    }
                    if (0 == dot_get_bool(debug, "packet", &vb)) {
                        vcom_config.csp_debug_level[CSP_PACKET] = vb;
                    }
                    if (0 == dot_get_bool(debug, "protocol", &vb)) {
                        vcom_config.csp_debug_level[CSP_PROTOCOL] = vb;
                    }
                    if (0 == dot_get_bool(debug, "lock", &vb)) {
                        vcom_config.csp_debug_level[CSP_LOCK] = vb;
                    }
                    if (0 == dot_get_bool(debug, "hexdump", &vb)) {
                        vcom_config.csp_debug_level[CSP_HEXDUMP] = vb;
                    }
                }
            }

            {
                cJSON *rdp = NULL;
                if (0 == dot_get_object(csp, "rdp", &rdp)) {
                    if (0 == dot_get_int(rdp, "window_size", &vi)) {
                        vcom_config.rdpopt.window_size = vi;
                    }
                    if (0 == dot_get_int(rdp, "conn_timeout_ms", &vi)) {
                        vcom_config.rdpopt.conn_timeout_ms = vi;
                    }
                    if (0 == dot_get_int(rdp, "packet_timeout_ms", &vi)) {
                        vcom_config.rdpopt.packet_timeout_ms = vi;
                    }
                    if (0 == dot_get_bool(rdp, "delayed_acks", &vb)) {
                        vcom_config.rdpopt.delayed_acks = (int)vb;
                    }
                    if (0 == dot_get_int(rdp, "ack_timeout_ms", &vi)) {
                        vcom_config.rdpopt.ack_timeout_ms = vi;
                    }
                    if (0 == dot_get_int(rdp, "ack_delay_count", &vi)) {
                        vcom_config.rdpopt.ack_delay_count = vi;
                    }
                }
            }

            if (0 == dot_get_int(csp, "conn_max", &vi)) {
                vcom_config.cspopt.conn_max = vi;
            }

            if (0 == dot_get_int(csp, "conn_queue_length", &vi)) {
                vcom_config.cspopt.conn_queue_length = vi;
            }

            if (0 == dot_get_int(csp, "fifo_length", &vi)) {
                vcom_config.cspopt.fifo_length = vi;
            }

            if (0 == dot_get_int(csp, "port_max_bind", &vi)) {
                vcom_config.cspopt.port_max_bind = vi;
            }

            if (0 == dot_get_int(csp, "buffers", &vi)) {
                vcom_config.cspopt.buffers = vi;
            }

            if (0 == dot_get_int(csp, "buffer_data_size", &vi)) {
                vcom_config.cspopt.buffer_data_size = vi;
            }

            if (0 == dot_get_int(csp, "conn_dfl_so", &vi)) {
                vcom_config.cspopt.conn_dfl_so = vi;
            }
        }
    }


    {
        cJSON *nii = NULL;
        if (0 == dot_get_object(json, "nii", &nii)) {
            if (0 == dot_get_bool(nii, "enable", &vb)) {
                vcom_config.use_nii = vb;
            }
            if (0 == dot_get_int(nii, "port", &vi)) {
                vcom_config.nii_port = vi;
            }
        }
    }

    {
        cJSON *paranoid = NULL;
        if (0 == dot_get_object(json, "paranoid", &paranoid)) {
            if (0 == dot_get_bool(paranoid, "enable", &vb)) {
                vcom_config.paranoid = vb;
            }
        }
    }

    {
        cJSON *batch = NULL;
        if (0 == dot_get_object(json, "batch", &batch)) {
            if (0 == dot_get_bool(batch, "enable", &vb)) {
                vcom_config.use_batch_input = vb;
            }
            if (0 == dot_get_bool(batch, "stop_on_error", &vb)) {
                vcom_config.batch_fail_exit = vb;
            }
            if (0 == dot_get_bool(batch, "as_start_script", &vb)) {
                vcom_config.batch_startup = vb;
            }
            if (0 == dot_get_string(batch, "file", &vs)) {
                vcom_config.batch_file_name = strdup(vs);
            }
        }
    }

    {
        cJSON *listen = NULL;
        if (0 == dot_get_object(json, "listen", &listen)) {
            if (0 == dot_get_bool(listen, "enable", &vb)) {
                vcom_config.use_listen = vb;
            }
            if (0 == dot_get_int(listen, "csp_port", &vi)) {
                vcom_config.listen_csp_port = vi;
            }
        }
    }

    {
        cJSON *txn = NULL;
        if (0 == dot_get_object(json, "txn", &txn)) {
            #define TXNSETCONF(__key, __field) \
                if (0 == dot_get_bool(txn, __key, &vb)) { \
                    if (cmd_lp_txn_conf.__field != vb) { \
                        cmd_lp_txn_conf.__field = vb; \
                        vcom_config.txn_config_changed = true; \
                    } \
                }

            TXNSETCONF("requests", txn_show_requests);
            TXNSETCONF("hex", txn_show_hex);
            TXNSETCONF("json", txn_show_json);
            TXNSETCONF("pretty", txn_show_pretty);
            TXNSETCONF("detailed", txn_annotated);
            TXNSETCONF("verbose", txn_verbose);
            #undef TXNSETCONF
        }
    }

    cJSON *ifaces = NULL;
    if (0 == dot_get_array(json, "ifc", &ifaces)) {
        cJSON *ifc;
        cJSON_ArrayForEach(ifc, ifaces) {
            // TODO this format supports defining e.g. multiple KISS interfaces
            const char *kind = NULL;
            if (0 != dot_get_string(ifc, "kind", &kind)) {
                vd_error("Missing \"kind\" in interface config!");
                continue;
            }

            if (0 == strcmp(kind, "KISS")) {
                if (0 == dot_get_bool(ifc, "enable", &vb)) {
                    vcom_config.use_uart = vb;
                }
                if (0 == dot_get_string(ifc, "device", &vs)) {
                    vcom_config.uart_device = strdup(vs);
                }
                if (0 == dot_get_int(ifc, "baud", &vi)) {
                    vcom_config.uart_baud = vi;
                }
            } else if (strstr(kind, "CAN")) {
#if VCOM_ENABLE_CAN
                vcom_config.num_cans++;
                uint8_t can_id = 0;
                if(strlen(kind) > 3) {
                    can_id = kind[3] - '0';
                }
                if (0 == dot_get_bool(ifc, "enable", &vb)) {
                    vcom_config.use_cans[can_id] = vb;
                }
                if (0 == dot_get_string(ifc, "device", &vs)) {
                    vcom_config.can_devices[can_id] = strdup(vs);
                }
                if (0 == dot_get_int(ifc, "baud", &vi)) {
                    vcom_config.can_bauds[can_id] = vi;
                }
#else
                if (0 == dot_get_bool(ifc, "enable", &vb)) {
                    if (vb) {
                        vd_error("CAN interface is disabled in this VCOM build, ignoring config entry!\n");
                    }
                }
#endif
            } else if (0 == strcmp(kind, "I2C")) {
                if (0 == dot_get_bool(ifc, "enable", &vb)) {
                    vcom_config.use_i2c = vb;
                }
                if (0 == dot_get_string(ifc, "device", &vs)) {
                    vcom_conf_i2c_paths_init(vs);
                }
            } else if (0 == strcmp(kind, "TCP") || 0 == strcmp(kind, "TCPCL")) {
                if (0 == dot_get_bool(ifc, "enable", &vb)) {
                    vcom_config.use_tcp_cli = vb;
                }
                if (0 == dot_get_string(ifc, "address", &vs)) {
                    vcom_config.tcp_cli_adr_port = strdup(vs);
                }
                if (0 == dot_get_string(ifc, "password", &vs)) {
                    vcom_config.tcp_cli_pw = strdup(vs);
                }
                if (0 == dot_get_bool(ifc, "legacy_protocol", &vb)) {
                    vcom_config.tcp_cli_legacy = vb;
                }
            } else if (0 == strcmp(kind, "TCPD") || 0 == strcmp(kind, "TCPSRV") || 0 == strcmp(kind, "TCPBR")) {
                if (0 == dot_get_bool(ifc, "enable", &vb)) {
                    vcom_config.use_tcp_srv = vb;
                }
                if (0 == dot_get_int(ifc, "port", &vi)) {
                    vcom_config.tcp_srv_port = vi;
                }
                if (0 == dot_get_string(ifc, "password", &vs)) {
                    vcom_config.tcp_srv_pw = strdup(vs);
                }
                if (0 == dot_get_bool(ifc, "legacy_protocol", &vb)) {
                    vcom_config.tcp_srv_legacy = vb;
                }
            } else if (0 == strcmp(kind, "KISS_NET")) {
                if (0 == dot_get_bool(ifc, "enable", &vb)) {
                    vcom_config.use_kiss_net = vb;
                }
                if (0 == dot_get_string(ifc, "host_ip", &vs)) {
                    vcom_config.kiss_net_host_ip = strdup(vs);
                }
                if (0 == dot_get_int(ifc, "host_port", &vi)) {
                    vcom_config.kiss_net_host_port = vi;
                }
            } else {
                vd_error("Bad \"kind\" = %s in interface config!", kind);
            }
        }
    }

    /* Dump config */
    if(vcom_config.logopt.enable) {
        char *printed = cJSON_Print(json);
        vd_debug("Parsed config JSON from file %s: %s\n", filename, printed);
        cJSON_free(printed);
    }

    if (0 == dot_get_string(json, "hmac_key", &vs)) {
        vcom_config.hmac_key = strdup(vs);
    }

    // Fall-through to clean-up
    fail:
    // clean up
    if (buffer) {
        free(buffer);
        buffer = NULL;
    }
    if (json) {
        cJSON_Delete(json);
        json = NULL;
    }
}

void vcom_conf_i2c_paths_init(const char *devname)
{
    int suc = asprintf((char**) &vcom_config.i2c_devfile, "/dev/%s", devname);
    assert(suc >= 0);
    suc = asprintf((char**) &vcom_config.i2c_safile, "/sys/bus/i2c/devices/%s/slave-addr", devname);
    assert(suc >= 0);
    suc = asprintf((char**) &vcom_config.i2c_sdfile, "/sys/bus/i2c/devices/%s/slave-data", devname);
    assert(suc >= 0);
}

/**
 * Global VCOM config.
 * Run-time modifications may not take effect for some fields.
 */
volatile struct app_config vcom_config = {
    .logopt = {
        .enable = true,
        .dir = "./logs",
        .console_level = 2,
        .fp = NULL,
    },

    .use_uart = false,
    .uart_device = "/dev/ttyUSB0",
    .uart_baud = 500000,

#if VCOM_ENABLE_CAN
    .num_cans = 1,
    .use_cans = { 0 },
    .can_devices = { "can0", NULL, NULL, NULL },
    .can_bauds = { 500000, 0, 0, 0, },
#endif

    .use_tcp_cli = false,
    .tcp_cli_adr_port = NULL,
    .tcp_cli_pw = "",

    .use_tcp_srv = false,
    .tcp_srv_port = TCPP_PORT,
    .tcp_srv_pw = "",

    .use_kiss_net = false,
    .kiss_net_host_ip = "127.0.0.1",
    .kiss_net_host_port = 2000,

    .use_nii = false,
    .nii_port = 9500,

    .paranoid = false,

    .use_batch_input = false,
    .batch_fail_exit = false,
    .batch_startup = false,
    .batch_file_name = "--",

    .i2c_devfile = NULL,
    .i2c_safile = NULL,
    .i2c_sdfile = NULL,

    .colors = VCOM_COLORS_AUTO,

    .csp_debug_level = {
        [CSP_ERROR]	= true,
        [CSP_WARN]	= true,
        [CSP_INFO]	= false,
        [CSP_BUFFER]	= false,
        [CSP_PACKET]	= false,
        [CSP_PROTOCOL]	= false,
        [CSP_LOCK]	= false,
        [CSP_HEXDUMP]	= false,
    },

    .rdpopt = {
        .window_size = 10,
        .conn_timeout_ms = 10000,
        .packet_timeout_ms = 1000,
        .delayed_acks = 1,
        .ack_timeout_ms = 500,
        .ack_delay_count = 7,
    },

    .cspopt = {
        .hostname = "VCOM",
        .address = 8,
        .conn_max = 30,
        .conn_queue_length = 30,
        .fifo_length = 50,
        .port_max_bind = 32,
        .buffers = 400,
        .buffer_data_size = 512,
        .conn_dfl_so = 0,
    },

    .hmac_key = "",
};
