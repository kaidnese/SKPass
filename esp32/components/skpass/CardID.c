#include "CardID.h"
#include <string.h>

uint8_t CardID_Read(const CardID_Reader *reader, CardID_Result *result) {
    uint8_t raw[CARD_ID_BYTES] = {0};
    uint8_t length;

    if (!reader || !result) return 0;
    memset(result, 0, sizeof(*result));

    if (reader->read_felica_idm) {
        length = reader->read_felica_idm(reader->reader, raw);
        if (length == CARD_ID_BYTES) {
            result->type = CARD_ID_FELICA;
            result->length = CARD_ID_BYTES;
            memcpy(result->bytes, raw, CARD_ID_BYTES);
            return 1;
        }
    }

    memset(raw, 0, sizeof(raw));
    if (reader->read_iso15693_uid) {
        length = reader->read_iso15693_uid(reader->reader, raw);
        if (length == CARD_ID_BYTES) {
            result->type = CARD_ID_ISO15693;
            result->length = CARD_ID_BYTES;
            for (uint8_t i = 0; i < CARD_ID_BYTES; ++i)
                result->bytes[i] = raw[CARD_ID_BYTES - 1u - i];
            return 1;
        }
    }

    return 0;
}
