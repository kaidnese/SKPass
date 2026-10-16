#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Enter MSC mode. A verified update restarts automatically; reset cancels
 * an unfinished/failed update and returns to the selected firmware. */
esp_err_t Uf2_Update_Enter(void);
bool Uf2_Update_Is_Active(void);
bool Uf2_Update_Is_Requested(void);
bool Uf2_Update_Boot(void);
bool Uf2_Update_Has_Error(void);

#ifdef __cplusplus
}
#endif
