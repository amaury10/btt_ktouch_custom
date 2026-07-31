/* selecteur_pas.h — widget « 0.1 / 1 / 10 / 100 mm » : quatre boutons
 * mutuellement exclusifs, un seul actif à la fois. Le pas courant est lu par
 * l'écran au moment d'un appui sur le pad de jog (voir ecran_deplacer.c,
 * tâche 4, refonte accueil/déplacer) — ce widget ne connaît rien de klipper_gcode.h ni de
 * ui_commander(), exactement comme tuile.h ne connaît aucune unité : un futur
 * fork non-Klipper le réutilise tel quel pour n'importe quel réglage à choix
 * discret.
 *
 * Structure à champs publics vivant dans le contexte de l'écran, JAMAIS
 * allouée par ce widget — même forme que tuile_t/progression_t (voir leurs
 * en-têtes respectifs) : ce fichier ne fait jamais qu'y ranger des lv_obj_t*
 * déjà créés, et l'écran appelant garde le droit de repositionner/
 * redimensionner `racine` après coup (même politique que progression_t). */
#pragma once
#include "lvgl.h"

typedef struct {
    lv_obj_t *racine;
    lv_obj_t *boutons[4];
    uint8_t   index_actif;    /* 0..3, indice dans SELECTEUR_PAS_MM */
} selecteur_pas_t;

/* Pas en mm indexés : {0.1, 1, 10, 100}. */
extern const float SELECTEUR_PAS_MM[4];

/* Crée les cinq objets LVGL (racine + 4 boutons) et les range dans `s` (déjà
 * alloué par l'appelant, voir le commentaire de tête). `index_actif` démarre
 * à 1 (1 mm) — un pas raisonnable par défaut, ni le plus fin (0.1, trop lent
 * pour un premier jog) ni le plus grossier (100, risqué en aveugle). `s` ou
 * `parent` NULL : ne fait rien plutôt que déréférencer. */
void selecteur_pas_creer(selecteur_pas_t *s, lv_obj_t *parent);

/* SELECTEUR_PAS_MM[index_actif] ; 1.0f (le défaut de index_actif) si `s` est
 * NULL plutôt que de déréférencer. */
float selecteur_pas_valeur(const selecteur_pas_t *s);
