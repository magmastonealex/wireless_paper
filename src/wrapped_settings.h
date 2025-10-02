#include <stdint.h>
#include <stddef.h>


// This is a wrapper around Zephyr's settings API which allows for treating it as a generic key-value store.
int wrapped_settings_init(); // performs any required initialization tasks.

// wrapped_settings_get_int retrieves the value stored at "key" and inserts it into "val".
int wrapped_settings_get_int(const char* key, int* val);

// Retrieve some raw data under the settings key "key", storing it in "data". The maximum size of data is in max_size, and the actual size of what was read is stored in actual_size.
int wrapped_settings_get_raw(const char* key, uint8_t *data, size_t max_size, size_t *actual_size);

// wrapped_settings_set_int stores a given integer under the key "key".
int wrapped_settings_set_int(const char* key, int val);

// STore some data under the key "key", of size "size"
int wrapped_settings_set_raw(const char* key, uint8_t *data, size_t size);
