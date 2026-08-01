/* klipper_gcode.h — construit des scripts gcode Klipper, EN FONCTIONS PURES
 * (aucun réseau, aucune allocation) : c'est la logique métier « quel gcode
 * pour cette action UI », testable entièrement sur PC. Un écran les appelle
 * puis remet le résultat à ui_commander(BACKEND_ACTION_GCODE, {"script":...}).
 * Chaque fonction rend false SANS toucher `sortie` si un argument est
 * invalide ou si le tampon est trop court (jamais de troncature silencieuse
 * rendue comme un succès — leçon du 2b). */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Longueur de tampon suffisante pour tout script produit ici (le plus long
 * est le jog avec SAVE/RESTORE_GCODE_STATE, ~110 octets). */
#define KLIPPER_GCODE_MAX 160

/* Déplacement relatif d'UN axe, borné par SAVE/RESTORE_GCODE_STATE pour ne
 * jamais laisser la machine en mode relatif ni changer sa vitesse courante :
 *   SAVE_GCODE_STATE NAME=ktouch_jog
 *   G91
 *   G1 <axe><distance signée> F<vitesse>
 *   RESTORE_GCODE_STATE NAME=ktouch_jog
 * `axe` ∈ {'X','Y','Z'} (majuscule) ; `distance_mm` non nul et fini, borné à
 * ±1000 mm ; `vitesse_mm_min` ∈ [1, 60000]. La distance est formatée avec au
 * plus 2 décimales, sans zéros de fin superflus. */
bool klipper_gcode_jog(char *sortie, size_t taille,
                       char axe, float distance_mm, uint16_t vitesse_mm_min);

/* Référencement. `axes_masque` reprend la convention de
 * etat_klipper_t::axes_references (bit0=X bit1=Y bit2=Z). 0 OU 0b111 ⇒ "G28"
 * (tout) ; sinon "G28" suivi des seuls axes demandés dans l'ordre X Y Z
 * (ex. bit0|bit2 ⇒ "G28 X Z"). Un bit hors des 3 de poids faible est ignoré.
 * Choix figé (task 1, jalon 3b) : un masque dont AUCUN des trois bits de
 * poids faible n'est levé (ex. 0xF8, uniquement des bits hauts) équivaut à
 * « tout référencer » (même sortie que masque=0), plutôt que de rendre
 * false ou "G28" sans axe -- un appelant qui ne connaît QUE des bits hauts
 * (bug amont) obtient malgré tout un homing complet plutôt qu'une commande
 * vide envoyée à Klipper. */
bool klipper_gcode_home(char *sortie, size_t taille, uint8_t axes_masque);

/* Consigne de température d'un chauffeur nommé, via la commande Klipper
 * générique (fonctionne pour extrudeurs ET plateau, contrairement à
 * M104/M140) :
 *   SET_HEATER_TEMPERATURE HEATER=<chauffeur> TARGET=<cible_c>
 * `chauffeur` non NULL, non vide, ≤ 32 octets, uniquement [A-Za-z0-9_] (nom
 * d'objet Klipper — un caractère hors de ce jeu ⇒ false, jamais injecté dans
 * le gcode). `cible_c` ∈ [0, 350] (0 = éteindre). */
bool klipper_gcode_consigne_temp(char *sortie, size_t taille,
                                 const char *chauffeur, uint16_t cible_c);

/* Arrêt d'urgence, via la commande M112 :
 *   M112
 * Fonction simple sans paramètres. */
bool klipper_gcode_arret_urgence(char *sortie, size_t taille);

/* Extrusion ou rétraction d'une distance donnée, en mode relatif, avec bordure
 * SAVE/RESTORE_GCODE_STATE pour ne jamais laisser la machine en mode absolu :
 *   SAVE_GCODE_STATE NAME=ktouch_extrude
 *   M83
 *   G1 E<distance signée> F<vitesse>
 *   RESTORE_GCODE_STATE NAME=ktouch_extrude
 * `distance_mm` signé (négatif = rétraction), non nul et fini, borné à ±200 mm ;
 * `vitesse_mm_min` ∈ [1, 6000]. La distance est formatée avec au plus 2
 * décimales, sans zéros de fin superflus. */
bool klipper_gcode_extrude(char *sortie, size_t taille,
                           float distance_mm, uint16_t vitesse_mm_min);

/* Active un outil (extrudeur) par son indice, via la commande ACTIVATE_EXTRUDER.
 * Pour indice 0, produit "ACTIVATE_EXTRUDER EXTRUDER=extruder" ; pour indice
 * ≥ 1, produit "...EXTRUDER=extruder<indice>" (ex. indice=2 → "extruder2").
 * `indice` < KLIPPER_EXTRUDEURS_MAX. */
bool klipper_gcode_activer_outil(char *sortie, size_t taille, uint8_t indice);

/* Pilote un ventilateur par pourcentage de vitesse, via la commande M106 :
 *   M106 S<valeur>
 * où `valeur` = (pct * 255 + 50) / 100 (arrondi entier au plus proche).
 * `pct` ∈ [0, 100] ; M106 S0 éteint le ventilateur (équivalent M107). */
bool klipper_gcode_ventilateur(char *sortie, size_t taille, uint8_t pct);

/* Démarre l'impression d'un fichier gcode déjà présent sur la carte SD
 * virtuelle Moonraker, via la commande Klipper générique :
 *   SDCARD_PRINT_FILE FILENAME=<nom>
 * `nom` vient de la liste renvoyée par Moonraker (rpc_lire_fichiers, chemin
 * déjà valide vis-à-vis du protocole -- éventuellement avec des '/' pour un
 * sous-dossier, ex. "sub/b.gcode") : recopié TEL QUEL, cette fonction ne le
 * valide pas au-delà de sa longueur (contrairement à
 * klipper_gcode_consigne_temp() dont le paramètre `chauffeur` vient d'un nom
 * d'objet Klipper qu'un appelant pourrait fabriquer à la main). Rend false
 * SANS toucher `sortie` si `nom` est NULL ou vide, ou si le tampon est trop
 * court -- jamais de troncature silencieuse rendue comme un succès (même
 * politique que le reste de ce fichier). */
bool klipper_gcode_imprimer_fichier(char *sortie, size_t taille, const char *nom);
