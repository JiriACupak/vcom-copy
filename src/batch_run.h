/**
 * Run commands from a file (stdin or a real file)
 * 
 * Created on 2020/08/17.
 */

#ifndef VCOM_BATCH_RUN_H
#define VCOM_BATCH_RUN_H

#include <stdio.h>
#include <stdbool.h>

void batch_run(FILE *input, bool fail_exit);

#endif //VCOM_BATCH_RUN_H
