/* accueil_choix.h — helper PUR : quel écran d'accueil pour un état donné
 * (tâche 3, jalon 3b). true => l'accueil impression (ECRAN_ACCUEIL, jalon
 * 2b) ; false => l'accueil idle (ECRAN_ACCUEIL_IDLE, cette tâche). Le socle
 * (app_main.c, simulateur/main.c) appelle ceci AU DÉMARRAGE pour empiler le
 * bon écran de fond -- la bascule VIVANTE idle<->impression pendant une
 * session déjà en cours est différée à la fin du plan 3b (voir
 * task-3-brief.md), ce helper n'est donc consulté qu'une seule fois par
 * démarrage aujourd'hui, jamais en boucle depuis boucle_cycle().
 *
 * Sans dépendance LVGL (comme klipper_paliers.h) : testable au harnais hôte
 * sans afficheur, et réutilisable par un futur fork qui n'aurait pas encore
 * construit son propre écran. */
#pragma once

#include <stdbool.h>

#include "etat_klipper.h"

/* Une pause SUSPEND une impression, elle ne la termine pas : rend vrai tant
 * que `impression_en_cours` est vrai, que `impression_en_pause` le soit ou
 * non -- exactement ce que vérifie test_accueil_choix.c. `etat` NULL rend
 * faux (repos, le choix le plus sûr) plutôt que de déréférencer. */
bool accueil_impression_actif(const etat_klipper_t *etat);
