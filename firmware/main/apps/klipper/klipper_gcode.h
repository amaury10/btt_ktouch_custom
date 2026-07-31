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
