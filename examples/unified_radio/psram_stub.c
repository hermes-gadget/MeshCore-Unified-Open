// Stub to disable PSRAM initialization on boards without PSRAM hardware.
// The ESP-IDF's pre-compiled spiram.c calls esp_spiram_init() during system init,
// which aborts on failure. This override returns ESP_ERR_NO_MEM, which triggers 
// the graceful "PSRAM not found" path instead of a fatal abort.
// 
// Linked with --allow-multiple-definition so the first definition (this file,
// compiled before the SDK archive) takes precedence over the library.
// Marked weak for safety even though it's not needed with the linker flag.

#include <stdint.h>
#include <stdbool.h>

#define ESP_OK          0
#define ESP_ERR_NO_MEM  0x101

__attribute__((weak)) int esp_spiram_init(void) {
    return ESP_ERR_NO_MEM;
}

__attribute__((weak)) void esp_spiram_init_cache(void) {
}

__attribute__((weak)) bool esp_spiram_test(void) {
    return false;
}

__attribute__((weak)) int esp_spiram_add_to_heapalloc(void) {
    return ESP_ERR_NO_MEM;
}
