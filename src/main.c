#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

#include "zygisk.h"

#include "logger.h"

#include <jni.h>

struct api_table *api_table;
JNIEnv *java_env;

const char *manager_pkgs[] = {
  "com.topjohnwu.magisk"
  "io.github.vvb2060.magisk",
  "com.rifsxd.ksunext"
};
int manager_count = sizeof(manager_pkgs) / sizeof(manager_pkgs[0]);

char *get_string_data(JNIEnv *env, jstring *value) {
  const char *str = (*env)->GetStringUTFChars(env, *value, 0);
  if (str == NULL) return NULL;

  char *out = strdup(str);
  (*env)->ReleaseStringUTFChars(env, *value, str);

  return out;
}

char *get_package_name(char *data_dir, char *process_name) {
  struct stat st;
  if (stat(data_dir, &st) == -1) {
    LOGW("Failed to stat data directory: %s", data_dir);

    return process_name;
  }

  char *last_slash = strrchr(data_dir, '/');
  if (!last_slash) {
    LOGW("Failed to parse package name from data directory: %s", data_dir);

    return process_name;
  }

  char *pkg_name = last_slash + 1;
  LOGD("Package name: %s for process: %s", pkg_name, process_name);

  return pkg_name;
}

/* INFO: This is the beginning of zygisk's functions
those are triggered by it on each stage:
 * .preAppSpecialize: Called on app open; runs with elevated privileges before the process is sandboxed.
 * .postAppSpecialize: Called after app sandboxing; runs within the app's restricted security context.
 * .preServerSpecialize: Called before system_server forks; used for system-level modifications.
 * .postServerSpecialize: Called after system_server is specialized; runs with system-level privileges.
*/
void pre_app_specialize(void *mod_data, struct AppSpecializeArgs *args) {
  (void) mod_data;
  api_table->setOption(api_table->impl, DLCLOSE_MODULE_LIBRARY);

  char *process_name = get_string_data(java_env, args->nice_name);
  char *data_dir = get_string_data(java_env, args->app_data_dir);

  char *package_name;
  if (data_dir) {
    package_name = get_package_name(data_dir, process_name);
  } else {
    package_name = process_name;
  }

  if (!process_name && !data_dir) {
    LOGE("Failed to get process name and data dir");
    goto free;
  }

  uint32_t flags = api_table->getFlags(api_table->impl);
  //if ((flags & PROCESS_ON_DENYLIST) == PROCESS_ON_DENYLIST)
    //goto free;

  if ((flags & PROCESS_GRANTED_ROOT) == PROCESS_GRANTED_ROOT)
    goto set;

  for (int i = 0; i < manager_count; i++) {
    if (package_name == manager_pkgs[i]) goto set;
  }
  goto free;

set:
  setenv("ZYGISK_ENABLED", "1", 1);
  LOGI("Set ZYGISK_ENABLED for %s", package_name);

free:
  if (process_name) free(process_name);
  if (data_dir) free(data_dir);
}

void post_app_specialize(void *mod_data, const struct AppSpecializeArgs *args) {
  (void) mod_data; (void) args;
}

void pre_server_specialize(void *mod_data, struct ServerSpecializeArgs *args) {
  (void) mod_data; (void) args;
  api_table->setOption(api_table->impl, DLCLOSE_MODULE_LIBRARY);
}

void post_server_specialize(void *mod_data, const struct ServerSpecializeArgs *args) {
  (void) mod_data; (void) args;
}

void zygisk_module_entry(struct api_table *table, JNIEnv *env) {
  api_table = table;
  java_env = env;

  static struct module_abi abi = {
    .api_version = 5,
    .impl = (void *)"zygisk_env",
    .preAppSpecialize = pre_app_specialize,
    .postAppSpecialize = post_app_specialize,
    .preServerSpecialize = pre_server_specialize,
    .postServerSpecialize = post_server_specialize
  };

  if (!table->registerModule(table, &abi)) return;
}

void zygisk_companion_entry(int fd) {
  (void) fd;
}
