/* Sous-projet "panneaux KlipperScreen", tache 7 : six panneaux PLACEHOLDER
 * pour des ecrans KlipperScreen dont le backend n'existe pas encore dans ce
 * depot -- Power (API power de Moonraker), Bed Mesh (donnees de maillage),
 * Input Shaper (test de resonance), Spoolman (serveur externe), Updater
 * (OTA) et Console (capture de gcode_response). Chacun affiche uniquement
 * son titre et UNE ligne d'explication : AUCUNE fausse donnee, AUCUNE
 * action -- meme discipline que le placeholder "Probe Offset: -" de
 * ecran_zcalibrate.c (voir son commentaire de tete) et la visualisation
 * "coins" volontairement absente de ecran_niveau_lit.c, mais poussee ici
 * jusqu'a l'ecran entier : tant que le backend correspondant n'existe pas
 * (etat_klipper.h, klipper_gcode.h, moonraker_ws.c), inventer un chiffre ou
 * un bouton reviendrait a mentir sur ce que la machine fait reellement.
 *
 * `mettre_a_jour = NULL` et `detruire = NULL` pour les six -- rien de
 * dynamique a rafraichir, rien a liberer au-dela du contexte (ici de taille
 * 0, voir ecran.h : "un ecran purement statique... peut laisser ce pointeur
 * a NULL"). Cablage dans le sous-menu Configuration reporte a la tache 8
 * (brief de cette tache) : ce module n'expose que les six descripteurs.
 *
 * Six paires construire/desc triviales generees par macro X-macro (STUBS()
 * dans le .c) plutot qu'un unique construire partage qui devrait retrouver
 * QUEL stub il construit via une table externe -- brief de la tache,
 * option (a) : chaque construire genere ferme sur ses propres litteraux
 * (titre + explication), pas de lookup a l'execution. */
#pragma once

#include "ecran.h"

extern const ecran_desc_t ECRAN_POWER;
extern const ecran_desc_t ECRAN_BED_MESH;
extern const ecran_desc_t ECRAN_INPUT_SHAPER;
extern const ecran_desc_t ECRAN_SPOOLMAN;
extern const ecran_desc_t ECRAN_UPDATER;
extern const ecran_desc_t ECRAN_CONSOLE;
