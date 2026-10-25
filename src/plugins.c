#include "plugins.h"

#include <stdlib.h>
#include <assert.h>
#include <dlfcn.h>
#include <dirent.h> 
#include <string.h> 

#include "vcom_config.h"
#include "vcom_debug.h"

#include "console/console.h"
#include <console/cmddef.h>

#include <openssl/md5.h>

#define MAX_NUM_PLUGINS         (64)
#define MAX_PLUGIN_NAME_LEN     (64)
#define MAX_PLUGIN_SEMVER_LEN   (16)
#define MAX_PLUGIN_GITREV_LEN   (13)
#define MAX_PLUGIN_STRING_SIZE  (MAX_PLUGIN_NAME_LEN + 10 + 4*(20+1) + 9 + MAX_PLUGIN_GITREV_LEN + 1)

typedef void (*cb_so_register_cmds)(void);
typedef void (*cb_so_get_version)(char* semver, char* gitrev);
typedef void (*cb_so_set_runtime_config)(void*);

struct semver_t {
    int major;
    int minor;
    int patch;
    int build;
};

struct vcom_plugin_t {
    // handle of the so library for dlopen/dlclose/dlsym
    void* so_handle;

    // symbols loaded from the plugin
    cb_so_register_cmds register_cli_cmds;
    cb_so_get_version get_version;
    cb_so_set_runtime_config set_runtime_config;

    // additional information
    struct semver_t semver;
    char gitrev[MAX_PLUGIN_GITREV_LEN + 1];
    char name[MAX_PLUGIN_NAME_LEN + 1];
};

static struct vcom_plugin_t plugins[MAX_NUM_PLUGINS] = { { 0 } };
static size_t num_plugins = 0;
static size_t num_susp_plugins = 0;

char * compute_file_md5(const char *filename) {
	unsigned char c[MD5_DIGEST_LENGTH];
	int i;
	MD5_CTX mdContext;
	int bytes;
	unsigned char data[1024];
	char *filemd5 = (char*) malloc(33 *sizeof(char));

	FILE *inFile = fopen (filename, "rb");
	if (inFile == NULL) {
		vd_error("File not found %s", filename);
		return 0;
	}

	MD5_Init (&mdContext);
	while ((bytes = fread (data, 1, 1024, inFile)) != 0)
	MD5_Update (&mdContext, data, bytes);
	MD5_Final (c,&mdContext);
	for(i = 0; i < MD5_DIGEST_LENGTH; i++) {
		sprintf(&filemd5[i*2], "%02x", (unsigned int)c[i]);
	}

	fclose(inFile);
	return filemd5;
}

static void get_plugin_info(size_t plugin_num, char* plugin_info) {
    // TODO: check plugin_info size against max size
    if (plugin_info) {
        snprintf(plugin_info, MAX_PLUGIN_STRING_SIZE,
            "%s: version %d.%d.%d.%d, gitrev %s",
            plugins[plugin_num].name,
            plugins[plugin_num].semver.major,
            plugins[plugin_num].semver.minor,
            plugins[plugin_num].semver.patch,
            plugins[plugin_num].semver.build,
            plugins[plugin_num].gitrev);
    }
}

void load_plugins() {
    // Load command plugin
    
    char** ppath = vcom_config.plugin_libs;
    char** pchecksum = vcom_config.plugin_hashes;
    if(!ppath) {
        return;
    }
    
    while (*ppath) {
        char *path = *ppath;
        char *checksum = *pchecksum;
        // TODO walk directories
        
        struct dirent *dir;
        DIR *d = opendir(path);
        if (d) {
            // TODO fix debug levels
            //vd_debug("Load .so plugins from directory \"%s\"", path);
            size_t pathlen = strlen(path);
            while ((dir = readdir(d)) != NULL) {        
                if(dir->d_type == DT_DIR) {
                    // Skip a directory. Other types might be valid.
                    continue;
                }
                
                size_t namlen = strlen(dir->d_name);
                if (strstr(dir->d_name, ".so") == (&dir->d_name[0] + namlen - 3)) {
                    char *pathbuf = malloc(pathlen + 1 + namlen + 1);
                    assert(pathbuf);
                    sprintf(pathbuf, "%s/%s", path, dir->d_name);
                    //vd_debug("Loading plugin lib \"%s\"", pathbuf);
                    void *sohandle = dlopen(pathbuf, RTLD_NOW); // | RTLD_GLOBAL
                    if (!sohandle) { 
                        vd_error("Fail to load plugin: %s", dlerror());
                    }
                    free(pathbuf);
                }
            }
            closedir(d);
        
        } else {

            // Check integrity of library before opening, fail if paranoid.        
            char *md5 = compute_file_md5(path);
            if (md5 == 0) {
                if (vcom_config.paranoid) {
                    vd_error("Plugin %s md5 checksum calculation failed! Skipping.", path);
                    ppath++;
                    pchecksum++;
                    continue;
                }
                else {
                    vd_warn("Plugin %s md5 checksum calculation failed!", path);
                }
                
            }
            else {
                if (checksum == NULL || strcmp(checksum, md5) != 0) {
                    if (checksum) {
                        if (vcom_config.paranoid) {
                            vd_error("Plugin %s checksum does not match! Skipping plugin.", path);
                            vd_error("Computed: %s, Got: %s", compute_file_md5(path), checksum);
                            ppath++;
                            pchecksum++;
                            continue;
                        }
                        else {
                            vd_warn("Plugin %s checksum does not match!", path);
                            vd_warn("Computed: %s, Got: %s", compute_file_md5(path), checksum);
                        }
                        
                    }
                    else {
                        if (vcom_config.paranoid) {
                            vd_error("Hash empty, skipping %s", path);
                            ppath++;
                            pchecksum++;
                            continue;
                        }
                    }    
                }
            }
            // .so library, try to open it
            //vd_debug("Loading plugin lib \"%s\"", path);
            plugins[num_plugins].so_handle = dlopen(path, RTLD_NOW);
            if (!plugins[num_plugins].so_handle) { 
                vd_error("Failed to load plugin: %s", dlerror());
            
            } else {
                // it's open, try to find the expected symbols
                plugins[num_plugins].register_cli_cmds = dlsym(plugins[num_plugins].so_handle, "register_cli_cmds");
                if (!plugins[num_plugins].register_cli_cmds) {
                    // at least the client commands are required
                    vd_error("Failed to find \"register_cli_cmds()\" in plugin \"%s\": %s", path, dlerror());
                    dlclose(plugins[num_plugins].so_handle);
                    plugins[num_plugins].so_handle = NULL;

                } else {
                    char* name = strrchr(path, '/') + 1;
                    //vd_debug("Registered commands from \"%s\"", path);
                    strncpy(plugins[num_plugins].name, name, MAX_PLUGIN_NAME_LEN);

                    // try to also access the version information
                    plugins[num_plugins].get_version = dlsym(plugins[num_plugins].so_handle, "get_version");
                    if(!plugins[num_plugins].get_version) {
                        vd_error("Failed to find \"get_version()\" in plugin \"%s\": %s", path, dlerror());
                        strncpy(plugins[num_plugins].gitrev, "unknown", MAX_PLUGIN_GITREV_LEN + 1);

                    } else {
                        char semver_str[MAX_PLUGIN_SEMVER_LEN + 1] = { 0 };
                        plugins[num_plugins].get_version(semver_str, plugins[num_plugins].gitrev);
                        sscanf(semver_str, "%d.%d.%d.%d", 
                            &plugins[num_plugins].semver.major,
                            &plugins[num_plugins].semver.minor,
                            &plugins[num_plugins].semver.patch,
                            &plugins[num_plugins].semver.build);
                    }

                    // check plugin integrity
                    bool load_plugin = false;
                    bool plugin_valid = true;
                    if((strstr(plugins[num_plugins].gitrev, "unknown") != NULL) || 
                        (strstr(plugins[num_plugins].gitrev, "dirty") != NULL)) {
                            plugin_valid = false;
                    }

                    if(plugin_valid) {
                        load_plugin = true;
                    } else {
                        num_susp_plugins++;
                        if(vcom_config.paranoid) {
                            vd_error("Failed to load plugin \"%s\"", path);
                            vd_error("gitrev is: \"%s\"", plugins[num_plugins].gitrev);
                            dlclose(plugins[num_plugins].so_handle);
                            plugins[num_plugins].so_handle = NULL;
                            load_plugin = false;
                        } else {
                            vd_warn("Suspicious plugin \"%s\"", path);
                            vd_error("gitrev is: \"%s\"", plugins[num_plugins].gitrev);
                            load_plugin = true;
                        }
                    }

                    if(load_plugin) {
                        plugins[num_plugins].register_cli_cmds();
                        num_plugins++;
                    }
                }
            }

        }
          
        ppath++;
        pchecksum++;
    }

    vd_info("Succesfully loaded %d plugins", (int)num_plugins);
    char plugin_info[MAX_PLUGIN_STRING_SIZE];
    for(size_t i = 0; i < num_plugins; i++) {
        if(plugins[i].so_handle) {
            get_plugin_info(i, plugin_info);
            vd_debug("%s", plugin_info);
        }
    }

    if((num_susp_plugins > 0) && (!vcom_config.paranoid)) {
        vd_warn("One or more plugins have bad version info. They were loaded because \"--paranoid\" option is unset.");
    }
}

void unload_plugins() {
    for(size_t i = 0; i < MAX_NUM_PLUGINS; i++) {
        if(plugins[i].so_handle) {
            dlclose(plugins[i].so_handle);
            plugins[i].so_handle = NULL;
            plugins[i].semver.major = 0;
            plugins[i].semver.minor = 0;
            plugins[i].semver.patch = 0;
            plugins[i].semver.build = 0;
            memset(plugins[i].gitrev, '\0', MAX_PLUGIN_GITREV_LEN + 1);
            memset(plugins[i].name, '\0', MAX_PLUGIN_NAME_LEN + 1);
        }
    }
    num_plugins = 0;
}

// FIXME reload is broken because console lib does not provide a way to unregister commands
int cmd_plugins_reload(console_ctx_t *ctx, cmd_signature_t *reg) {
    EMPTY_CMD_SETUP("Reload plugins from the json configuration file");

    unload_plugins();
    load_plugins();
    console_printf("Reloaded %d plugins\n", (int)num_plugins);

    return CONSOLE_OK;
}

int cmd_plugins_show(console_ctx_t *ctx, cmd_signature_t *reg) {
    EMPTY_CMD_SETUP("Show all currently configured plugins");

    char plugin_info[MAX_PLUGIN_STRING_SIZE];
    console_printf("Currently loaded %d plugins\n", (int)num_plugins);
    for(size_t i = 0; i < num_plugins; i++) {
        if(plugins[i].so_handle) {
            get_plugin_info(i, plugin_info);
            console_printf("%s\n", plugin_info);
        }
    }

    return CONSOLE_OK;
}

void register_plugin_cmds() {
    console_group_add("plugins", "Plugin commands");
    //console_cmd_register(cmd_plugins_reload, "plugins reload");
    console_cmd_register(cmd_plugins_show, "plugins show");
}
