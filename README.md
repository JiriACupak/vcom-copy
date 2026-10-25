VCOM
====

VZLU Command-Line Communicator

# How to compile

The following packages are needed to build VCOM and its dependencies:

```bash
apt-get install curl zip pip git pkg-config libsocketcan-dev cargo libssl-dev
```

Additionally, `meson` and `ninja` are used to configure and build VCOM. It is recommended to install them via `pip` as the packages in `apt` tend to be outdated:

```bash
pip install meson ninja
```

There are three possible options how to build VCOM, depending on how should additional commands be handled. All of these can be selected by changing the `commands_type` meson option.
1. Pure VCOM (`commands_type=none`) - This is the simplest way to build VCOM, however, **no additional commands can be added without modifying VCOM source!**. Purpose of this build type is to allow standalone builds without assuming VZLU dependencies are available. The `build.sh` script is provided to build in this configuration.
2. Statically linked (`commands_type=static`) - In this mode, VCOM and all of its dependencies are linked statically, resulting in a single standalone executable. Additional commands can be added as meson subprojects. This option does not allow usage of plugins. See chapter "Building with statically linked commands" for details. 
3. With plugin support (`commands_type=plugins`) - VCOM is built using dynamic linkage for its dependencies. This requires VZLU dependencies to be installed in a known prefix, however, it also enables plugin support, as described below.

## Installation with plugin support

The easiest way to install VCOM is to use the `install.sh` script, which will clone, build and install all VZLU dependencies into the prefix provided as its first argument - by default, the prefix is `/opt/vzlu`. Installing to this prefix will likely require root, so you might get asked for a password several times during the installation. It should be noted that `install.sh` requires that the needed packages (listed above) are already installed!

This script can also be used to setup development environment for VCOM plugins, provided they are built with the prefix that VCOM was installed into. When rebuilding VCOM after installation, the script `build_plugins.sh` can be used.

When developing VCOM plugins, it is useful to have access to rebuilding VCOM dependencies in different configurations. For that purpose, you can specify a custom path to the `install.sh` script as its second argument, in which the VZLU dependency repositories will be cloned into. If this argument is not provided, these repositories will be cloned into `/tmp/vzlu` and deleted after installation. If a custom clone path is provided, the repositories will be kept after the install.

## Building with statically linked commands

By default, new functionality is added into VCOM by using plugins (see "Adding new commands" section). However, in some cases, it may be advantageous to have a single statically linked binary that contains all functionality without external dependencies. This can be achieved by statically linking command subprojects into VCOM during build:

1. Add your command packages into `subprojects/` directory - meson wrap files can be used to achieve this. The command packages **MUST** follow the VZLU component template: https://gitlab.com/vzlu/sw/component-template
2. In `src/cmd_static.c`, `#include` the client command header(s) and in the function `register_client_cmds` add a call to your package command registration function(s). If the command package also requires some initialization (e.g., SALLY backend registration), this is the place to do it. For example:

```c
#include "some_cli/some_cli_cmds.h"

void register_client_cmds() {
  // if clients are static libraries, their commands shall be registered and initialized here
  extern service_com_t backend_posix_tty;
  register_some_cli_cmds();
  some_cli_init(&backend_posix_tty);
}
```

3. Add the package dependency to `meson.build` (see the file for an example).
4. Build using the appropriate script.

```bash
./build_static.sh
```

Now, VCOM is built as a standalone statically linked binary. Keep in mind that packages built in this way are not considered plugins, and no plugins can be loaded!

## Debug build

To keep debug symbols add "debug" option to the build command.

```bash
meson setup --wipe --debug build
cd build
ninja
```

# Config file

VCOM tries to loads its config from `vcom.json`, if found. Use `-c` to specify a different file to load.

The config file can set all the options that are available as CLI arguments, plus some additional ones, 
such as the CSP buffers config and initial RDP options.

See `vcom.example.json` for a complete list of options that can be used, each with its default value.

CLI arguments have precedence over the config file.

NOTE: The JSON format does not support comments!  
NOTE: Defining multiple interfaces of the same kind is not currently supported, it may be added in the future.

# Adding new commands

New commands can be added to VCOM by adding the path to a plugin containing those commands to the json configuration file (as e.g. `"plugins": [ { "path": "path/to/plugin.so", "md5sum": "01234abcd" } ]`). Most VZLU software and hardware is distributed with plugins built for the most common platforms using CI.

If adding a competely new set of commands, the following example can be used as a starting point https://gitlab.com/vzlu/sw/component-template.

On startup, VCOM will try to load the plugin and if successful, execute `register_cli_cmds()` from the plugin. Therefore, registration of client commands in the plugin must be done in that function.


# Můj postup

Postupoval jsem dle README sekce "Installation with plugin support", VCOM se mi podařilo sestavit bez větších problémů. Následně jsem se pokoušel sestavit dle návodu kódy v component-templates. Podařilo se mi vytvořit soubor "libcomp-templ-cli.so", opakovaně se mi tento plugin nepodařoli spojit s VCOM. Pomocí příkazu s parametry "readelf --dyn-syms -W" jsem zjistil, že v této knihovně není funkce "register_cli_cmds". Vytvořil jsem soubor "plugin.c" s vlastním pluginem "cmd_my_hello", který jsem registroval do funkce "register_cli_cmds". Po přeložení na "my_plugin.so" se mi ho podařilo propojit s VCOM. 

Můj plugin je jen obyčejné vypsání odpovědi na text "hello". Nemělo by být ale problémové takto napsat libovolný plugin. Případně i do této podoby upravit pluginy z component-templates. 

Další problém, na který jsem narazil, byla úprava souboru "vcom.json". Ve VS code i v Poznámkovém bloku se po uložení změnila struktura souboru (byl delší o 8 bajtů). Zkontroloval jsem kódování i ukončování řádků, v ničem jsem chybu nenašel. Fungovalo až upravování tohoto souboru pomocí nástroje "nano". 

Problém ve "vcom.json" je i relativní cesta k souboru obsahující plugin. Z tohoto důvodu je "my_plugin.so" umístěn ve stejné složce jako samotný VCOM.

# Použití AI

Používal jsem model Gemini 3.6 flash. Pomocí AI jsem vyhledával linuxové příkazy vhodné pro tento projekt (např. readelf, hexdump) a pomáhala my napsat kód v jazyku C, který je přeložitelný do dynamické knihovny.



