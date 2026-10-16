#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool spad_access_code(const uint8_t cipher[16], uint8_t code[10]);
#ifdef __cplusplus
}
#endif
