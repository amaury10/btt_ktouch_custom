/* klipper_paliers.h — CHOIX PUR du palier d'affichage des chauffeurs en
 * fonction du nombre d'extrudeurs présents (spec §6). Extrait ici, une seule
 * fois, pour que l'accueil idle, l'accueil impression (jalon suivant) et le
 * panneau filament (3e) affichent tous les outils de la même façon au même
 * nombre de têtes — jamais recalculé à la main écran par écran. */
#pragma once
#include <stdint.h>

typedef enum {
    PALIER_MONO = 0,   /* 1 tête : grandes tuiles (police 48) */
    PALIER_MOYEN,      /* 2-4 têtes : grille 2x2, police 28, outil actif marqué */
    PALIER_COMPACT,    /* 5-8 têtes : grille 2x4, police 20, tap => détail */
} palier_outils_t;

/* 0 ou 1 => MONO ; 2..4 => MOYEN ; >=5 => COMPACT. Une machine sans extrudeur
 * annoncé (0) retombe sur MONO plutôt que sur une grille vide. */
palier_outils_t palier_outils(uint8_t nb_extrudeurs);

/* Nombre de colonnes de la grille de chauffeurs pour un palier donné :
 * MONO=1 (une tuile), MOYEN=2, COMPACT=2. (Le nombre de lignes se déduit du
 * nombre de chauffeurs présents et des colonnes, côté écran.) */
uint8_t palier_colonnes(palier_outils_t palier);

/* Police LVGL (taille en points) de la VALEUR de température pour un palier :
 * MONO=48, MOYEN=28, COMPACT=20. Rendue en int (pas un lv_font_t*, pour que
 * ce fichier reste sans dépendance LVGL et testable sur PC) ; l'écran
 * convertit en &lv_font_montserrat_<n>. */
uint8_t palier_taille_police(palier_outils_t palier);
