/* Store dédié de la liste des fichiers gcode connus de Moonraker.
 *
 * POURQUOI hors de etat_klipper_t : cette liste (jusqu'à
 * KLIPPER_FICHIERS_MAX x KLIPPER_FICHIER_MAX, ~2 Ko) vivait DANS
 * etat_klipper_t, or cet état est un POD copié PARTOUT -- copies statiques en
 * RAM interne (g_etat de l'habillage, g_dernier_etat_ws du backend, la boîte
 * moonraker_boite) ET posé sur les piles des tâches WS/boucle/httpd. Les
 * +2 Ko multipliés par toutes ces copies épuisaient la RAM interne au point
 * que la tâche WebSocket (16 Ko de pile) ne pouvait plus s'allouer
 * (« Error create websocket task ») -- le WS ne se connectait donc jamais, et
 * macros ET fichiers (tous deux récupérés par le WS) restaient vides. Voir la
 * mémoire du projet.
 *
 * En sortant `fichiers[]` de l'état vers CE store, sizeof(etat_klipper_t)
 * revient à ~1808 octets et la liste ne vit plus qu'en UN SEUL exemplaire
 * (les ~2 Ko de g_store dans klipper_fichiers.c, au lieu de 3 copies ou plus),
 * ce qui est un gain net de RAM interne.
 *
 * Le store est ÉCRIT par la tâche WebSocket (klipper_fichiers_definir() sur
 * réponse à server.files.list) et LU par d'autres tâches (l'écran fichiers via
 * la tâche LVGL, la route /state via la tâche httpd) : l'accès est donc protégé
 * par un verrou (voir klipper_fichiers.c). */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "etat_klipper.h" /* KLIPPER_FICHIERS_MAX / KLIPPER_FICHIER_MAX */

/* Instantané complet de la liste des fichiers gcode. Même politique de
 * nommage/bornes que l'ancien champ `fichiers[]` de etat_klipper_t : chaque
 * `noms[i]` est un CHEMIN relatif à la racine "gcodes" de Moonraker,
 * éventuellement avec des '/' (sous-dossier). Les entrées au-delà de `nb` sont
 * sans signification. */
typedef struct {
    char    noms[KLIPPER_FICHIERS_MAX][KLIPPER_FICHIER_MAX];
    uint8_t nb;       /* 0 = liste jamais reçue ou vide */
    bool    tronques; /* true si Moonraker en avait plus que KLIPPER_FICHIERS_MAX */
} klipper_fichiers_t;

/* Remplace ENTIÈREMENT le contenu du store par `*src` (copie sous verrou).
 * `src` NULL = no-op. */
void klipper_fichiers_definir(const klipper_fichiers_t *src);

/* Copie le contenu courant du store dans `*dest` (fourni par l'appelant, sous
 * verrou). `dest` NULL = no-op. */
void klipper_fichiers_lire(klipper_fichiers_t *dest);
