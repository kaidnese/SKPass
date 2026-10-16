#include "mifare_reader.h"
#include "skpass_usb.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

#define MIFARE_ATTEMPTS 3
#define MIFARE_GAP_MS 5

static void frame_gap(void)
{
    /* Round UP, with an extra tick for the partially elapsed current tick.
     * pdMS_TO_TICKS(5) alone is zero at the project's 100 Hz tick rate.
     * At 100 Hz this yields approximately 10..20 ms, without busy-waiting. */
    vTaskDelay((MIFARE_GAP_MS + portTICK_PERIOD_MS - 1) /
               portTICK_PERIOD_MS + 1);
}

static unsigned long time_ms(void)
{
    return (unsigned long)xTaskGetTickCount() * portTICK_PERIOD_MS;
}

static void diagnostic(const char *text)
{
    ESP_LOGI("AIME_READ", "%s", text);
    SKPass_USB_Debug(text);
}

static bool read_block(i2c_master_bus_handle_t bus, uint8_t block,
                       unsigned attempt, unsigned limit, uint8_t out[16])
{
    /* ElectronicCats MifareClassic_read_block: 10 30 block. */
    uint8_t cmd[] = {0x10, 0x30, block}, rx[255]; size_t n = 0;
    frame_gap();
    unsigned long started = time_ms();
    esp_err_t ret = Pn7150_Tag_Transceive(bus, cmd, sizeof(cmd), rx, sizeof(rx), &n);
    char bytes[511] = "", text[640];
    for (size_t i = 0; i < n; ++i) sprintf(bytes + i * 2, "%02X", rx[i]);
    snprintf(text, sizeof(text),
             "MIFARE block=%02X result=%s raw=%s attempt=%u/%u start_ms=%lu",
             block, esp_err_to_name(ret), bytes, attempt, limit, started);
    diagnostic(text);
    if (ret != ESP_OK || n != 18 || rx[0] != 0x10 || rx[17]) return false;
    memcpy(out, rx + 1, 16);
    return true;
}

bool Mifare_Read_Unauthenticated(i2c_master_bus_handle_t bus, uint8_t out[48])
{
    /* Preserve the existing probe for cards that expose data without auth.
     * A normal Classic NACK here should lead to authentication, not retries. */
    for (uint8_t b = 0; b < 3; ++b) {
        if (!read_block(bus, b, 1, 1, out + 16 * b)) return false;
    }
    return true;
}

static uint8_t authenticate(i2c_master_bus_handle_t bus,
    const pn7150_card_info_t *card, const uint8_t key[6],
    const char *key_slot, unsigned attempt, bool reselect)
{
    /* Do not blindly resend DATA after timeout: it has no request ID, and a
     * late read response could otherwise be assigned to the next block.
     * Sleep + select drains old DATA, waits for deactivation/activation and
     * checks the exact UID/interface/protocol. This also renews Crypto1 and
     * RF connection credits without toggling VEN or resetting USB. */
    esp_err_t ret = ESP_OK;
    if (reselect) {
        frame_gap();
        ret = Pn7150_ReActivate(bus, card);
        if (ret != ESP_OK) {
            char text[128];
            snprintf(text, sizeof(text), "MIFARE RESELECT result=%s; soft retry aborted",
                     esp_err_to_name(ret));
            diagnostic(text);
            return 0x13;
        }
    }
    /* On the first authentication attempt the RF activation notification has
     * just arrived, so send the command immediately.  Keep the inter-frame
     * gap after a reselect because that path drains RF_DEACTIVATE/ACTIVATE
     * notifications before renewing Crypto1. */
    if (reselect) frame_gap();
    uint8_t auth[9] = {0x40, 0x00, 0x10}, rx[255]; size_t n = 0;
    memcpy(auth + 3, key, 6);
    unsigned long started = time_ms();
    ret = Pn7150_Tag_Transceive(bus, auth, sizeof(auth), rx, sizeof(rx), &n);
    char text[192];
    snprintf(text, sizeof(text),
        "MIFARE AUTH key_slot=%s result=%s size=%u status=%02X attempt=%u/%u start_ms=%lu",
        key_slot, esp_err_to_name(ret), (unsigned)n, n ? rx[n-1] : 0xFF,
        attempt, MIFARE_ATTEMPTS, started);
    diagnostic(text);
    return ret == ESP_OK && n == 2 && rx[0] == 0x40 && rx[1] == 0 ? 0 : 0x12;
}

uint8_t Mifare_Read_Authenticated(i2c_master_bus_handle_t bus,
    const pn7150_card_info_t *card, const uint8_t key[6],
    const char *key_slot, uint8_t out[48])
{
    /* Never expose partially recovered sector contents as a full card. */
    memset(out, 0, 48);
    uint8_t status = 0x12;
    for (unsigned attempt = 1; attempt <= MIFARE_ATTEMPTS; ++attempt) {
        /* ElectronicCats' reference flow authenticates immediately after
         * RF activation.  Do not force a Sleep/Select cycle before the first
         * attempt: CUID/Gen2 cards are especially sensitive to that extra
         * transition. */
        status = authenticate(bus, card, key, key_slot, attempt, attempt > 1);
        /* A weak edge-of-field exchange can return the same NACK-looking
         * status as a failed key.  Re-select and retry all non-success
         * attempts; the configured key and fallback key are still tried by
         * the caller, while a genuine success exits immediately. */
        if (status == 0) break;
        if (status == 0x13 && attempt < MIFARE_ATTEMPTS)
            diagnostic("MIFARE RETRY authentication action=reselect_reauth");
    }
    if (status) return status;

    for (uint8_t b = 0; b < 3; ++b) {
        bool complete = false;
        for (unsigned attempt = 1; attempt <= MIFARE_ATTEMPTS; ++attempt) {
            if (attempt > 1) {
                char text[128];
                snprintf(text, sizeof(text),
                    "MIFARE RETRY block=%02X attempt=%u/%u action=reselect_reauth",
                    b, attempt, MIFARE_ATTEMPTS);
                diagnostic(text);
                status = authenticate(bus, card, key, key_slot, attempt, true);
                if (status == 0x13) break;
                if (status) continue;
            }
            if (read_block(bus, b, attempt, MIFARE_ATTEMPTS, out + 16 * b)) {
                complete = true;
                if (attempt > 1) diagnostic("MIFARE RETRY recovered; continuing sector read");
                break;
            }
        }
        if (!complete) {
            memset(out, 0, 48);
            diagnostic("MIFARE RETRY exhausted or RF session lost; requesting recovery");
            return 0x13;
        }
    }
    return 0;
}
