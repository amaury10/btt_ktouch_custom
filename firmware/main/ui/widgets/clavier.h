/* Clavier tactile modal : le plus gros morceau partagé du jalon (§6 de la
 * spécification) — adresse d'hôte, commande gcode, recherche de fichier, mot
 * de passe WiFi passeront tous par ici plutôt que chacun réinventer son
 * propre clavier. Habille `lv_keyboard` (fourni par LVGL) en dialogue modal
 * qui REND une valeur au lieu de se contenter d'éditer un texte à l'écran :
 * c'est cette conversion événement -> valeur, avec un cycle de vie sûr, qui
 * est le vrai travail de ce fichier.
 *
 * Un seul clavier existe jamais (état statique de fichier dans clavier.c,
 * jamais alloué par le widget lui-même — voir la justification dans
 * clavier.c) : un second clavier.h alors qu'un premier est déjà ouvert est
 * refusé plutôt qu'empilé, voir clavier_ouvrir() ci-dessous. */
#pragma once

#include <stdbool.h>

/* Taille du tampon qui porte la valeur rendue au rappel (voir
 * clavier_rappel_t), NUL compris. 128 : dans le voisinage de
 * BACKEND_HOTE_LONGUEUR_MAX (core/backend.h, 64) sans le copier telle
 * quelle — ce clavier sert aussi des commandes gcode et des mots de passe
 * WiFi (63 caractères au maximum pour une WPA2-PSK, plus le NUL) que
 * BACKEND_HOTE_LONGUEUR_MAX ne dimensionne pas. 128 couvre large la plus
 * grande chaîne déjà connue du dépôt qui transite par un clavier
 * (REGLAGES_HOTE_CHAINE_MAX, core/reglages.c, 71 octets : "hôte:port") avec
 * de la marge pour une commande gcode ou une recherche de fichier assez
 * longue, sans viser un tampon inutilement grand sur un appareil aux
 * ressources modestes. */
#define CLAVIER_VALEUR_MAX 128

typedef enum {
    CLAVIER_TEXTE,      /* clavier alphanumérique complet (LV_KEYBOARD_MODE_TEXT_LOWER) */
    CLAVIER_NUMERIQUE,  /* pavé numérique (LV_KEYBOARD_MODE_NUMBER) */
} clavier_mode_t;

/* Rappel appelé exactement une fois par clavier_ouvrir() : avec la valeur
 * saisie (pointeur vers un tampon interne à clavier.c, voir le commentaire
 * de tête de clavier.c — valide uniquement pendant l'appel de ce rappel, à
 * copier immédiatement si l'appelant en a besoin plus tard) si l'utilisateur
 * valide, avec NULL s'il annule. Le clavier s'est déjà refermé (ses objets
 * LVGL sont programmés à la destruction) au moment de cet appel — un rappel
 * qui empile un nouvel écran ou rouvre un clavier ne lit donc jamais de
 * mémoire déjà libérée. */
typedef void (*clavier_rappel_t)(const char *valeur, void *contexte);

/* Ouvre un clavier plein écran (800x480, par-dessus tout y compris la barre
 * d'état de l'habillage — voir clavier.c pour comment) pré-rempli avec
 * `valeur_initiale` (NULL traité comme chaîne vide, même politique que le
 * reste de ui/, voir tuile_definir_valeur()), dans le mode demandé. `titre`
 * NULL est également traité comme chaîne vide.
 *
 * `rappel` NULL est refusé (journalisé) : ouvrir un clavier dont le résultat
 * ne serait jamais recueilli n'a pas de sens.
 *
 * Un second appel alors qu'un clavier est déjà ouvert est ignoré et
 * journalisé (JOURNAL_ALERTE) plutôt qu'empilé : deux claviers modaux dont
 * l'un devient inaccessible sont pires qu'un appel refusé. */
void clavier_ouvrir(const char *titre, const char *valeur_initiale,
                     clavier_mode_t mode, clavier_rappel_t rappel, void *contexte);
