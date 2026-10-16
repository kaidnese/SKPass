#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
bool cards_json_parse(const char *text,size_t size,uint8_t code[10],unsigned *key);
