/* Test double for ESP-IDF esp_err.h.
 *
 * This header exists ONLY for host-side compilation of firmware/main/core
 * on PC. On device, the real ESP-IDF esp_err.h is used. This shim must
 * define nothing beyond what the core code actually references.
 *
 * Error constants taken from ESP-IDF v5.5.5:
 * components/esp_common/include/esp_err.h
 *
 * IMPORTANT: Do not modify values from memory. If these need updating,
 * read the real header file and verify each value. */
#pragma once

typedef int esp_err_t;

#define ESP_OK                 0x0000
#define ESP_FAIL               -1
#define ESP_ERR_NO_MEM         0x0101
#define ESP_ERR_INVALID_ARG    0x0102
#define ESP_ERR_INVALID_STATE  0x0103
#define ESP_ERR_INVALID_SIZE   0x0104
#define ESP_ERR_NOT_FOUND      0x0105
#define ESP_ERR_NOT_SUPPORTED  0x0106
#define ESP_ERR_TIMEOUT        0x0107

/* Static assertions to catch accidental modifications. These state the intent
 * independently of the values, making editorial mistakes visible at build time. */
_Static_assert(ESP_OK == 0x0000,          "ESP_OK doit valoir 0x0000 comme dans ESP-IDF");
_Static_assert(ESP_FAIL == -1,            "ESP_FAIL vaut -1 dans ESP-IDF v5.5.5, pas +1");
_Static_assert(ESP_ERR_NO_MEM == 0x0101,  "ESP_ERR_NO_MEM doit valoir 0x0101");
_Static_assert(ESP_ERR_INVALID_ARG == 0x0102, "ESP_ERR_INVALID_ARG doit valoir 0x0102");
_Static_assert(ESP_ERR_INVALID_STATE == 0x0103, "ESP_ERR_INVALID_STATE doit valoir 0x0103");
_Static_assert(ESP_ERR_INVALID_SIZE == 0x0104, "ESP_ERR_INVALID_SIZE doit valoir 0x0104");
_Static_assert(ESP_ERR_NOT_FOUND == 0x0105, "ESP_ERR_NOT_FOUND doit valoir 0x0105 (non 0x0104)");
_Static_assert(ESP_ERR_NOT_SUPPORTED == 0x0106, "ESP_ERR_NOT_SUPPORTED doit valoir 0x0106 (non 0x0105)");
_Static_assert(ESP_ERR_TIMEOUT == 0x0107, "ESP_ERR_TIMEOUT doit valoir 0x0107");
