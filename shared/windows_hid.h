#ifndef SKPASS_WINDOWS_HID_H
#define SKPASS_WINDOWS_HID_H
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include "hid_wire.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    wchar_t path[1024], serial[128];
} skpass_hid_device_t;
typedef struct {
    HANDLE handle;
    OVERLAPPED read;
    uint8_t input[64];
    int pending;
    uint8_t sequence;
    uint32_t session;
} skpass_hid_handle_t;
size_t skpass_hid_list(skpass_hid_device_t *out, size_t capacity);
/* 1=open, 0=not present, -1=busy or inaccessible. serial=NULL selects first. */
int skpass_hid_open(skpass_hid_handle_t *h, const wchar_t *serial, wchar_t selected[128]);
void skpass_hid_close(skpass_hid_handle_t *h);
/* Nonblocking, 1=report received, 0=pending, -1=disconnected/error. */
int skpass_hid_read(skpass_hid_handle_t *h, uint8_t out[64]);
int skpass_hid_send(skpass_hid_handle_t *h, const uint8_t *frame, size_t size, DWORD timeout_ms);
#ifdef __cplusplus
}
#endif
#endif
