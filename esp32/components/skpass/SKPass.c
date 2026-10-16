#include "SKPass.h"
#include <string.h>

const uint8_t SKPass_HID_ReportDescriptor[] = {
    0x06,0xCA,0xFF, 0x09,0x01, 0xA1,0x01,
    0x85,0x01, 0x06,0xCA,0xFF, 0x09,0x41, 0x15,0x00, 0x25,0xFF,
    0x75,0x08, 0x95,0x08, 0x81,0x02,
    0x85,0x02, 0x06,0xCA,0xFF, 0x09,0x42, 0x15,0x00, 0x25,0xFF,
    0x75,0x08, 0x95,0x08, 0x81,0x02, 0xC0
};
const uint16_t SKPass_HID_ReportDescriptorSize =
    (uint16_t)sizeof(SKPass_HID_ReportDescriptor);

void SKPass_Init(SKPass *c, SKPass_SendReport sender, void *context) {
    if (!c) return;
    c->send_report = sender;
    c->context = context;
    c->suppress_ms = 3000u;
    c->last_send_ms = 0u;
    c->has_sent = 0u;
}

void SKPass_SetSuppressTime(SKPass *c, uint32_t milliseconds) {
    if (c) c->suppress_ms = milliseconds;
}

int SKPass_Send(SKPass *c, uint8_t type, const uint8_t id[8], uint32_t now_ms) {
    uint8_t report[SKPASS_REPORT_BYTES];
    if (!c || !c->send_report || !id) return -1;
    if (c->has_sent && (uint32_t)(now_ms - c->last_send_ms) < c->suppress_ms)
        return 0;
    report[0] = type;
    memcpy(&report[1], id, SKPASS_CARD_BYTES);
    int result = c->send_report(report, c->context);
    if (result >= 0) {
        c->last_send_ms = now_ms;
        c->has_sent = 1u;
    }
    return result;
}

void SKPass_FromFeliCa(SKPass *c, const uint8_t idm[8], uint32_t now_ms) {
    (void)SKPass_Send(c, SKPASS_TYPE_FELICA_OR_ISO14443, idm, now_ms);
}

void SKPass_FromISO14443(SKPass *c, const uint8_t *uid, uint8_t length,
                         uint32_t now_ms) {
    uint8_t id[8] = {0};
    if (!uid) return;
    if (length > 8u) length = 8u;
    memcpy(id, uid, length);
    /* Matches the reference firmware's compatibility rule. */
    id[0] &= 0x0Fu;
    (void)SKPass_Send(c, SKPASS_TYPE_FELICA_OR_ISO14443, id, now_ms);
}

void SKPass_FromISO15693(SKPass *c, const uint8_t uid_lsb_first[8],
                         uint32_t now_ms) {
    uint8_t id[8];
    if (!uid_lsb_first) return;
    for (uint8_t i = 0; i < 8u; ++i) id[i] = uid_lsb_first[7u - i];
    (void)SKPass_Send(c, SKPASS_TYPE_ISO15693, id, now_ms);
}
