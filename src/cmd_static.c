#include "plugins.h"

// this file registers commands from statically-linked packages
// see README.md for details
// it should be considered a template that is to be tailored by the user if needed!

//#include "some_cli/some_cli_cmds.h"

void register_client_cmds() {
  // if clients are static libraries, their commands shall be registered and initialized here
  /*extern service_com_t backend_posix_tty;
  register_some_cli_cmds();
  some_cli_init(&backend_posix_tty);*/
}

void client_cmds_exit() {
  // this function will be called when exiting
  // shall be used to clean up client commands (e.g. close opened ports)
}
