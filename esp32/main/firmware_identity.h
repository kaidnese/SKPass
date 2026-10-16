#ifndef SKPASS_FIRMWARE_IDENTITY_H
#define SKPASS_FIRMWARE_IDENTITY_H
#include <stdint.h>

/* Read the complete esptool-patched digest from the application descriptor.
 * esp_app_get_elf_sha256() is limited by CONFIG_APP_RETRIEVE_LEN_ELF_SHA,
 * regardless of how large the caller's destination buffer is.
 * Volatile loads prevent optimization of bytes patched after compilation. */
static inline void SKPass_Format_Firmware_Hash(const volatile uint8_t digest[32], char out[65])
{
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; ++i) {
        uint8_t byte = digest[i];
        out[i * 2] = hex[byte >> 4];
        out[i * 2 + 1] = hex[byte & 15];
    }
    out[64] = 0;
}
#endif
