/* Journalisation portable pour le code de core/ et apps/.
 *
 * Sur l'appareil, ces macros passent par ESP_LOG*, donc par le relais installé
 * dans netlog.c — et se retrouvent dans /log, seul canal de diagnostic d'un
 * appareil sans port série exploitable. Un printf() ordinaire, lui, contourne
 * ce relais et ne serait visible nulle part.
 *
 * Sur PC (tests hôte, futur simulateur), elles retombent sur printf : il n'y a
 * ni ESP-IDF ni relais, et la sortie va simplement au terminal. */
#pragma once

#ifdef ESP_PLATFORM
#include "esp_log.h"
#define JOURNAL_INFO(tag, ...)   ESP_LOGI(tag, __VA_ARGS__)
#define JOURNAL_ALERTE(tag, ...) ESP_LOGW(tag, __VA_ARGS__)
#define JOURNAL_ERREUR(tag, ...) ESP_LOGE(tag, __VA_ARGS__)
#else
#include <stdio.h>
#define JOURNAL_INFO(tag, ...)   do { printf("I %s: ", tag); printf(__VA_ARGS__); printf("\n"); } while (0)
#define JOURNAL_ALERTE(tag, ...) do { printf("W %s: ", tag); printf(__VA_ARGS__); printf("\n"); } while (0)
#define JOURNAL_ERREUR(tag, ...) do { printf("E %s: ", tag); printf(__VA_ARGS__); printf("\n"); } while (0)
#endif
