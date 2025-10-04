#include "wrapped_settings.h"

#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(wrapped_settings, LOG_LEVEL_INF);

#define KVS_PREFIX "kvs/"
#define MAX_KEY_LEN 64

static int build_key(const char* key, char* full_key, size_t full_key_size)
{
    if (!key || !full_key) {
        return -EINVAL;
    }

    size_t key_len = strlen(key);
    size_t prefix_len = strlen(KVS_PREFIX);

    if (prefix_len + key_len + 1 > full_key_size) {
        LOG_ERR("Key too long: %s", key);
        return -ENOMEM;
    }

    snprintf(full_key, full_key_size, "%s%s", KVS_PREFIX, key);
    return 0;
}

int wrapped_settings_init(void)
{
    int ret;

    ret = settings_subsys_init();
    if (ret) {
        LOG_ERR("Failed to initialize settings subsystem: %d", ret);
        return ret;
    }

    LOG_INF("Settings wrapper initialized");
    return 0;
}
// borrowed idea from settings_shell for raw settings reads and writes.
struct settings_read_callback_params {
    uint8_t *data;
    size_t data_max_len;
    ssize_t data_real_size;
    bool value_found;
    bool value_truncated;
};

static int settings_read_callback(const char* key, size_t len, settings_read_cb read_cb, void *cb_arg, void *param) {
    struct settings_read_callback_params *params = param;
    // ignore descendents
    if (settings_name_next(key, NULL) != 0) {
            return 0;
    }

    
    if (len > params->data_max_len) {
        params->value_truncated=true;
        params->data_real_size = len;
        return 0;
    }

    ssize_t num_read_bytes = MIN(len, params->data_max_len);
    num_read_bytes = read_cb(cb_arg, params->data, num_read_bytes);
    if (num_read_bytes < 0) {
        LOG_ERR("failed to read settings via callback: %d", num_read_bytes);
        return 0;
    }

    params->value_found = true;
    params->data_real_size = num_read_bytes;

    return 0;
}
 
int wrapped_settings_get_raw(const char* key, uint8_t *data, size_t max_size, size_t *actual_size)
{
    char full_key[MAX_KEY_LEN]; 
    int ret;

    struct settings_read_callback_params ctx = {
        .data = data,
        .data_max_len = max_size,
        .data_real_size = 0,
        .value_found = false,
        .value_truncated = false
    };

    ret = build_key(key, full_key, sizeof(full_key));
    if (ret) {
        return ret;
    }

    ret = settings_load_subtree_direct(full_key, settings_read_callback, &ctx);
    if (ret) {
        LOG_ERR("Failed to load from key '%s': %d", full_key, ret);
        return ret;
    }

    if (!ctx.value_found) {
        LOG_ERR("Key '%s' not found", full_key);
        return -ENOENT;
    }

    if (ctx.value_truncated) {
        LOG_ERR("input array too small for result (actually %zd vs. %zu)", ctx.data_real_size, ctx.data_max_len);
        return -ENOMEM;
    }
    if (actual_size != NULL) {
        *actual_size = (size_t)ctx.data_real_size;
    }
    
    LOG_INF("Loaded %zu bytes from key '%s'", *actual_size, full_key);
    return 0;
}

int wrapped_settings_set_raw(const char* key, uint8_t *data, size_t size)
{
    char full_key[MAX_KEY_LEN];
    int ret;

    if (!key || !data) {
        return -EINVAL;
    }

    ret = build_key(key, full_key, sizeof(full_key));
    if (ret) {
        return ret;
    }

    ret = settings_save_one(full_key, data, size);

    if (ret) {
        LOG_ERR("Failed to save %zu bytes to key '%s': %d", size, full_key, ret);
        return ret;
    }

    LOG_INF("Saved %zu bytes to key '%s'", size, full_key);
    return 0;
}
