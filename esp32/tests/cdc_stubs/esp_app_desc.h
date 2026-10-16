#pragma once
#include <stdint.h>
typedef struct {char project_name[32],version[32],date[16],time[16],idf_ver[32];uint8_t app_elf_sha256[32];} esp_app_desc_t;
const esp_app_desc_t *esp_app_get_description(void);
