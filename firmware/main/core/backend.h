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

    /* `etat` pointe TOUJOURS vers un tampon remis à zéro par le socle juste
     * avant cet appel (voir etat_store_tampon_arriere() dans etat_store.c,
     * appelée par boucle_cycle() dans core/boucle_cycle.c immédiatement avant
     * chaque rafraîchissement) — jamais le contenu du cycle précédent. Un
     * backend qui a besoin de mémoriser quelque chose d'un appel à l'autre
     * (un compteur de progression, par exemple) doit porter cet état
     * lui-même, typiquement dans une variable statique du fichier — voir
     * backend_factice.c — jamais en le relisant depuis `etat`. C'est le
     * contrat inverse d'un rappel qui recevrait l'état précédent à modifier
     * en place ; il est délibéré : le socle ne garantit rien de plus fort
     * qu'un tampon neuf, pour ne jamais laisser un backend fautif publier un
     * débris du cycle d'avant si `rafraichir` échoue de ne remplir qu'une
     * partie de la structure. */
    esp_err_t (*rafraichir)(void *etat);
    void      (*arreter)(void *etat);

    /* `arguments_json` vaut NULL quand l'action n'en prend pas. */
    esp_err_t (*commande)(void *etat, const char *action, const char *arguments_json);

    /* Champ OPTIONNEL (jalon 3a, tâche 5) : NULL = comportement actuel
     * (cadence fixe du socle, voir BOUCLE_PERIODE_MS_DEFAUT dans
     * core/boucle_cycle.h) — c'est ce qui garde le jouet du 2b (et
     * `backend_factice.c`) compilables SANS AUCUNE MODIFICATION, un
     * littéral `backend_desc_t` qui n'initialise pas ce champ par
     * désignation le met à NULL par construction (critère 8 du jalon 3,
     * vérifié mécaniquement par `test_boucle_cycle.c`).
     *
     * Période d'interrogation souhaitée en ms, relue par la boucle à
     * CHAQUE cycle (la valeur peut changer d'un appel à l'autre — c'est
     * précisément l'usage prévu : le backend Moonraker rend 250 quand le
     * WS est en ligne, 1000 en repli HTTP, voir backend_moonraker.c). Une
     * valeur de retour de 0 vaut aussi « défaut du socle » : un backend qui
     * n'a pas encore d'avis (ex. avant sa toute première connexion) peut
     * rendre 0 sans avoir à connaître la constante du socle. `etat` reçoit
     * le même tampon qu'un lecteur (le tampon "avant" publié, jamais celui
     * en cours de rafraîchissement) — aucun backend connu n'en a besoin
     * aujourd'hui (l'état WS en ligne/hors ligne est porté en statique de
     * fichier, comme g_actif dans backend_moonraker.c), mais le contrat le
     * fournit par symétrie avec `rafraichir`/`commande` plutôt que
     * d'inventer un cas particulier. */
    uint32_t (*periode_ms)(void *etat);
} backend_desc_t;

/* Actions communes. Un backend qui n'en gère pas une rend ESP_ERR_NOT_SUPPORTED
 * plutôt que d'échouer silencieusement — l'interface doit pouvoir griser un
 * bouton en le sachant. */
#define BACKEND_ACTION_PAUSE      "pause"
#define BACKEND_ACTION_REPRENDRE  "reprendre"
#define BACKEND_ACTION_ANNULER    "annuler"
#define BACKEND_ACTION_URGENCE    "arret_urgence"

/* Lance une macro nommée. `arguments_json` vaut `{"nom":"<macro>"}` — jamais
 * NULL pour cette action-ci, contrairement aux quatre ci-dessus qui n'en
 * prennent pas (voir le commentaire de `commande` plus haut) : un backend
 * qui reçoit NULL ici n'a pas de nom à lancer et rend ESP_ERR_NOT_SUPPORTED.
 * Introduite tâche 2, jalon 3a (backend_factice.c, scénario 11 « U1 ») ; le
 * backend Moonraker (tâche 6) partage ce même symbole. */
#define BACKEND_ACTION_MACRO      "macro"

/* Jalon 3b : script gcode arbitraire construit par un écran (klipper_gcode.c)
 * et relayé tel quel à printer.gcode.script. arguments_json = {"script":"..."}.
 * Distinct de BACKEND_ACTION_MACRO (qui porte {"nom":...}) : ici l'appelant a
 * déjà construit le gcode complet, le backend ne fait que le transmettre. */
#define BACKEND_ACTION_GCODE      "gcode"

/* Feature "Power devices Moonraker", tâche B : bascule ("toggle") d'une
 * prise pilotée par Moonraker (API machine.device_power.*). arguments_json =
 * {"device":"<nom>","action":"toggle"} — déjà construit par l'écran
 * (ecran_power.c), relayé tel quel au backend comme params_json de
 * machine.device_power.set_device (voir backend_moonraker.c) : même esprit
 * que BACKEND_ACTION_GCODE, l'appelant a déjà construit le JSON attendu par
 * Moonraker, le backend ne fait que le transmettre. */
#define BACKEND_ACTION_POWER      "power"

/* Feature Spoolman (spec 2026-08-15) : designe la bobine chargee.
 * arguments_json = {"spool_id":N} ou {"spool_id":null} (aucune) — deja
 * construit par l'ecran (ecran_spoolman.c), relaye tel quel comme
 * params_json de server.spoolman.spool_id : meme esprit que
 * BACKEND_ACTION_POWER juste au-dessus.
 *
 * POURQUOI cette methode plutot que la macro gcode SET_ACTIVE_SPOOL
 * (installee sur le Pi pour Mainsail et les slicers) : elle ne depend pas de
 * printer.cfg, ne traverse pas la file gcode, et fonctionne meme si Klippy
 * est en erreur — trois raisons pour lesquelles un panneau d'ecran ne doit
 * pas emprunter le chemin gcode ici. */
#define BACKEND_ACTION_SPOOLMAN   "spoolman"
