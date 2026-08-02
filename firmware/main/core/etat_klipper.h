/* État d'une machine Klipper tel que l'interface le consomme.
 *
 * Structure POD, taille fixe, sans pointeur : c'est ce qui permet au socle de
 * détecter un changement par simple comparaison mémoire (voir etat_store.h) et
 * d'allouer une fois pour toutes. Ajouter un champ est sans danger ; ajouter un
 * pointeur casserait la détection de changement.
 *
 * v2 (jalon 3a) : les champs `buse_*`/`plateau_*` du 2b ont disparu, remplacés
 * par `extrudeurs[0]` et `plateau` (même type `klipper_chauffeur_t`) — voir
 * task-1-brief.md. Le jouet du 2b (exemples/backend_jouet/) a son propre type
 * d'état et n'est pas concerné par cette migration. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define KLIPPER_ETAT_TEXTE_MAX  24
#define KLIPPER_FICHIER_MAX     64

#define KLIPPER_EXTRUDEURS_MAX   8
#define KLIPPER_VENTILATEURS_MAX 4
#define KLIPPER_MACROS_MAX      48
#define KLIPPER_MACRO_NOM_MAX   32

/* Fichiers gcode listés depuis Moonraker (server.files.list) -- voir
 * rpc_lire_fichiers() dans moonraker_rpc.h. Réutilise KLIPPER_FICHIER_MAX
 * (défini ci-dessus pour `fichier`, le fichier en cours d'impression) comme
 * longueur d'une entrée : même contrainte de nommage Moonraker des deux
 * côtés, pas de raison d'avoir deux constantes de longueur distinctes. */
#define KLIPPER_FICHIERS_MAX    32

/* Plafond d'affichage du temps restant estimé : 99 h 59 min 59 s. Au-delà,
 * l'estimation n'a plus de sens (impression malformée ou début aberrant) et
 * un afficheur peut représenter cette valeur sans cas particulier. */
#define KLIPPER_TEMPS_RESTANT_MAX_S 359999u

/* Un chauffeur générique (extrudeur ou plateau) : mesure, consigne, et un
 * drapeau de présence — un plateau ou un extrudeur au-delà de nb_extrudeurs
 * peut simplement ne pas exister sur la machine, ce n'est pas la même chose
 * qu'un chauffeur présent mais à 0°C. */
typedef struct {
    float actuelle;
    float consigne;
    bool  presente;
} klipper_chauffeur_t;

typedef struct {
    float vitesse;   /* 0.0 à 1.0 */
    bool  present;
} klipper_ventilateur_t;

typedef struct {
    char  etat[KLIPPER_ETAT_TEXTE_MAX];   /* "ready", "printing", "paused", "error" */

    klipper_chauffeur_t   extrudeurs[KLIPPER_EXTRUDEURS_MAX];
    uint8_t               nb_extrudeurs;    /* 0..8, nombre de `presente` */
    uint8_t               outil_actif;      /* index dans extrudeurs */
    klipper_chauffeur_t   plateau;          /* presente=false si pas de lit chauffant */
    klipper_ventilateur_t ventilateurs[KLIPPER_VENTILATEURS_MAX];

    float   position[3];                    /* X, Y, Z (mm) ; 0 si jamais reçu */
    uint8_t axes_references;                /* masque bit0=X bit1=Y bit2=Z */
    bool    deplacement_absolu;

    uint16_t vitesse_pct;                   /* M220, 100 = normal ; 0 = pas encore reçu */
    uint16_t flux_pct;                      /* M221, 100 = normal ; 0 = pas encore reçu */
    int32_t  babystep_z_um;                 /* µm signés (gcode offset Z) */

    /* Limites globales de vitesse/accélération (panneau Limits, tâche 5,
     * sous-projet "panneaux KlipperScreen") -- toolhead.max_velocity/
     * max_accel/square_corner_velocity/max_accel_to_decel, lues par
     * fusionner_toolhead() (moonraker_rpc.c). +16 octets, QUATRE scalaires
     * `float` groupés, PAS de tableau -- voir la note RAM en tête de ce
     * fichier (klipper_fichiers.h) sur pourquoi un tableau ajouté ici serait
     * dangereux alors que des scalaires ne le sont pas. 0 = pas encore reçu
     * (Klipper récent peut omettre max_accel_to_decel, déprécié -- ce champ
     * reste alors à 0, l'écran grise cette ligne, voir ecran_limites.c). */
    float limite_velocity;        /* mm/s, toolhead.max_velocity */
    float limite_accel;           /* mm/s^2, toolhead.max_accel */
    float limite_square_corner;   /* mm/s, toolhead.square_corner_velocity */
    float limite_accel_to_decel;  /* mm/s^2, toolhead.max_accel_to_decel */

    char    macros[KLIPPER_MACROS_MAX][KLIPPER_MACRO_NOM_MAX];
    uint8_t nb_macros;
    bool    macros_tronquees;

    /* NOTE : la liste des fichiers gcode NE vit PLUS ici. Elle a été sortie
     * vers un store dédié (apps/klipper/klipper_fichiers.h) : cet état est un
     * POD copié partout (habillage, boîte WS, piles des tâches), et les ~2 Ko
     * de `fichiers[]` dupliqués dans chaque copie épuisaient la RAM interne au
     * point que la tâche WebSocket ne pouvait plus s'allouer. Voir
     * klipper_fichiers.h. */

    char     fichier[KLIPPER_FICHIER_MAX];
    float    progression;                  /* 0.0 à 1.0 */
    uint32_t temps_restant_s;              /* 0 si inconnu */

    bool impression_en_cours;
    bool impression_en_pause;
} etat_klipper_t;
