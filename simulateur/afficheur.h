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

/* Initialise LVGL et crée l'afficheur.
 *
 * Rend false dans deux cas distincts :
 * - l'afficheur est déjà démarré (un afficheur_demarrer() en cours n'est
 *   jamais remplacé silencieusement par un second : appeler
 *   afficheur_arreter() d'abord). Même politique que boucle_demarrer()
 *   (firmware/main/core/boucle.c), qui rend ESP_ERR_INVALID_STATE sur un
 *   second appel plutôt que de créer une seconde tâche.
 * - en mode FENETRE, SDL_Init(SDL_INIT_VIDEO) échoue, OU réussit mais sur
 *   l'un des pilotes de repli "dummy"/"offscreen" de SDL2 (aucun des deux
 *   n'affiche jamais rien à un utilisateur). C'est le signe le plus fiable
 *   d'un serveur graphique absent que ce module puisse observer, mais pas
 *   une garantie absolue — voir la limite ci-dessous. L'appelant peut
 *   alors retomber sur AFFICHEUR_HORS_ECRAN.
 *
 * Limite assumée sur ce second cas : au-delà de SDL_Init et du nom du
 * pilote retenu, le pilote SDL vendorisé de LVGL
 * (simulateur/lvgl/src/drivers/sdl/lv_sdl_window.c) n'examine aucun code de
 * retour de SDL_CreateWindow/SDL_CreateRenderer/SDL_CreateTexture — un
 * échec à ce niveau plus profond, avec un pilote pourtant réel (x11,
 * wayland), resterait invisible ici aussi. Vérifié empiriquement (voir
 * simulateur/README.md) en deux temps : d'abord que sans aucune
 * vérification, ce mode rendait true en tête de processus (DISPLAY et
 * WAYLAND_DISPLAY vides) — l'exact contraire de ce que ce commentaire
 * promettait alors ; ensuite qu'un simple contrôle du retour de SDL_Init
 * ne suffisait PAS à corriger ça, parce que SDL2 sur cette machine
 * retombe silencieusement sur son pilote "offscreen" (SDL_Init rend 0
 * quand même) dès que x11 et wayland échouent — d'où le rejet explicite de
 * "dummy" et "offscreen" par leur nom. */
bool afficheur_demarrer(afficheur_mode_t mode);

/* Avance l'horloge LVGL de `ms` et traite les événements en attente. */
void afficheur_pomper(uint32_t ms);

/* Écrit la trame courante en PNG. Rend false si le mode est FENETRE (les
 * pixels vivent alors dans SDL, pas dans notre tampon), si le chemin ne peut
 * pas être ouvert, ou si l'encodage échoue. */
bool afficheur_capturer(const char *chemin_png);

/* Arrête l'afficheur et rend le module à son état d'avant tout démarrage :
 * supprime l'affichage LVGL, ferme SDL (SDL_Quit, en mode FENETRE
 * uniquement) et appelle lv_deinit(). Un cycle démarrer/arrêter/démarrer
 * est donc censé être propre — vérifié sous ASan+UBSan avec un harnais
 * temporaire (voir task-1-report.md, section réentrance). Si l'afficheur
 * n'a jamais démarré, ou a déjà été arrêté, ne fait rien : sûr à appeler
 * plusieurs fois de suite. */
void afficheur_arreter(void);
