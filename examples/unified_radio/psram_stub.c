// Linker wrappers to disable PSRAM initialization on boards without PSRAM.
// Plain weak replacements do not work here: the ESP-IDF archive contains
// strong definitions and the linker discards the weak stubs. The 8 MB target
// uses --wrap for these symbols so external SDK calls resolve here explicitly.

#include <stdbool.h>

#define ESP_ERR_NO_MEM  0x101

int __wrap_esp_spiram_init(void) {
    return ESP_ERR_NO_MEM;
}

void __wrap_esp_spiram_init_cache(void) {
}

bool __wrap_esp_spiram_test(void) {
    return false;
}

int __wrap_esp_spiram_add_to_heapalloc(void) {
    return ESP_ERR_NO_MEM;
}
