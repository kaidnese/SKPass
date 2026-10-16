#ifndef SKPASS_CARD_EMULATION_H
#define SKPASS_CARD_EMULATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CARD_EMU_UID_BYTES 4
#define CARD_EMU_MAX_NDEF 240
#define CARD_EMU_MAX_RULES 4
#define CARD_EMU_MAX_MATCH 16
#define CARD_EMU_MAX_RESPONSE 32

typedef struct {
    uint8_t match[CARD_EMU_MAX_MATCH];
    uint8_t match_len;
    uint8_t response[CARD_EMU_MAX_RESPONSE];
    uint8_t response_len;
    bool prefix;
} card_emu_rule_t;

typedef struct {
    uint8_t uid[CARD_EMU_UID_BYTES];
    uint8_t ndef[CARD_EMU_MAX_NDEF];
    uint16_t ndef_len;
    bool write_enabled;
    uint8_t fallback[2];
    card_emu_rule_t rules[CARD_EMU_MAX_RULES];
    uint8_t rule_count;
} card_emu_config_t;

typedef struct {
    card_emu_config_t config;
    uint8_t selected_file; /* 1=CC, 2=NDEF */
    bool app_selected;
    uint8_t staged[CARD_EMU_MAX_NDEF];
    uint16_t staged_len;
    uint16_t expected_len;
    uint8_t written[(CARD_EMU_MAX_NDEF + 7) / 8];
    bool update_active;
} card_emu_t;

void CardEmu_Init(card_emu_t *emu, const card_emu_config_t *config);
void CardEmu_Set_Config(card_emu_t *emu, const card_emu_config_t *config);
void CardEmu_Reset_Session(card_emu_t *emu);
size_t CardEmu_Process_Apdu(card_emu_t *emu, const uint8_t *apdu, size_t apdu_len,
                            uint8_t *response, size_t response_capacity,
                            bool *ndef_updated);
size_t CardEmu_Build_Ndef(uint8_t kind, const char *text, uint8_t *out, size_t capacity);
bool CardEmu_Is_Ndef(const uint8_t *data, size_t len);

#endif
