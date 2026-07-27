/* Contrat que remplit une application pour parler à sa machine.
 *
 * Le socle possède la boucle : il alloue l'état, appelle `rafraichir`
 * périodiquement, et range le résultat. Un backend ne connaît ni l'affichage,
 * ni la navigation, ni la persistance — seulement son protocole. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#define BACKEND_HOTE_LONGUEUR_MAX 64

typedef struct {
    char     adresse[BACKEND_HOTE_LONGUEUR_MAX]; /* nom ou IPv4, sans schéma */
    uint16_t port;
} backend_hote_t;

typedef struct {
    const char *nom;          /* "moonraker", "factice" — stocké dans les réglages */
    size_t      taille_etat;  /* le socle alloue ; le backend n'alloue jamais */

    esp_err_t (*demarrer)(void *etat, const backend_hote_t *hote);
    esp_err_t (*rafraichir)(void *etat);
    void      (*arreter)(void *etat);

    /* `arguments_json` vaut NULL quand l'action n'en prend pas. */
    esp_err_t (*commande)(void *etat, const char *action, const char *arguments_json);
} backend_desc_t;

/* Actions communes. Un backend qui n'en gère pas une rend ESP_ERR_NOT_SUPPORTED
 * plutôt que d'échouer silencieusement — l'interface doit pouvoir griser un
 * bouton en le sachant. */
#define BACKEND_ACTION_PAUSE      "pause"
#define BACKEND_ACTION_REPRENDRE  "reprendre"
#define BACKEND_ACTION_ANNULER    "annuler"
#define BACKEND_ACTION_URGENCE    "arret_urgence"
