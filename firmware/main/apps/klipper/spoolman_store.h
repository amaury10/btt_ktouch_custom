/* Store dédié Spoolman (spec 2026-08-15-spoolman-design.md) : la liste des
 * bobines et la bobine active, écrites par la tâche WS (via les parseurs de
 * moonraker_rpc.h), lues par l'écran Spoolman.
 *
 * POURQUOI hors etat_klipper_t (même choix que klipper_fichiers/usb_fichiers/
 * bed_mesh) : ~1 Ko de liste multiplié par toutes les copies d'état ET les
 * piles qui les portent, c'est exactement la maladie « etat_klipper_t vs
 * piles » de la mémoire du projet. Instance UNIQUE en PSRAM, verrou court,
 * génération.
 *
 * La liste (~1 Ko) impose un scratch au LECTEUR (contrat de
 * spoolman_lire_liste()) ; l'état (12 octets) se lit sur la pile sans
 * précaution. Les deux partagent le MÊME compteur de génération : l'écran
 * consomme les deux, un compteur par source ne lui ferait rien redessiner de
 * plus. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define SPOOLMAN_BOBINES_MAX   12 /* bobines retenues (troncature au-delà, signalée) */
#define SPOOLMAN_TEXTE_MAX     24 /* nom de filament / de fabricant */
#define SPOOLMAN_MATIERE_MAX   12 /* "PLA", "PETG", "ABS"... */

/* Aucune bobine active. JAMAIS 0 : c'est un identifiant valide côté
 * Spoolman, le confondre avec « aucune » afficherait une bobine fantôme. */
#define SPOOLMAN_AUCUNE_BOBINE (-1)

typedef struct {
    int32_t  id;
    char     filament[SPOOLMAN_TEXTE_MAX];   /* filament.name */
    char     fabricant[SPOOLMAN_TEXTE_MAX];  /* filament.vendor.name ("" si aucun) */
    char     matiere[SPOOLMAN_MATIERE_MAX];  /* filament.material ("" si absente) */
    uint32_t couleur;                        /* 0xRRGGBB, valide seulement si couleur_connue */
    bool     couleur_connue;
    float    restant_g;                      /* remaining_weight, valide si restant_connu */
    bool     restant_connu;                  /* faux : bobine sans poids initial -> "? g" */
    float    total_g;                        /* filament.weight (0 = inconnu) */
} spoolman_bobine_t;

typedef struct {
    uint8_t           nb;        /* <= SPOOLMAN_BOBINES_MAX */
    bool              tronquee;  /* la source en portait davantage */
    bool              connue;    /* une liste a réellement été reçue (vs jamais interrogé) */
    spoolman_bobine_t bobines[SPOOLMAN_BOBINES_MAX];
} spoolman_liste_t;

typedef struct {
    int32_t id_actif;     /* SPOOLMAN_AUCUNE_BOBINE si aucune */
    bool    connecte;     /* Moonraker voit-il le serveur Spoolman ? */
    bool    statut_connu; /* un statut a été reçu au moins une fois */
} spoolman_etat_t;

/* Remplace la liste (copie sous verrou, +1 génération). NULL = no-op. */
void spoolman_definir_liste(const spoolman_liste_t *liste);

/* Copie la liste dans `dest` -- ~1 Ko : `dest` DOIT être un scratch
 * PSRAM/statique, jamais une variable de pile (voir le commentaire de tête).
 * NULL = no-op ; jamais définie = zéros (donc `connue` faux). */
void spoolman_lire_liste(spoolman_liste_t *dest);

/* Bobine active. `id` hors [0, INT32_MAX] est ramené à SPOOLMAN_AUCUNE_BOBINE
 * (un identifiant négatif n'existe pas côté Spoolman). +1 génération. */
void spoolman_definir_actif(int32_t id);

/* État de la liaison Moonraker <-> Spoolman. +1 génération. */
void spoolman_definir_connecte(bool connecte);

/* Copie l'état (12 octets : la pile est ici parfaitement légitime, à la
 * différence de spoolman_lire_liste()). NULL = no-op. */
void spoolman_lire_etat(spoolman_etat_t *dest);

/* Compteur monotone, +1 à chaque écriture de l'un ou l'autre -- même idiome
 * que usb_fichiers_generation()/bed_mesh_generation() (redessin sur
 * changement seulement, et somme des générations externes de l'habillage
 * dans app_main). */
uint32_t spoolman_generation(void);
