#pragma once

/* Sauvetage automatique : voir rescue.c pour le principe. */

#include <stdint.h>

#include "esp_err.h"

esp_err_t rescue_arm(uint32_t delai_ms);
void rescue_disarm(void);
esp_err_t rescue_switch_to_other_slot(void);
