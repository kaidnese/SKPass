#include "windows_hid.h"
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static int matches(HANDLE file, wchar_t serial[128]) {
    HIDD_ATTRIBUTES attrs = {0}; attrs.Size = sizeof(attrs);
    if (!HidD_GetAttributes(file, &attrs) || attrs.VendorID != 0x1CCF || attrs.ProductID != 0x5252) return 0;
    PHIDP_PREPARSED_DATA data = NULL;
    HIDP_CAPS caps;
    if (!HidD_GetPreparsedData(file, &data)) return 0;
    NTSTATUS status = HidP_GetCaps(data, &caps); HidD_FreePreparsedData(data);
    if (status != HIDP_STATUS_SUCCESS || caps.UsagePage != SKPASS_HID_USAGE_PAGE ||
        caps.Usage != SKPASS_HID_USAGE || caps.InputReportByteLength != 64 || caps.OutputReportByteLength != 64) return 0;
    serial[0] = 0;
    HidD_GetSerialNumberString(file, serial, 128 * sizeof(wchar_t)); serial[127] = 0;
    return 1;
}

size_t skpass_hid_list(skpass_hid_device_t *out, size_t capacity) {
    GUID guid; HidD_GetHidGuid(&guid);
    HDEVINFO info = SetupDiGetClassDevsW(&guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (info == INVALID_HANDLE_VALUE) return 0;
    size_t count = 0;
    SP_DEVICE_INTERFACE_DATA itf = {0}; itf.cbSize = sizeof(itf);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(info, NULL, &guid, i, &itf); ++i) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(info, &itf, NULL, 0, &needed, NULL);
        if (!needed) continue;
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail = malloc(needed);
        if (!detail) break;
        detail->cbSize = sizeof(*detail);
        if (SetupDiGetDeviceInterfaceDetailW(info, &itf, detail, needed, NULL, NULL) && wcslen(detail->DevicePath) < 1024) {
            /* Zero-access metadata handles do not consume reports and can
             * identify an interface even while another process owns its I/O. */
            HANDLE f = CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   NULL, OPEN_EXISTING, 0, NULL);
            wchar_t serial[128];
            if (f != INVALID_HANDLE_VALUE) {
                if (matches(f, serial)) {
                    if (out && count < capacity) { wcscpy(out[count].path, detail->DevicePath); wcscpy(out[count].serial, serial); }
                    ++count;
                }
                CloseHandle(f);
            }
        }
        free(detail);
    }
    SetupDiDestroyDeviceInfoList(info); return count;
}

int skpass_hid_open(skpass_hid_handle_t *h, const wchar_t *serial, wchar_t selected[128]) {
    skpass_hid_device_t devices[16];
    size_t n = skpass_hid_list(devices, 16);
    int result = 0;
    if (n > 16) n = 16;
    for (size_t i = 0; i < n; ++i) {
        if (serial && *serial && wcscmp(serial, devices[i].serial)) continue;
        HANDLE f = CreateFileW(devices[i].path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                               OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
        if (f == INVALID_HANDLE_VALUE) { result = -1; continue; }
        memset(h, 0, sizeof(*h)); h->handle = f;
        LARGE_INTEGER counter; QueryPerformanceCounter(&counter);
        h->session = (uint32_t)counter.QuadPart ^ (uint32_t)(counter.QuadPart >> 32) ^ GetCurrentProcessId();
        if (!h->session) h->session = 1;
        h->read.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!h->read.hEvent) { CloseHandle(f); h->handle = NULL; result = -1; continue; }
        HidD_SetNumInputBuffers(f, 256);
        HidD_FlushQueue(f);
        if (selected) wcscpy(selected, devices[i].serial);
        return 1;
    }
    return result;
}

void skpass_hid_close(skpass_hid_handle_t *h) {
    if (!h || !h->handle) return;
    if (h->pending) {
        CancelIoEx(h->handle, &h->read);
        DWORD got; GetOverlappedResult(h->handle, &h->read, &got, TRUE);
    }
    CloseHandle(h->handle); CloseHandle(h->read.hEvent); memset(h, 0, sizeof(*h));
}

int skpass_hid_read(skpass_hid_handle_t *h, uint8_t out[64]) {
    if (!h || !h->handle) return -1;
    DWORD got = 0;
    if (!h->pending) {
        ResetEvent(h->read.hEvent);
        if (ReadFile(h->handle, h->input, sizeof(h->input), &got, &h->read)) {
            if (got != 64) return -1;
            memcpy(out, h->input, 64); return 1;
        }
        if (GetLastError() != ERROR_IO_PENDING) return -1;
        h->pending = 1;
    }
    if (!GetOverlappedResult(h->handle, &h->read, &got, FALSE)) {
        if (GetLastError() == ERROR_IO_INCOMPLETE) return 0;
        h->pending = 0; return -1;
    }
    h->pending = 0;
    if (got != 64) return -1;
    memcpy(out, h->input, 64); return 1;
}

int skpass_hid_send(skpass_hid_handle_t *h, const uint8_t *frame, size_t size, DWORD timeout_ms) {
    if (!h || !h->handle || !skpass_hid_fragments(size)) return 0;
    DWORD started = GetTickCount();
    uint8_t sequence = ++h->sequence;
    for (size_t i = 0; i < skpass_hid_fragments(size); ++i) {
        uint8_t report[64]; skpass_hid_fragment(frame, size, sequence, (uint8_t)i, report);
        OVERLAPPED ov = {0}; ov.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!ov.hEvent) return 0;
        DWORD sent = 0; BOOL ok = WriteFile(h->handle, report, 64, &sent, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            DWORD elapsed = GetTickCount() - started;
            DWORD wait = elapsed < timeout_ms ? timeout_ms - elapsed : 0;
            if (WaitForSingleObject(ov.hEvent, wait) != WAIT_OBJECT_0) CancelIoEx(h->handle, &ov);
            ok = GetOverlappedResult(h->handle, &ov, &sent, TRUE);
        }
        CloseHandle(ov.hEvent);
        if (!ok || sent != 64) return 0;
    }
    return 1;
}
