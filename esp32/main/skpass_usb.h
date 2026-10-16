#ifndef SKPASS_USB_H
#define SKPASS_USB_H

#include "esp_err.h"
#include "pn7150.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Install the ESP-IDF TinyUSB HID device and initialize SKPass. */
esp_err_t SKPass_USB_Init(void);

/**
 * Submit one PN7150 activation to SKPass.
 *
 * The PN7150 driver stores ISO15693 UIDs in canonical (MSB-first) order,
 * while SKPass_Send expects the already-normalized eight-byte payload.  The
 * adapter therefore performs the protocol/type mapping here and avoids a
 * second ISO15693 reversal.
 *
 * The card is copied into a one-entry pending queue.  This means a card
 * detected before USB enumeration is not lost; the USB task retries it after
 * the host mounts.  ESP_OK means that the event was accepted by that queue.
 */
esp_err_t SKPass_USB_SendCard(const pn7150_card_info_t *card_info);
/* Nonblocking ASCII diagnostic frame, independent of the HID queue. */
void SKPass_USB_Debug(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* SKPASS_USB_H */
