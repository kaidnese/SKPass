#include "aime_reader.h"
#include "mifare_reader.h"
#include "skpass_usb.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

static void hex(const uint8_t *data, size_t size, char *out)
{
    for (size_t i = 0; i < size; ++i) sprintf(out + i * 2, "%02X", data[i]);
    out[size * 2] = 0;
}

/* Only Read Without Encryption is used. No RC writes or key-block writes. */
static esp_err_t felica_read(i2c_master_bus_handle_t bus, const uint8_t idm[8],
                             const uint8_t svc[2], uint8_t block, uint8_t data[16])
{
    uint8_t cmd[16] = {16, 0x06};
    memcpy(cmd + 2, idm, 8);
    cmd[10] = 1; cmd[11] = svc[0]; cmd[12] = svc[1];
    cmd[13] = 1; cmd[14] = 0x80; cmd[15] = block;
    uint8_t rx[255]; size_t n = 0;
    esp_err_t ret = Pn7150_Tag_Transceive(bus, cmd, sizeof(cmd), rx, sizeof(rx), &n);
    unsigned s1 = 0xFF, s2 = 0xFF;
    if (ret == ESP_OK) {
        /* PN7150 Frame interface appends an RF status byte; FeliCa's own
         * length excludes it. Accept only exact validated layouts. */
        if (n >= 12 && rx[1] == 0x07 && !memcmp(rx + 2, idm, 8) &&
            (n == rx[0] || (n == (size_t)rx[0] + 1 && rx[n - 1] == 0))) {
            s1 = rx[10]; s2 = rx[11];
            if (!s1 && !s2 && rx[0] == 29 && n >= 29 && rx[12] == 1) {
                memcpy(data, rx + 13, 16);
            } else ret = ESP_ERR_INVALID_RESPONSE;
        } else ret = ESP_ERR_INVALID_RESPONSE;
    }
    char id[17], bytes[33] = "", text[192];
    hex(idm, 8, id);
    if (ret == ESP_OK) hex(data, 16, bytes);
    snprintf(text, sizeof(text), "FELICA idm=%s service=%02X%02X block=%02X status1=%02X status2=%02X result=%s data=%s",
             id, svc[0], svc[1], block, s1, s2, esp_err_to_name(ret), bytes);
    ESP_LOGI("AIME_READ", "%s", text);
    SKPass_USB_Debug(text);
    return ret;
}

void Aime_Read_Card(i2c_master_bus_handle_t bus, const pn7150_card_info_t *card,
                    aime_card_event_t *event)
{
    memset(event, 0, sizeof(*event));
    memcpy(event->idm, card->uid, card->uid_length < 8 ? card->uid_length : 8);
    char physical_uid[21], tag_text[128];
    hex(card->uid, card->uid_length, physical_uid);
    snprintf(tag_text,sizeof(tag_text),"TAG uid=%s mode=%02X protocol=%02X interface=%02X sak=%02X",
             physical_uid,card->mode_technology,card->protocol,card->rf_interface,
             card->sak_valid ? card->sak : 0xFF);
    SKPass_USB_Debug(tag_text);
    if (card->mode_technology == 0x02 && card->uid_length == 8) {
        event->card_type = 0x20;
        if (card->pmm_valid) memcpy(event->pmm, card->pmm, 8);
        else event->status = 0x14;
        char id[17], sys[5] = "----", text[100];
        hex(event->idm, 8, id);
        if (card->system_code_valid) hex(card->system_code, 2, sys);
        snprintf(text, sizeof(text), "CARD idm=%s system=%s pmm_valid=%u", id, sys, card->pmm_valid);
        SKPass_USB_Debug(text);
        static const uint8_t services[4][2] = {{0x0B,0x00},{0x00,0x0B},{0x09,0x00},{0x00,0x09}};
        uint8_t data[16]; bool stop = false;
        for (unsigned phase = 0; phase < 2 && !stop; ++phase) {
            for (unsigned s = 0; s < 4; ++s) {
                esp_err_t ret = felica_read(bus, event->idm, services[s], phase ? 0 : 0x82, data);
                if (ret == ESP_OK && phase && !event->spad_len) {
                    event->spad_len = 16; memcpy(event->spad, data, 16);
                }
                if (ret == ESP_ERR_TIMEOUT || ret == ESP_ERR_INVALID_STATE) { stop = true; break; }
            }
        }
#if CONFIG_SKPASS_FELICA_FULL_SCAN
        /* Read-only diagnostic probes, including the access result for CK
         * 0x87. No authentication secrets are assumed and no blocks written. */
        for (unsigned b = 0; b <= 0x88 && !stop; ++b) {
            if (b > 0x0D && b < 0x80) continue;
            if (b == 0 || b == 0x82) continue;
            for (unsigned s = 0; s < 4; ++s) {
                esp_err_t ret = felica_read(bus, event->idm, services[s], (uint8_t)b, data);
                if (ret == ESP_ERR_TIMEOUT || ret == ESP_ERR_INVALID_STATE) {stop = true; break;}
            }
        }
#endif
        if (!event->spad_len) event->status = 0x11;
        SKPass_USB_Debug(event->spad_len ? "SPAD0 read succeeded; decoding and validation required" :
                          "SPAD0 unavailable: inspect status/transport; authentication requirement is NOT proven");
    } else if (card->mode_technology == 0 &&
               (card->protocol == 0x80 || (card->sak_valid &&
                (card->sak == 0x08 || card->sak == 0x18 || card->sak == 0x09)))) {
        event->card_type = 0x10;
        if (card->rf_interface != 0x80) {event->status = 0x15; return;}
        /* Authenticate immediately after activation.  The upstream PN7150
         * flow does not probe Classic blocks before authentication; an
         * unauthenticated READ can produce a NACK and disturb the Crypto1
         * session, which is particularly unreliable on CUID/Gen2 cards. */
        memset(event->mifare, 0, 48);
        /* PN7150 internal Crypto1 authentication with embedded Key A.
         * Source: ElectronicCats MifareClassic_read_block, Auth[] =
         * {40, block/4, 10, KEY_MFC}. NOT raw RF 60/61 + UID/key. */
        uint8_t key_bytes[6];
        const char *key = CONFIG_SKPASS_MIFARE_KEY_A;
        if (strlen(key) != 12) {event->status = 0x12; return;}
        for (unsigned i = 0; i < 12; ++i) {
            if (!isxdigit((unsigned char)key[i])) {event->status = 0x12; return;}
        }
        for (unsigned i = 0; i < 6; ++i) {
            char pair[3] = {key[i * 2], key[i * 2 + 1], 0}, *end;
            unsigned long v = strtoul(pair, &end, 16);
            if (*end || v > 255) {event->status = 0x12; return;}
            key_bytes[i] = (uint8_t)v;
        }
        /* The failed unauthenticated READ may halt/alter the Classic session.
         * Re-select the same UID before authentication. For cards whose
         * trailer was intentionally preserved, also support factory FF Key A. */
        static const uint8_t factory_key[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        for (unsigned slot=0; slot<2; ++slot) {
            event->status = Mifare_Read_Authenticated(bus, card,
                slot ? factory_key : key_bytes, slot ? "factory_FF" : "configured",
                event->mifare);
            if (!event->status) {event->mifare_len=48;return;}
            /* Once authenticated, a read failure is a session/data problem;
             * retry that block with the same key, not a different key. */
            if (event->status != 0x12 || !memcmp(key_bytes, factory_key, 6)) break;
        }
        memset(event->mifare,0,48);
    } else if (card->mode_technology == 0x06) event->card_type = 0x30;
    else event->status = 0x16;
}

size_t Aime_Encode_Event(const aime_card_event_t *e, uint8_t out[84])
{
    size_t p = 0;
    out[p++] = e->card_type; out[p++] = e->status;
    memcpy(out+p, e->idm, 8); p+=8; memcpy(out+p, e->pmm, 8); p+=8;
    out[p++] = e->spad_len; memcpy(out+p, e->spad, e->spad_len); p+=e->spad_len;
    out[p++] = e->mifare_len; memcpy(out+p, e->mifare, e->mifare_len); p+=e->mifare_len;
    return p;
}
