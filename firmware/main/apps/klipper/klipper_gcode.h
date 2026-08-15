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

/* Module Input Shaper (spec 2026-08-15) : SET_INPUT_SHAPER, un champ a la
 * fois -- meme contrat "faux SANS toucher sortie" que le reste du fichier.
 * `axe` : 'X' ou 'Y' uniquement. `type` : uniquement alphanumerique +
 * underscore (meme barriere anti-injection que klipper_gcode_consigne_temp),
 * non vide, <= 16 octets. `freq` : bornes 10.0-150.0 Hz VALIDEES ICI (le
 * point host-testable, l'ecran ne fait que notifier sur faux). */
bool klipper_gcode_input_shaper_type(char *sortie, size_t taille, char axe, const char *type);
bool klipper_gcode_input_shaper_freq(char *sortie, size_t taille, char axe, float freq);

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

/* Vitesse d'impression, via M220 :
 *   M220 S<pct>
 * `pct` ∈ [1, 300]. */
bool klipper_gcode_vitesse_impression(char *sortie, size_t taille, uint16_t pct);

/* Flux d'extrusion, via M221 :
 *   M221 S<pct>
 * `pct` ∈ [1, 300]. */
bool klipper_gcode_flux(char *sortie, size_t taille, uint16_t pct);

/* Décalage Z courant (babystepping persistant), via SET_GCODE_OFFSET :
 *   reset=true  -> "SET_GCODE_OFFSET Z=0 MOVE=1" (delta_um ignoré)
 *   reset=false -> "SET_GCODE_OFFSET Z_ADJUST=<delta_mm> MOVE=1"
 * Si !reset, `delta_um` ∈ [-2000, 2000] et non nul (un delta nul n'est un
 * script valide QUE via reset=true). Formaté en mm, ≤3 décimales, sans zéro
 * de fin superflu. */
bool klipper_gcode_offset_z(char *sortie, size_t taille, int32_t delta_um, bool reset);

typedef enum { KLIPPER_ZCAL_PROBE, KLIPPER_ZCAL_ENDSTOP,
               KLIPPER_ZCAL_ACCEPT, KLIPPER_ZCAL_ABORT } klipper_zcal_action_t;
/* Étapes de l'assistant de calibration Z (écran dédié) :
 *   PROBE   -> "PROBE_CALIBRATE"
 *   ENDSTOP -> "Z_ENDSTOP_CALIBRATE"
 *   ACCEPT  -> "ACCEPT"
 *   ABORT   -> "ABORT" */
bool klipper_gcode_calibration_z(char *sortie, size_t taille, klipper_zcal_action_t action);

/* Ajustement Z pendant la calibration (TESTZ), via :
 *   TESTZ Z=<±delta_mm>
 * `delta_um` ∈ [-5000, 5000], non nul. Signe explicite ('+' ou '-') même
 * pour une valeur positive -- c'est la syntaxe attendue par Klipper pour
 * TESTZ. Formaté en mm, ≤3 décimales, sans zéro de fin superflu. */
bool klipper_gcode_testz(char *sortie, size_t taille, int32_t delta_um);

typedef enum { KLIPPER_LIT_SCREWS, KLIPPER_LIT_ZTILT,
               KLIPPER_LIT_QGL, KLIPPER_LIT_DISABLE } klipper_lit_action_t;
/* Mise à niveau du plateau :
 *   SCREWS  -> "SCREWS_TILT_CALCULATE"
 *   ZTILT   -> "Z_TILT_ADJUST"
 *   QGL     -> "QUAD_GANTRY_LEVEL"
 *   DISABLE -> "M84" (coupe les moteurs -- sortie de l'assistant) */
bool klipper_gcode_niveau_lit(char *sortie, size_t taille, klipper_lit_action_t action);

typedef enum { KLIPPER_LIM_VELOCITY, KLIPPER_LIM_ACCEL,
               KLIPPER_LIM_SQV, KLIPPER_LIM_ACCEL_TO_DECEL } klipper_lim_champ_t;
/* Limites de vitesse/accélération globales, via SET_VELOCITY_LIMIT :
 *   VELOCITY        -> "SET_VELOCITY_LIMIT VELOCITY=<v>"
 *   ACCEL           -> "SET_VELOCITY_LIMIT ACCEL=<v>"
 *   SQV             -> "SET_VELOCITY_LIMIT SQUARE_CORNER_VELOCITY=<v>"
 *   ACCEL_TO_DECEL  -> "SET_VELOCITY_LIMIT ACCEL_TO_DECEL=<v>"
 * `valeur` entier ∈ [1, 100000]. */
bool klipper_gcode_limite_vitesse(char *sortie, size_t taille, klipper_lim_champ_t champ, uint32_t valeur);

typedef enum { KLIPPER_RETR_LENGTH, KLIPPER_RETR_SPEED,
               KLIPPER_RETR_EXTRA, KLIPPER_RETR_UNRETRACT_SPEED } klipper_retr_champ_t;
/* Longueurs de rétraction, via SET_RETRACTION (uniquement les deux champs de
 * longueur du même enum -- les deux champs de vitesse rendent false ici,
 * voir klipper_gcode_retraction_vitesse() ci-dessous) :
 *   LENGTH -> "SET_RETRACTION RETRACT_LENGTH=<mm>"
 *   EXTRA  -> "SET_RETRACTION UNRETRACT_EXTRA_LENGTH=<mm>"
 * `valeur_um` ∈ [0, 20000] (0 accepté -- désactive la rétraction). Formaté
 * en mm, ≤3 décimales, sans zéro de fin superflu.
 * Choix figé (task 1, jalon panneaux) : SET_RETRACTION mélange dans un même
 * enum brief des champs en µm (longueurs) et des champs en mm/s entier
 * (vitesses) -- domaines non convertibles l'un dans l'autre. Plutôt qu'une
 * seule fonction ambiguë sur l'unité de son paramètre selon `champ`, on
 * découpe en deux fonctions dont le NOM porte l'unité, chacune ne reconnaissant
 * que les deux valeurs d'enum de son domaine. */
bool klipper_gcode_retraction_longueur(char *sortie, size_t taille, klipper_retr_champ_t champ, uint32_t valeur_um);

/* Vitesses de rétraction, via SET_RETRACTION (uniquement les deux champs de
 * vitesse du même enum -- les deux champs de longueur rendent false ici,
 * voir klipper_gcode_retraction_longueur() ci-dessus) :
 *   SPEED            -> "SET_RETRACTION RETRACT_SPEED=<mm_s>"
 *   UNRETRACT_SPEED  -> "SET_RETRACTION UNRETRACT_SPEED=<mm_s>"
 * `mm_s` entier ∈ [1, 1000] -- des mm/s, PAS des µm (contrairement au
 * paramètre `valeur_um` de klipper_gcode_retraction_longueur() ; voir la
 * note ci-dessus sur la scission des deux fonctions). */
bool klipper_gcode_retraction_vitesse(char *sortie, size_t taille, klipper_retr_champ_t champ, uint32_t mm_s);
