#ifndef SKPASS_H
#define SKPASS_H

/*
 * SKPass's platform-independent adapter for the CardIO HID protocol from
 * the original PN5180-cardio project. Naming changed; wire format preserved.
 *
 * A SKPass input report is nine bytes: report/type (1 byte) followed by an
 * eight byte card identifier. The transport (usually USB HID interrupt IN)
 * is supplied by the application through SKPass_Init().
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SKPASS_CARD_BYTES 8u
#define SKPASS_REPORT_BYTES 9u
#define SKPASS_TYPE_ISO15693 1u
#define SKPASS_TYPE_FELICA_OR_ISO14443 2u

typedef int (*SKPass_SendReport)(const uint8_t report[SKPASS_REPORT_BYTES],
                                 void *context);

typedef struct {
    SKPass_SendReport send_report;
    void *context;
    uint32_t suppress_ms;
    uint32_t last_send_ms;
    uint8_t has_sent;
} SKPass;

void SKPass_Init(SKPass *skpass, SKPass_SendReport sender, void *context);
void SKPass_SetSuppressTime(SKPass *skpass, uint32_t milliseconds);

/* Build and send a report. Returns sender's result, or -1 if unconfigured. */
int SKPass_Send(SKPass *skpass, uint8_t type,
                const uint8_t card_id[SKPASS_CARD_BYTES],
                uint32_t now_ms);

/* Card number adapters used by the reference firmware. */
void SKPass_FromFeliCa(SKPass *skpass, const uint8_t idm[8], uint32_t now_ms);
void SKPass_FromISO14443(SKPass *skpass, const uint8_t *uid,
                         uint8_t uid_length, uint32_t now_ms);
void SKPass_FromISO15693(SKPass *skpass, const uint8_t uid_lsb_first[8],
                         uint32_t now_ms);

/* The HID report descriptor from SKPASSHID.cpp (42 bytes). */
#define SKPASS_HID_REPORT_DESCRIPTOR_BYTES 42u
extern const uint8_t SKPass_HID_ReportDescriptor[SKPASS_HID_REPORT_DESCRIPTOR_BYTES];
extern const uint16_t SKPass_HID_ReportDescriptorSize;

#ifdef __cplusplus
}
#endif

#endif
