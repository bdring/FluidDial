#ifdef ESP32
#    include "nvs_flash.h"
#else
typedef const char* nvs_handle_t;
void                nvs_get_str(nvs_handle_t handle, const char* name, char* value, size_t* len);
void                nvs_set_str(nvs_handle_t handle, const char* name, const char* value);
void                nvs_get_i32(nvs_handle_t handle, const char* name, int* value);
void                nvs_set_i32(nvs_handle_t handle, const char* name, int value);
#endif

nvs_handle_t nvs_init(const char* name);
