/* Sonde séquentielle du parc (spec 2026-08-15-gestion-parc-design.md) :
 * interroge UNE imprimante non active par tour (HTTP GET léger, timeout
 * 1,5 s -- l'utilisateur a explicitement refusé les longs timeouts), publie
 * dans parc_imprimantes.h, pause 1 s, round-robin. JAMAIS de connexions
 * simultanées (RAM interne, leçons des 14-15/08) et JAMAIS l'imprimante
 * active (son état vient du store temps réel existant).
 *
 * Cycle de vie : tâche pérenne créée paresseusement à la première ouverture
 * de l'écran Parc (patron usb_scan : pile en PSRAM via xTaskCreateWithCaps,
 * réveil par sémaphore, échec = non démarré, retenté) ; ACTIVE seulement
 * tant que l'écran Parc est ouvert (activer(true) à construire(),
 * activer(false) à detruire()) -- aucun trafic de fond sinon.
 * No-op complet hors ESP_PLATFORM (esp_http_client), même politique que
 * usb_scan.h. */
#pragma once

#include <stdbool.h>

void parc_sonde_demarrage_paresseux(void);

/* Arme/désarme la sonde. Désarmée : la tâche s'endort à la fin du tour en
 * cours (au plus ~2,5 s de trafic résiduel). Armer avant le démarrage
 * paresseux est sans effet. */
void parc_sonde_activer(bool active);
