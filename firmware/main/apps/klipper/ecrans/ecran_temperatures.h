/* Écran Températures (sous-projet 2, découpage KlipperScreen -- panneau
 * Temperature) : la case de menu "Temperatures" de ecran_accueil_hub.c
 * (ECRAN_ACCUEIL_HUB_MENU_TEMPERATURES, actuellement no-op scopé, câblage
 * réel prévu en tâche 2 de ce sous-projet) ouvre CET écran dédié pour régler
 * les consignes de chauffe -- une fonction que l'ancien ecran_accueil_idle.c
 * portait (tap sur une cellule -> clavier numérique + rangée de préréglages
 * PLA/PETG/ABS/Off) et qui est partie AVEC lui quand le hub l'a remplacé
 * (celui-ci n'a délibérément NI clic sur ses tuiles NI préréglages, voir le
 * commentaire de tête de ecran_accueil_hub.c, "ÉCART délibéré"). Cette tâche
 * (T1) rétablit la fonction dans son propre écran plutôt que de la remettre
 * dans le hub -- exactement le découpage "un panneau KlipperScreen = un
 * écran dédié" que ce sous-projet poursuit.
 *
 * Reprend À L'IDENTIQUE (renommage mis à part) la SECTION TEMPÉRATURES de
 * l'ancien ecran_accueil_idle.c (git ce47e3a, avant sa suppression tâche 7
 * de la refonte accueil/déplacer) : structures de cellule/info/preset,
 * nom_chauffeur_extrudeur()/consigne_u16()/cellule_info_nom_chauffeur(),
 * cellule_clavier_rappel()/cellule_bouton_cb(), preset_bouton_cb(), et la
 * construction + mettre_a_jour() des tuiles par palier (géométrie IDENTIQUE
 * à celle déjà reprise par ecran_accueil_hub.c -- LARGEUR_CONTENU=742, pas
 * 800, cet écran vivant lui aussi à droite du rail persistant). NE reprend
 * PAS le jog/homing/sélecteur de pas/ligne de position-outil actif/bouton
 * Macros de l'ancien idle : cet écran n'a QUE les tuiles réglables et la
 * rangée de 4 préréglages, rien d'autre -- le jog/homing vit désormais dans
 * ecran_deplacer.c, la position/l'outil actif nulle part pour l'instant
 * (aucun brief ne le redemande ici).
 *
 * `ecran_temperatures_ctx_t` est exposé ici plutôt qu'opaque, même raison
 * que ecran_accueil_hub_ctx_t (voir son en-tête) : host-test/tests/
 * test_ecran_temperatures.c relit les libellés/couleurs via
 * lv_label_get_text()/lv_obj_get_style_text_color() pour prouver ce que
 * mettre_a_jour() écrit sans jamais regarder un pixel. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ecran.h"
#include "etat_klipper.h"
#include "lvgl.h"

/* Une cellule de température : copie de la structure équivalente de
 * l'ancien ecran_accueil_idle.c/de ecran_accueil_hub.c -- voir le
 * commentaire de tête pour pourquoi une copie plutôt qu'un partage (même
 * raisonnement que ecran_accueil_hub.h, "Réutilisation (DRY)"). */
typedef struct {
    lv_obj_t *racine;
    lv_obj_t *nom;
    lv_obj_t *valeur;
    lv_obj_t *consigne;
} ecran_temperatures_cellule_t;

/* user_data du rappel de clic d'UNE cellule de température -- même forme
 * que ecran_accueil_idle_cellule_info_t (l'ancien écran) : un tableau
 * PARALLÈLE à `cellules[]`, indexé EXACTEMENT pareil (voir
 * ecran_temperatures_mettre_a_jour() dans le .c, qui remplit `cellules[i]`
 * ET `cellule_infos[i]` du même indice `total` à chaque appel). Passé
 * DIRECTEMENT comme `contexte` à clavier_ouvrir() : clavier_ouvrir() relaie
 * son `contexte` tel quel au rappel, donc CE pointeur suffit à identifier
 * quel chauffeur a été tapé sans jamais écrire d'état partagé avant
 * l'ouverture -- clavier_ouvrir() est déjà lui-même un singleton qui refuse
 * une seconde ouverture (voir clavier.h). */
typedef struct {
    struct ecran_temperatures_ctx_s *ctx; /* jamais NULL une fois construire() passé */
    bool     est_plateau;      /* true => "heater_bed", false => extrudeurs[indice_extrudeur] */
    uint8_t  indice_extrudeur; /* valide seulement si !est_plateau, voir etat_klipper_t::extrudeurs */
    /* Consigne courante (°C, entier), rafraîchie à CHAQUE mettre_a_jour()
     * depuis etat_klipper_t::extrudeurs[i].consigne / ::plateau.consigne
     * (bornée [0, 350], voir consigne_u16() dans le .c), jamais relue depuis
     * l'état backend au moment du clic -- ce que le clic lit doit être ce
     * que le dernier rendu a montré à l'utilisateur, pas un état backend qui
     * a pu changer entre-temps. */
    uint16_t consigne_courante;
} ecran_temperatures_cellule_info_t;

/* Titres du clavier numérique de température, copie FIXE exportée -- même
 * raison que dans l'ancien ecran_accueil_idle.h : cellule_bouton_cb()
 * (ecran_temperatures.c) doit toujours ouvrir EXACTEMENT le même clavier. */
extern const char ECRAN_TEMPERATURES_TITRE_BUSE[];
extern const char ECRAN_TEMPERATURES_TITRE_PLATEAU[];

/* Bornes de saisie du clavier numérique de température -- nommées pour que
 * cellule_clavier_rappel() (le rappel) et un futur test ne puissent jamais
 * diverger sur la borne haute. */
#define ECRAN_TEMPERATURES_TEMP_MIN 0
#define ECRAN_TEMPERATURES_TEMP_MAX 350

/* Préréglages (brief : "PLA 210/60, PETG 240/80, ABS 250/100, Off") --
 * index FIXE dans `preset_boutons`/`preset_infos`. */
#define ECRAN_TEMPERATURES_PRESET_PLA  0
#define ECRAN_TEMPERATURES_PRESET_PETG 1
#define ECRAN_TEMPERATURES_PRESET_ABS  2
#define ECRAN_TEMPERATURES_PRESET_OFF  3
#define ECRAN_TEMPERATURES_PRESET_NB   4

/* user_data d'un rappel de clic de préréglage : le contexte de l'écran
 * (pour lire l'outil actif AU MOMENT DU CLIC) et les deux cibles (°C) que CE
 * préréglage précis pose -- buse ACTIVE puis plateau, décision figée
 * (toujours DEUX gcodes, jamais un seul combiné). */
typedef struct {
    struct ecran_temperatures_ctx_s *ctx;
    uint16_t cible_buse;
    uint16_t cible_plateau;
} ecran_temperatures_preset_info_t;

/* Une par extrudeur possible plus le plateau (voir KLIPPER_EXTRUDEURS_MAX
 * dans etat_klipper.h) : le pool est dimensionné au pire cas (palier
 * COMPACT, 8 têtes) une fois pour toutes. */
#define ECRAN_TEMPERATURES_CELLULES_MAX (KLIPPER_EXTRUDEURS_MAX + 1)

typedef struct ecran_temperatures_ctx_s {
    ecran_temperatures_cellule_t cellules[ECRAN_TEMPERATURES_CELLULES_MAX];
    /* Tableau PARALLÈLE à `cellules[]`, même indexation (voir le commentaire
     * de ecran_temperatures_cellule_info_t plus haut). */
    ecran_temperatures_cellule_info_t cellule_infos[ECRAN_TEMPERATURES_CELLULES_MAX];

    /* Dernier `e->outil_actif` vu par mettre_a_jour(), relu par
     * preset_bouton_cb() AU MOMENT DU CLIC -- le contexte de l'écran ne doit
     * jamais être touché depuis le rappel de clic hors de ce genre de
     * lecture différée. */
    uint8_t outil_actif_connu;

    /* Rangée de préréglages : `zone_preregalges` est le conteneur flex qui
     * les porte (voir ECRAN_TEMPERATURES_PRESET_* plus haut pour
     * l'indexation), widgets à largeur égale (flex_grow). */
    lv_obj_t                         *zone_preregalges;
    lv_obj_t                         *preset_boutons[ECRAN_TEMPERATURES_PRESET_NB];
    ecran_temperatures_preset_info_t  preset_infos[ECRAN_TEMPERATURES_PRESET_NB];
} ecran_temperatures_ctx_t;

extern const ecran_desc_t ECRAN_TEMPERATURES;
