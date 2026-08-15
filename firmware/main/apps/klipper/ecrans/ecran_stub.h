/* Sous-projet "panneaux KlipperScreen", tache 7 : panneaux PLACEHOLDER pour
 * des ecrans KlipperScreen dont le backend n'existe pas encore dans ce depot
 * -- Bed Mesh (donnees de maillage), Input Shaper (test de resonance),
 * Spoolman (serveur externe) et Console (capture de gcode_response). Chacun
 * affiche uniquement son titre et UNE ligne d'explication : AUCUNE fausse
 * donnee, AUCUNE action -- meme discipline que le placeholder "Probe Offset:
 * -" de ecran_zcalibrate.c (voir son commentaire de tete) et la
 * visualisation "coins" volontairement absente de ecran_niveau_lit.c, mais
 * poussee ici jusqu'a l'ecran entier : tant que le backend correspondant
 * n'existe pas (etat_klipper.h, klipper_gcode.h, moonraker_ws.c), inventer
 * un chiffre ou un bouton reviendrait a mentir sur ce que la machine fait
 * reellement.
 *
 * Le sixieme stub d'origine, Updater (OTA), est parti dans un fichier a part
 * (ecran_updater.{h,c}) depuis Task 2 (jalon OTA firmware) : le backend
 * existe desormais partiellement (esp_ota_get_running_partition()/
 * esp_app_get_description(), deja utilises par web.c) -- ECRAN_UPDATER
 * affiche donc un vrai etat en lecture seule au lieu du placeholder "Requires
 * OTA - unavailable on this firmware". Le symbole ECRAN_UPDATER N'EST PLUS
 * declare ici, pour ne jamais entrer en collision avec celui de
 * ecran_updater.h -- voir ce fichier pour son contrat.
 *
 * Power (API power de Moonraker) est parti de la meme facon (feature "Power
 * devices Moonraker", tache B) : le backend existe desormais (store dedie
 * power_devices.h, cablage WS dans moonraker_ws.c) -- ECRAN_POWER affiche
 * donc une vraie liste de prises au lieu du placeholder "Requires Moonraker
 * power API - not yet available". Le symbole ECRAN_POWER N'EST PLUS declare
 * ici, pour ne jamais entrer en collision avec celui de ecran_power.h -- voir
 * ce fichier pour son contrat.
 *
 * Console est parti de la meme facon (feature "Console gcode", tache B --
 * integration ESP) : le backend existe desormais (store dedie console_log.h,
 * tache A, cablage WS par notify_gcode_response dans moonraker_ws.c) --
 * ECRAN_CONSOLE affiche donc un vrai scrollback + une saisie clavier au lieu
 * du placeholder "Requires gcode_response capture - not yet available". Le
 * symbole ECRAN_CONSOLE N'EST PLUS declare ici, pour ne jamais entrer en
 * collision avec celui de ecran_console.h -- voir ce fichier pour son
 * contrat.
 *
 * `mettre_a_jour = NULL` et `detruire = NULL` pour les quatre restants --
 * rien de dynamique a rafraichir, rien a liberer au-dela du contexte (ici de
 * taille 0, voir ecran.h : "un ecran purement statique... peut laisser ce
 * pointeur a NULL"). Cablage dans le sous-menu Configuration : voir
 * ecran_menu_reglages.c.
 *
 * Quatre paires construire/desc triviales generees par macro X-macro
 * (STUBS() dans le .c) plutot qu'un unique construire partage qui devrait
 * retrouver QUEL stub il construit via une table externe -- brief de la
 * tache, option (a) : chaque construire genere ferme sur ses propres
 * litteraux (titre + explication), pas de lookup a l'execution. */
#pragma once

#include "ecran.h"

/* ECRAN_BED_MESH et ECRAN_INPUT_SHAPER ne sont plus des stubs depuis le
 * 2026-08-15 -- voir ecran_bed_mesh.h / ecran_input_shaper.h. */
extern const ecran_desc_t ECRAN_SPOOLMAN;
