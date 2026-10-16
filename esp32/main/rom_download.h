#pragma once
#include <stdbool.h>
#include "esp_err.h"

/* No eFuse is written. Both USB ROM and UART0 use the same ROM loader. */
esp_err_t Rom_Download_Enter(void);
bool Rom_Download_Is_Requested(void);
