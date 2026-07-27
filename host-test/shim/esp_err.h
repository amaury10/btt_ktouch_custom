/* Test double for ESP-IDF esp_err.h.
 *
 * This header exists ONLY for host-side compilation of firmware/main/core
 * on PC. On device, the real ESP-IDF esp_err.h is used. This shim must
 * define nothing beyond what the core code actually references.
 *
 * Error constants copied from ESP-IDF standard definitions. */
#pragma once

typedef int esp_err_t;

#define ESP_OK                 0x0000
#define ESP_FAIL               0x0001
#define ESP_ERR_NO_MEM         0x0101
#define ESP_ERR_INVALID_ARG    0x0102
#define ESP_ERR_INVALID_STATE  0x0103
#define ESP_ERR_NOT_FOUND      0x0104
#define ESP_ERR_NOT_SUPPORTED  0x0105
#define ESP_ERR_TIMEOUT        0x0107
