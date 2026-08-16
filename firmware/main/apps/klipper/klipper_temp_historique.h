/* Store dédié de l'historique des températures (extrudeurs + plateau), pour
 * alimenter un futur graphe à l'accueil.
 *
 * POURQUOI hors de etat_klipper_t : même raison que klipper_fichiers.h (voir
 * son commentaire de tête) -- etat_klipper_t est un POD copié PARTOUT (copies
 * statiques en RAM interne : g_etat de l'habillage, g_dernier_etat_ws du
 * backend, la boîte moonraker_boite, ET les piles des tâches WS/boucle/httpd
 * qui le portent en variable locale). Un tampon circulaire de
 * KLIPPER_HISTO_SERIES x KLIPPER_HISTO_POINTS points (~2,2 Ko) ajouté DANS
 * cet état serait multiplié par toutes ces copies -- exactement le
 * mécanisme qui avait épuisé la RAM interne au point que la tâche WebSocket
 * ne pouvait plus s'allouer (voir la mémoire du projet). Ce store vit donc à
 * part, en UN SEUL exemplaire statique (klipper_temp_historique.c),
 * alimenté à chaque nouvel état reçu via klipper_temp_historique_pousser().
 *
 * Accès : ce store sera écrit par la tâche qui reçoit l'état Klipper (WS ou
 * boucle simulée) et lu par la tâche LVGL (futur graphe à l'accueil) : accès
 * protégé par un verrou, même politique que klipper_fichiers.c. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "etat_klipper.h" /* etat_klipper_t, KLIPPER_EXTRUDEURS_MAX */

/* ~10 min d'historique à raison d'un point toutes les 5 s. */
#define KLIPPER_HISTO_POINTS 120
/* 8 extrudeurs + 1 plateau = 9 séries. */
#define KLIPPER_HISTO_SERIES (KLIPPER_EXTRUDEURS_MAX + 1)

/* Mapping FIXE des séries : série i dans [0, KLIPPER_EXTRUDEURS_MAX-1] =
 * extrudeurs[i] ; série KLIPPER_EXTRUDEURS_MAX = plateau. */

/* Pousse UN point par série (température réelle actuelle, arrondie en
 * int16 °C) à partir de l'état Klipper courant : met à jour les drapeaux de
 * présence, avance la tête du tampon circulaire, incrémente la génération.
 * `e` NULL = no-op (pas d'avancée, pas de génération incrémentée). */
void klipper_temp_historique_pousser(const etat_klipper_t *e);

/* Compteur monotone, +1 à chaque pousser -- un consommateur (le futur graphe
 * de l'accueil) ne redessine que quand cette valeur a changé. */
uint32_t klipper_temp_historique_generation(void);

/* Le chauffant de cette série était-il présent au dernier pousser ? Rend
 * false si `serie >= KLIPPER_HISTO_SERIES`. */
bool klipper_temp_historique_serie_presente(uint8_t serie);

/* Dernier point poussé pour cette série, dans `*sortie`. Rend false (et ne
 * touche pas `*sortie`) si la série est invalide, ou si aucun point n'a
 * encore été poussé. */
bool klipper_temp_historique_dernier(uint8_t serie, int16_t *sortie);

/* Copie dans `dest` les points valides d'UNE série, en ordre chronologique
 * (du plus ancien au plus récent -- le wraparound du tampon circulaire est
 * géré ici), jusqu'à `min(nb_points_valides, max)` points. Rend le nombre de
 * points copiés. Rend 0 sans toucher `dest` si `serie >= KLIPPER_HISTO_SERIES`
 * ou si `dest` est NULL. */
size_t klipper_temp_historique_serie(uint8_t serie, int16_t *dest, size_t max);
