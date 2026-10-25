#ifndef VCOM_PLUGINS_H_
#define VCOM_PLUGINS_H_

#include "conf_vcom.h"

void load_plugins();
void register_plugin_cmds();

#if defined(VCOM_STATIC_CMDS)
  void register_client_cmds();
  void client_cmds_exit();
#endif

#endif