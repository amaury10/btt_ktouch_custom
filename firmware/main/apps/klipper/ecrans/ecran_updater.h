/* Ecran Updater (Task 2, sous-projet OTA firmware) : l'ANCIEN placeholder
 * "Requires OTA - unavailable on this firmware" (ecran_stub.c) est remplace
 * ici par un ecran d'ETAT reel, en LECTURE SEULE : le slot OTA courant (la
 * partition app en cours d'execution), la version du firmware
 * (esp_app_get_description()) et un rappel textuel fixe de la procedure de
 * mise a jour ("Update via /ota (browser)"). Aucun bouton, aucun champ de
 * saisie : ce jalon ne cable AUCUNE ecriture OTA depuis cet ecran ni depuis
 * ce fichier -- voir le commentaire de tete de web.c ("Deliberement AUCUNE
 * route de mise a jour" a ce jalon) et task-2-brief.md ("ZERO risque").
 *
 * `esp_ota_get_running_partition()`/`esp_app_get_description()` sont
 * ESP-only : sous #ifdef ESP_PLATFORM dans le .c, avec repli sur des chaines
 * de substitution ("sim"/"dev") hors cible pour que host-test/le simulateur
 * PC compilent et affichent un ecran coherent -- meme discipline que
 * ecran_reglages_wifi.c pour wifi_scanner (voir son commentaire de tete,
 * "Sur l'appareil (ESP_PLATFORM) / En host-test / simulateur (pas
 * d'ESP_PLATFORM)"). Contenu entierement statique une fois construit :
 * `mettre_a_jour = NULL` (ni le slot ni la version ne changent pendant la
 * vie de cet ecran), `detruire = NULL` (rien a liberer au-dela du contexte,
 * voir ecran.h).
 *
 * `ecran_updater_ctx_t` expose ses trois labels (meme choix que
 * ecran_niveau_lit_ctx_t/ecran_zcalibrate_ctx_t) pour que
 * host-test/tests/test_ecran_updater.c puisse lire leur texte directement,
 * sans jamais inspecter un pixel. */
#pragma once

#include "ecran.h"
#include "lvgl.h"

typedef struct ecran_updater_ctx_s {
    lv_obj_t *label_slot;    /* "Slot: <label partition>" */
    lv_obj_t *label_version; /* "Version: <esp_app_desc_t::version>" */
    lv_obj_t *label_update;  /* "Update via /ota (browser)", texte fixe */
} ecran_updater_ctx_t;

extern const ecran_desc_t ECRAN_UPDATER;
