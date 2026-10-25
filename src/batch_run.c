#include "batch_run.h"
#include "vcom_config.h"
#include <stdio.h>
#include <unistd.h>
#include <console/console.h>

void batch_run(FILE *input, bool fail_exit) {
    console_ctx_t *ctx = console_ctx_init(NULL, NULL, stdout);
    ctx->interactive = false;
    ctx->use_colors = vcom_config.colors; // colors must be resolved to boolean at this point
    ctx->exit_allowed = true;
    sprintf(ctx->prompt, "vcom:%d>", vcom_config.cspopt.address);

    bool file_is_tty = isatty(fileno(input));

    int c;
    size_t wi = 0;
    console_err_t result;
    int pRetval = CONSOLE_OK;

    while((c = fgetc(input)) != EOF) {
        if (c == '\r' || c == '\n') {
handle_cmd:
            if (wi == 0) {
                // nothing to run
                if (ctx->exit_requested) {
                    return;
                } else {
                    continue;
                }
            }
            ctx->line_buffer[wi] = 0;

            if (!file_is_tty) {
                // Show the command - useful in logs
                printf("%s %s\n", ctx->prompt, ctx->line_buffer);
            }

            result = console_handle_cmd(ctx, ctx->line_buffer, &pRetval, NULL);
            if (fail_exit && (result != CONSOLE_OK || pRetval != CONSOLE_OK)) {
                printf("Terminating execution because of previous command fail!\n");
                ctx->exit_requested = true;
            }
            if (ctx->exit_requested) {
                return;
            }
            wi = 0;
        } else if (wi < CONSOLE_LINE_BUF_LEN - 1) {
            ctx->line_buffer[wi++] = (char) c;
        }
    }

    if (wi > 0) {
        // handle the remainder, then exit
        ctx->exit_requested = true; // This flag aborts the loop after the command is handled
        goto handle_cmd;
    }
}
