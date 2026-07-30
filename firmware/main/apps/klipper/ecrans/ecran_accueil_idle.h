/* Écran d'accueil Klipper AU REPOS (tâche 3, jalon 3b) : l'état complet de
 * la machine (températures de tous les chauffeurs présents, position,
 * outil actif) affiché selon le palier d'outils courant (klipper_paliers.h)
 * -- machine mono-extrudeur (grandes tuiles), changeur d'outils 2-4 têtes
 * (grille 2x2/2x3, outil actif marqué) ou 5-8 têtes (grille 2x4/2x5
 * compacte, sans consigne inline). Voir accueil_choix.h pour le choix,
 * décidé au démarrage, entre cet écran et ECRAN_ACCUEIL (jalon 2b,
 * impression en cours).
 *
 * Contrôles : le pad de jog XY/Z + sélecteur de pas (tâche 4, jalon 3b) et
 * les quatre boutons de homing avec confirmation (tâche 5) sont désormais
 * réels, dans le même conteneur `zone_controles` que la tâche 3 avait
 * réservé. Rangée Macros : encore un PLACEHOLDER (câblage réel tâche 7).
 *
 * Tâche 6 (jalon 3b) : chaque cellule de température devient tapable --
 * ouvre le clavier numérique (clavier.h) prérempli avec la consigne
 * courante, valide en [0, 350] °C, envoie SET_HEATER_TEMPERATURE. Au palier
 * COMPACT (spec §6 : la cellule n'a plus la place d'un réglage direct), la
 * « vue détaillée » minimale pour ce jalon EST ce même clavier ouvert sur le
 * chauffeur tapé -- PAS un sous-écran séparé avec graphe/historique, hors
 * scope de ce jalon (voir cellule_bouton_cb() dans ecran_accueil_idle.c).
 * Une rangée de préréglages (PLA/PETG/ABS/Off) sous `zone_controles` envoie
 * directement la paire buse active + plateau sans passer par le clavier.
 *
 * `ecran_accueil_idle_ctx_t` est exposé ici plutôt qu'opaque, même raison
 * que ecran_accueil_ctx_t (voir son en-tête) : host-test/tests/
 * test_ecran_accueil_idle.c relit les libellés via lv_label_get_text() pour
 * prouver ce que mettre_a_jour() écrit sans jamais regarder un pixel. */
#pragma once

#include <stdbool.h>

#include "ecran.h"
#include "etat_klipper.h"
#include "lvgl.h"
#include "selecteur_pas.h"

/* Une cellule de température : nom court ("T0".."T7", "Bed"), valeur
 * courante (police du palier), consigne (masquée -- LV_OBJ_FLAG_HIDDEN,
 * jamais NULL -- au palier COMPACT, voir ecran_accueil_idle.c). Toujours
 * créée, jamais NULL : même politique que ctx->bouton_macros dans
 * ecran_accueil.c, visibilité recalculée à chaque mettre_a_jour() plutôt
 * qu'un pointeur à vérifier partout. */
typedef struct {
    lv_obj_t *racine;
    lv_obj_t *nom;
    lv_obj_t *valeur;
    lv_obj_t *consigne;
} ecran_accueil_idle_cellule_t;

/* user_data du rappel de clic d'UNE cellule de température (tâche 6) --
 * même forme que ecran_accueil_idle_jog_info_t/home_info_t : un tableau
 * PARALLÈLE à `cellules[]`, indexé EXACTEMENT pareil (voir
 * ecran_accueil_idle_mettre_a_jour() dans le .c, qui remplit `cellules[i]`
 * ET `cellule_infos[i]` du même indice `total` à chaque appel). Passé
 * DIRECTEMENT comme `contexte` à clavier_ouvrir() (pas via un champ
 * "en attente" du ctx d'écran, contrairement à home_masque_en_attente) :
 * clavier_ouvrir() relaie son `contexte` tel quel au rappel, donc CE
 * pointeur suffit à identifier quel chauffeur a été tapé sans jamais écrire
 * d'état partagé avant l'ouverture -- rien à cloner-avant-ouverture, donc
 * rien que la garde du T5 (home_masque_en_attente + confirmation_est_ouverte())
 * ait besoin de protéger ici : clavier_ouvrir() est déjà lui-même un
 * singleton qui refuse une seconde ouverture (voir clavier.h), et cette
 * fonction n'écrit jamais rien avant de l'appeler. */
typedef struct {
    struct ecran_accueil_idle_ctx_s *ctx; /* jamais NULL une fois construire() passe */
    bool     est_plateau;      /* true => "heater_bed", false => extrudeurs[indice_extrudeur] */
    uint8_t  indice_extrudeur; /* valide seulement si !est_plateau, voir etat_klipper_t::extrudeurs */
    /* Consigne courante (°C, entier -- klipper_gcode_consigne_temp() ne
     * connaît que des cibles entières) : rafraîchie à CHAQUE
     * mettre_a_jour() depuis etat_klipper_t::extrudeurs[i].consigne /
     * ::plateau.consigne (bornée [0, 350], voir consigne_u16() dans le .c),
     * jamais relue depuis l'état backend au moment du clic -- même
     * discipline que ctx->selecteur_pas pour le pas de jog : ce que le clic
     * lit doit être ce que le dernier rendu a montré à l'utilisateur, pas un
     * état backend qui a pu changer entre-temps. */
    uint16_t consigne_courante;
} ecran_accueil_idle_cellule_info_t;

/* Titres du clavier numérique de température (tâche 6), copie FIXE
 * exportée -- même raison que ECRAN_ACCUEIL_IDLE_HOME_TITRES ci-dessous :
 * cellule_bouton_cb() (ecran_accueil_idle.c) ET la capture du simulateur
 * (--scenario 14, aucun tactile simulé ne peut atteindre une vraie cellule)
 * DOIVENT ouvrir EXACTEMENT le même clavier, jamais deux chaînes tapées à la
 * main qui pourraient diverger. */
extern const char ECRAN_ACCUEIL_IDLE_TEMP_TITRE_BUSE[];
extern const char ECRAN_ACCUEIL_IDLE_TEMP_TITRE_PLATEAU[];

/* Bornes de saisie du clavier numérique de température (tâche 6, spec :
 * "borner à [0, 350]") -- nommées pour que cellule_clavier_rappel() (le
 * rappel) et un futur test ne puissent jamais diverger sur la borne haute. */
#define ECRAN_ACCUEIL_IDLE_TEMP_MIN 0
#define ECRAN_ACCUEIL_IDLE_TEMP_MAX 350

/* Préréglages (tâche 6, brief : "PLA 210/60, PETG 240/80, ABS 250/100,
 * Off") -- index FIXE dans `preset_boutons`/`preset_infos`, même convention
 * que ECRAN_ACCUEIL_IDLE_HOME_* plus haut. */
#define ECRAN_ACCUEIL_IDLE_PRESET_PLA  0
#define ECRAN_ACCUEIL_IDLE_PRESET_PETG 1
#define ECRAN_ACCUEIL_IDLE_PRESET_ABS  2
#define ECRAN_ACCUEIL_IDLE_PRESET_OFF  3
#define ECRAN_ACCUEIL_IDLE_PRESET_NB   4

/* user_data d'un rappel de clic de préréglage : le contexte de l'écran (pour
 * lire l'outil actif AU MOMENT DU CLIC, même discipline que
 * jog_bouton_cb()/home_bouton_cb() pour le pas/les axes référencés) et les
 * deux cibles (°C) que CE préréglage précis pose -- buse ACTIVE puis
 * plateau, décision figée (task-6-brief.md) : toujours DEUX gcodes, jamais
 * un seul combiné. */
typedef struct {
    struct ecran_accueil_idle_ctx_s *ctx;
    uint16_t cible_buse;
    uint16_t cible_plateau;
} ecran_accueil_idle_preset_info_t;

/* Une par extrudeur possible plus le plateau (voir KLIPPER_EXTRUDEURS_MAX
 * dans etat_klipper.h) : le pool est dimensionné au pire cas (palier
 * COMPACT, 8 têtes) une fois pour toutes -- jamais redimensionné au fil des
 * mettre_a_jour() successifs, seules la géométrie/police/visibilité de
 * chaque cellule suivent le palier courant. */
#define ECRAN_ACCUEIL_IDLE_CELLULES_MAX (KLIPPER_EXTRUDEURS_MAX + 1)

/* Six boutons de jog, index FIXE dans `jog_boutons`/`jog_infos` -- reutilise
 * par mettre_a_jour() (grisage par axe) et par les tests (host-test/tests/
 * test_ecran_accueil_idle.c), voir ecran_accueil_idle.c pour l'ordre de
 * construction et la mise en page (pad XY autour d'un centre + colonne Z). */
#define ECRAN_ACCUEIL_IDLE_JOG_X_NEG 0
#define ECRAN_ACCUEIL_IDLE_JOG_X_POS 1
#define ECRAN_ACCUEIL_IDLE_JOG_Y_NEG 2
#define ECRAN_ACCUEIL_IDLE_JOG_Y_POS 3
#define ECRAN_ACCUEIL_IDLE_JOG_Z_POS 4
#define ECRAN_ACCUEIL_IDLE_JOG_Z_NEG 5
#define ECRAN_ACCUEIL_IDLE_JOG_NB    6

/* user_data d'un rappel de clic de bouton de jog -- meme forme que
 * ecran_macros_emplacement_t (voir ecran_macros.h) : le contexte de l'ecran
 * (pour relire le pas courant du selecteur au moment du clic) et ce que ce
 * bouton precis represente (axe + sens), jamais recalcule ailleurs. */
typedef struct {
    struct ecran_accueil_idle_ctx_s *ctx; /* jamais NULL une fois construire() passe */
    char                             axe;   /* 'X', 'Y' ou 'Z' */
    float                            signe; /* +1.0f ou -1.0f */
} ecran_accueil_idle_jog_info_t;

/* Quatre boutons de homing (tache 5, jalon 3b), index FIXE dans
 * `home_boutons`/`home_infos` -- meme convention que ECRAN_ACCUEIL_IDLE_JOG_*
 * ci-dessus. L'ORDRE de ce tableau (voir HOME_DEFS dans ecran_accueil_idle.c)
 * DOIT rester synchronise avec ces indices, eux-memes utilises pour indexer
 * ECRAN_ACCUEIL_IDLE_HOME_TITRES[] (meme fichier .c). */
#define ECRAN_ACCUEIL_IDLE_HOME_ALL 0
#define ECRAN_ACCUEIL_IDLE_HOME_X   1
#define ECRAN_ACCUEIL_IDLE_HOME_Y   2
#define ECRAN_ACCUEIL_IDLE_HOME_Z   3
#define ECRAN_ACCUEIL_IDLE_HOME_NB  4

/* user_data d'un rappel de clic de bouton de homing : le contexte de l'ecran
 * (pour lire axes_references_connus au moment du clic et poser
 * home_masque_en_attente avant d'ouvrir la confirmation) et `masque`, le
 * masque d'axes que CE bouton represente -- meme convention de bits que
 * etat_klipper_t::axes_references / klipper_gcode_home() (bit0=X bit1=Y
 * bit2=Z), 0x7 pour "All". `indice` est l'un des ECRAN_ACCUEIL_IDLE_HOME_*
 * ci-dessus, pour indexer ECRAN_ACCUEIL_IDLE_HOME_TITRES[] sans avoir a
 * retrouver l'axe a partir du masque au moment du clic. */
typedef struct {
    struct ecran_accueil_idle_ctx_s *ctx;
    uint8_t                          indice;
    uint8_t                          masque;
} ecran_accueil_idle_home_info_t;

/* Copie du dialogue de confirmation de homing (tache 5), definie UNE SEULE
 * FOIS dans ecran_accueil_idle.c et exposee ici -- le vrai bouton (rappel de
 * clic dans ecran_accueil_idle.c) ET la capture du simulateur
 * (simulateur/main.c, scenario 13, aucun tactile simule ne peut declencher un
 * vrai clic) DOIVENT montrer EXACTEMENT le meme dialogue : deux chaines
 * saisies a la main qui pourraient un jour diverger sans qu'aucun test ne le
 * remarque est precisement ce que ce partage evite (meme lecon que le
 * commentaire de simulateur/main.c pres du scenario 6 -- "Cancel print?" --
 * mais appliquee ici via une VRAIE constante partagee plutot qu'une simple
 * note de synchronisation manuelle). ECRAN_ACCUEIL_IDLE_HOME_TITRES est
 * indexe par ECRAN_ACCUEIL_IDLE_HOME_ALL/X/Y/Z. */
extern const char *const ECRAN_ACCUEIL_IDLE_HOME_TITRES[ECRAN_ACCUEIL_IDLE_HOME_NB];
extern const char        ECRAN_ACCUEIL_IDLE_HOME_MESSAGE[];
extern const char        ECRAN_ACCUEIL_IDLE_HOME_ACTION[];
extern const char        ECRAN_ACCUEIL_IDLE_HOME_DECLINER[];

typedef struct ecran_accueil_idle_ctx_s {
    ecran_accueil_idle_cellule_t cellules[ECRAN_ACCUEIL_IDLE_CELLULES_MAX];
    /* Tâche 6 : tableau PARALLÈLE à `cellules[]`, même indexation (voir le
     * commentaire de ecran_accueil_idle_cellule_info_t plus haut). */
    ecran_accueil_idle_cellule_info_t cellule_infos[ECRAN_ACCUEIL_IDLE_CELLULES_MAX];
    lv_obj_t *position;        /* "X:.. Y:.. Z:.." (1 decimale, "--" si l'axe n'est pas reference) */
    lv_obj_t *outil_actif_nom; /* "Active: T.." / "Active: --" (aucun extrudeur) */
    /* Tâche 6 : dernier `e->outil_actif` vu par mettre_a_jour(), relu par
     * preset_bouton_cb() AU MOMENT DU CLIC -- même discipline que
     * axes_references_connus pour le homing (voir plus bas) : le contexte de
     * l'écran ne doit jamais être touché depuis le rappel de clic hors de ce
     * genre de lecture différée. */
    uint8_t   outil_actif_connu;
    lv_obj_t *zone_controles;  /* conteneur reserve (pad de jog + homing, taches 4/5) */

    /* Pad de jog (tache 4, jalon 3b) : voir ECRAN_ACCUEIL_IDLE_JOG_* plus
     * haut pour l'indexation. `jog_infos[i].ctx` pointe toujours vers CE
     * contexte -- pose une fois par construire(), jamais recalcule. */
    lv_obj_t                      *jog_boutons[ECRAN_ACCUEIL_IDLE_JOG_NB];
    ecran_accueil_idle_jog_info_t  jog_infos[ECRAN_ACCUEIL_IDLE_JOG_NB];
    selecteur_pas_t                selecteur_pas;

    /* Homing (tache 5, jalon 3b) : voir ECRAN_ACCUEIL_IDLE_HOME_* plus haut
     * pour l'indexation. `axes_references_connus` est la DERNIERE valeur de
     * e->axes_references vue par mettre_a_jour() -- relue au moment du clic
     * par le rappel de bouton (meme raison que selecteur_pas pour le pas de
     * jog, voir jog_bouton_cb() dans ecran_accueil_idle.c) pour decider
     * confirmation-ou-direct SANS jamais toucher a l'etat backend depuis un
     * rappel de clic. `home_masque_en_attente` porte le masque D'UN CLIC
     * PRECIS jusqu'a ce que le rappel de confirmation.c (qui arrive PLUS
     * TARD, apres que l'utilisateur ait tape) le relise -- DOIT vivre ici (le
     * contexte de l'ecran), jamais en variable statique de fichier ni en
     * local de pile : confirmation_ouvrir_ex() recoit CE contexte comme
     * `void *contexte`, exactement comme le prescrit son commentaire de tete
     * sur le singleton d'etat qu'un second dialogue empilerait sinon. */
    lv_obj_t                       *home_boutons[ECRAN_ACCUEIL_IDLE_HOME_NB];
    ecran_accueil_idle_home_info_t  home_infos[ECRAN_ACCUEIL_IDLE_HOME_NB];
    uint8_t                         axes_references_connus;
    uint8_t                         home_masque_en_attente;

    /* Tâche 6 : rangée de préréglages, entre `zone_controles` et
     * `bouton_macros` (voir ECRAN_ACCUEIL_IDLE_PRESET_* plus haut pour
     * l'indexation). `zone_preregalges` est le conteneur flex qui les porte,
     * même idiome que `selecteur_pas.racine` (widgets à largeur égale,
     * flex_grow). */
    lv_obj_t                         *zone_preregalges;
    lv_obj_t                         *preset_boutons[ECRAN_ACCUEIL_IDLE_PRESET_NB];
    ecran_accueil_idle_preset_info_t  preset_infos[ECRAN_ACCUEIL_IDLE_PRESET_NB];

    lv_obj_t *bouton_macros;   /* placeholder tache 3, cablage reel tache 7 */
    lv_obj_t *label_macros;    /* enfant direct de bouton_macros, pour le regrisage */
} ecran_accueil_idle_ctx_t;

extern const ecran_desc_t ECRAN_ACCUEIL_IDLE;
