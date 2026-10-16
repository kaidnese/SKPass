#ifndef CARD_ID_H
#define CARD_ID_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CARD_ID_BYTES 8u

typedef enum {
    CARD_ID_NONE = 0,
    CARD_ID_FELICA = 1,
    CARD_ID_ISO15693 = 2
} CardID_Type;

typedef struct {
    CardID_Type type;
    uint8_t bytes[CARD_ID_BYTES];
    uint8_t length;
} CardID_Result;

/*
 * Reader-specific callbacks perform RF setup and the protocol exchange.
 * Return 8 on success, or 0 when no matching card was found / on error.
 * FeliCa callback returns IDm in response order. ISO15693 callback returns
 * UID in the reader library's raw order; CardID_Read reverses it for output.
 */
typedef uint8_t (*CardID_ReadRaw)(void *reader, uint8_t out[CARD_ID_BYTES]);

typedef struct {
    CardID_ReadRaw read_felica_idm;
    CardID_ReadRaw read_iso15693_uid;
    void *reader;
} CardID_Reader;

/* Try FeliCa first, then ISO15693. Returns 1 if a card was read, else 0. */
uint8_t CardID_Read(const CardID_Reader *reader, CardID_Result *result);

#ifdef __cplusplus
}
#endif

#endif
