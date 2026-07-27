/* Sortie graphique du simulateur : fenêtre SDL, ou rendu hors écran destiné
 * à une capture PNG.
 *
 * Les deux modes partagent le MÊME format de pixel que le panneau de la
 * K-Touch (RGB565) et la même taille (800x480) : une capture montre donc les
 * pixels que l'appareil pousserait vers sa dalle, pas une approximation. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    AFFICHEUR_FENETRE = 0,   /* fenêtre SDL, interactif */
    AFFICHEUR_HORS_ECRAN,    /* aucun serveur graphique requis */
} afficheur_mode_t;

#define AFFICHEUR_LARGEUR 800
#define AFFICHEUR_HAUTEUR 480

/* Initialise LVGL et crée l'afficheur. Rend false si SDL refuse d'ouvrir une
 * fenêtre (mode FENETRE sans serveur graphique, typiquement) : l'appelant
 * peut alors retomber sur AFFICHEUR_HORS_ECRAN plutôt que de mourir. */
bool afficheur_demarrer(afficheur_mode_t mode);

/* Avance l'horloge LVGL de `ms` et traite les événements en attente. */
void afficheur_pomper(uint32_t ms);

/* Écrit la trame courante en PNG. Rend false si le mode est FENETRE (les
 * pixels vivent alors dans SDL, pas dans notre tampon), si le chemin ne peut
 * pas être ouvert, ou si l'encodage échoue. */
bool afficheur_capturer(const char *chemin_png);

void afficheur_arreter(void);
