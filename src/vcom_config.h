/**
 * VCOM main config struct & config loading
 *
 * Created on 2020/06/14.
 */

#ifndef VCOM_MAIN_H
#define VCOM_MAIN_H

#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdbool.h>
#include "conf_vcom.h"

void vcom_start_log();
void vcom_load_config_from_file(const char *filename);
void vcom_conf_i2c_paths_init(const char *devname);

#define VCOM_COLORS_AUTO (-1)

// TODO smarter way to handle multiple interfaces of the same kind
#define VCOM_MAX_NUM_SAME_IFC        (4)

struct vcom_config_rdpopt {
    unsigned int window_size;
    unsigned int conn_timeout_ms;
    unsigned int packet_timeout_ms;
    unsigned int delayed_acks;
    unsigned int ack_timeout_ms;
    unsigned int ack_delay_count;
};

struct vcom_config_cspopt {
    const char *hostname;
    uint8_t address;       //!< Own CSP address
    uint8_t conn_max;		/**< Max number of connections. A fixed connection array is allocated by csp_init() */
    uint8_t conn_queue_length;	/**< Max queue length (max queued Rx messages). */
    uint8_t fifo_length;		/**< Length of incoming message queue, used for handover to router task. */
    uint8_t port_max_bind;		/**< Max/highest port for use with csp_bind() */
    uint8_t rdp_max_window;		/**< Max RDP window size */
    uint16_t buffers;		/**< Number of CSP buffers */
    uint16_t buffer_data_size;	/**< Data size of a CSP buffer. Total size will be sizeof(#csp_packet_t) + data_size. */
    uint32_t conn_dfl_so;		/**< Default connection options. Options will always be or'ed onto new connections, see csp_connect() */
};

enum vcom_timestamp_mode {
    VCOM_TS_UPTIME = 0,         //!< Prepend software uptime (sec.usec) - legacy default
    VCOM_TS_UTC,                //!< Prepend ISO-8601 UTC timestamp (ms precision, 'Z')
};

struct vcom_config_logopt {
    bool enable;                //!< Enable logging to file
    char *dir;                  //!< Log files directory
    int console_level;          //!< Logging level to print to console (0 - trace, 4 - error)
    enum vcom_timestamp_mode timestamp_mode; //!< File-log timestamp format
    FILE* fp;                   //!< Pointer to the open logging file
    struct timeval tval_start;
};

/** VCOM configuration */
struct app_config {
    char** plugin_libs;       //!< Paths to shared libs, malloc'd, last is NULL
    char** plugin_hashes;     //!< Plugin md5sum hashes
    bool use_uart;            //!< Enable USART
    char *uart_device;        //!< TTY device (/dev/ttyUSB0)
    uint32_t uart_baud;       //!< TTY baud rate

#if VCOM_ENABLE_CAN
    uint8_t num_cans;         //!< Number of socketcan devices (maximum of VCOM_MAX_NUM_SAME_IFC)
    bool use_cans[VCOM_MAX_NUM_SAME_IFC];             //!< Enable/disable each CAN device
    char *can_devices[VCOM_MAX_NUM_SAME_IFC];         //!< CAN devices
    uint32_t can_bauds[VCOM_MAX_NUM_SAME_IFC];        //!< CAN baud rates
#endif

    //char *packetlog_file;     //!< Packet log file TODO implement packetlog!
    struct vcom_config_logopt logopt;
    char *initial_routes_file; //!< Routes file to load at startup

    bool use_tcp_cli;         //!< Enable CSP TCP client
    char *tcp_cli_adr_port;   //!< address:port for TCP client mode (connect to emulator / other VCOM / csp-term)
    char *tcp_cli_pw;         //!< Password to pass to the CSP TCP server, empty string = no password
    bool tcp_cli_legacy;      //!< Use legacy protocol

    bool use_tcp_srv;         //!< Enable CSP TCP server
    uint16_t tcp_srv_port;    //!< Listen port for TCP bridge mode (CSP server)
    char *tcp_srv_pw;         //!< Password for the CSP TCP server, empty string if no password is set
    bool tcp_srv_legacy;      //!< Use legacy protocol

    bool use_batch_input;     //!< Commands are read from stdin or a file
    bool batch_fail_exit;     //!< Stop execution of batch, when any command fails
    bool batch_startup;//!< Go to interactive console when the batch file is completed. Does not work when the batch file is stdin.
    char *batch_file_name;    //!< Batch input file name

    bool use_nii;             //!< Indicates non-interactive mode (commands sent via TCP interface)
    uint16_t nii_port;        //!< Non-interactive command interface port

    bool use_listen;          //!< Run listen mode on selected CSP port
    uint16_t listen_csp_port; //!< CSP port to listen on

    int colors;               //!< Colors; -1=auto

    bool use_i2c;
    char *i2c_devfile;        //!< I2C device file, e.g. /dev/i2c-10
    char *i2c_safile;         //!< I2C slave address control file (LUCI driver)
    char *i2c_sdfile;         //!< I2C slave data queue-read file (LUCI driver)

    bool use_kiss_net;        //!< Enable KISS over network
    char *kiss_net_host_ip;   //!< Remote host IP
    uint16_t kiss_net_host_port;  //!< Remote host port

    bool paranoid;            //!< Do not allow loading plugins with dirty/unknown git revisions

    bool csp_debug_level[8];
    bool txn_config_changed;

    struct vcom_config_rdpopt rdpopt;
    struct vcom_config_cspopt cspopt;

    char * hmac_key;
};

extern volatile struct app_config vcom_config;

#endif //VCOM_MAIN_H
