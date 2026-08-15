#include "petit_test.h"
#include "klipper_gcode.h"
#include "core/etat_klipper.h"

#include <math.h>
#include <string.h> /* strcpy -- temoin "sortie intacte" de section_input_shaper() */

/* --- SET_INPUT_SHAPER (module Input Shaper, 2026-08-15) ------------------ */

static void section_input_shaper(void)
{
    char sortie[64];

    VERIFIER(klipper_gcode_input_shaper_type(sortie, sizeof(sortie), 'X', "mzv"));
    VERIFIER_TEXTE(sortie, "SET_INPUT_SHAPER SHAPER_TYPE_X=mzv");
    VERIFIER(klipper_gcode_input_shaper_type(sortie, sizeof(sortie), 'Y', "2hump_ei"));
    VERIFIER_TEXTE(sortie, "SET_INPUT_SHAPER SHAPER_TYPE_Y=2hump_ei");
    /* Refus SANS toucher sortie : axe invalide, type vide/NULL, injection. */
    strcpy(sortie, "temoin");
    VERIFIER(!klipper_gcode_input_shaper_type(sortie, sizeof(sortie), 'Z', "mzv"));
    VERIFIER(!klipper_gcode_input_shaper_type(sortie, sizeof(sortie), 'X', ""));
    VERIFIER(!klipper_gcode_input_shaper_type(sortie, sizeof(sortie), 'X', NULL));
    VERIFIER(!klipper_gcode_input_shaper_type(sortie, sizeof(sortie), 'X', "mzv\nM112"));
    VERIFIER_TEXTE(sortie, "temoin");

    VERIFIER(klipper_gcode_input_shaper_freq(sortie, sizeof(sortie), 'X', 41.6f));
    VERIFIER_TEXTE(sortie, "SET_INPUT_SHAPER SHAPER_FREQ_X=41.6");
    /* Bornes 10-150 : refusees dehors, acceptees aux limites. */
    strcpy(sortie, "temoin");
    VERIFIER(!klipper_gcode_input_shaper_freq(sortie, sizeof(sortie), 'X', 9.9f));
    VERIFIER(!klipper_gcode_input_shaper_freq(sortie, sizeof(sortie), 'Y', 150.1f));
    VERIFIER_TEXTE(sortie, "temoin");
    VERIFIER(klipper_gcode_input_shaper_freq(sortie, sizeof(sortie), 'Y', 10.0f));
    VERIFIER(klipper_gcode_input_shaper_freq(sortie, sizeof(sortie), 'Y', 150.0f));

    /* BED_MESH_PROFILE LOAD (liste de profils, 2026-08-15). */
    VERIFIER(klipper_gcode_bed_mesh_profil_load(sortie, sizeof(sortie), "default"));
    VERIFIER_TEXTE(sortie, "BED_MESH_PROFILE LOAD=default");
    VERIFIER(klipper_gcode_bed_mesh_profil_load(sortie, sizeof(sortie), "pei-05_lisse"));
    VERIFIER_TEXTE(sortie, "BED_MESH_PROFILE LOAD=pei-05_lisse");
    strcpy(sortie, "temoin");
    VERIFIER(!klipper_gcode_bed_mesh_profil_load(sortie, sizeof(sortie), ""));
    VERIFIER(!klipper_gcode_bed_mesh_profil_load(sortie, sizeof(sortie), NULL));
    VERIFIER(!klipper_gcode_bed_mesh_profil_load(sortie, sizeof(sortie), "nom avec espace"));
    VERIFIER(!klipper_gcode_bed_mesh_profil_load(sortie, sizeof(sortie), "x\nM112"));
    VERIFIER(!klipper_gcode_bed_mesh_profil_load(sortie, sizeof(sortie),
                                                 "nom_beaucoup_trop_long_pour_24"));
    VERIFIER_TEXTE(sortie, "temoin");
}

void suite_klipper_gcode(void)
{
    printf("suite : klipper_gcode\n");
    char g[KLIPPER_GCODE_MAX];

    /* --- jog --- */
    /* déplacement positif d'un entier : pas de décimale superflue */
    VERIFIER(klipper_gcode_jog(g, sizeof(g), 'X', 10.0f, 3000) == true);
    VERIFIER_TEXTE(g,
        "SAVE_GCODE_STATE NAME=ktouch_jog\nG91\nG1 X10 F3000\nRESTORE_GCODE_STATE NAME=ktouch_jog");
    /* déplacement négatif fractionnaire : signe et décimales, sans zéros de fin */
    VERIFIER(klipper_gcode_jog(g, sizeof(g), 'Z', -0.1f, 600) == true);
    VERIFIER_TEXTE(g,
        "SAVE_GCODE_STATE NAME=ktouch_jog\nG91\nG1 Z-0.1 F600\nRESTORE_GCODE_STATE NAME=ktouch_jog");
    /* axe invalide, distance nulle, non finie, hors borne, vitesse nulle : false, sortie intacte */
    snprintf(g, sizeof(g), "sentinelle");
    VERIFIER(klipper_gcode_jog(g, sizeof(g), 'A', 10.0f, 3000) == false);
    VERIFIER_TEXTE(g, "sentinelle");
    VERIFIER(klipper_gcode_jog(g, sizeof(g), 'X', 0.0f, 3000) == false);
    VERIFIER(klipper_gcode_jog(g, sizeof(g), 'X', 2000.0f, 3000) == false); /* > 1000 */
    VERIFIER(klipper_gcode_jog(g, sizeof(g), 'X', 10.0f, 0) == false);
    /* tampon trop court : false */
    char court[8];
    VERIFIER(klipper_gcode_jog(court, sizeof(court), 'X', 10.0f, 3000) == false);

    /* --- home --- */
    VERIFIER(klipper_gcode_home(g, sizeof(g), 0) == true);        VERIFIER_TEXTE(g, "G28");
    VERIFIER(klipper_gcode_home(g, sizeof(g), 0x07) == true);     VERIFIER_TEXTE(g, "G28"); /* tout */
    VERIFIER(klipper_gcode_home(g, sizeof(g), 0x01) == true);     VERIFIER_TEXTE(g, "G28 X");
    VERIFIER(klipper_gcode_home(g, sizeof(g), 0x05) == true);     VERIFIER_TEXTE(g, "G28 X Z");
    VERIFIER(klipper_gcode_home(g, sizeof(g), 0x02) == true);     VERIFIER_TEXTE(g, "G28 Y");
    VERIFIER(klipper_gcode_home(g, sizeof(g), 0xF8) == true);     VERIFIER_TEXTE(g, "G28"); /* bits hauts ignorés = aucun des 3 ⇒ tout */

    /* --- consigne température --- */
    VERIFIER(klipper_gcode_consigne_temp(g, sizeof(g), "extruder", 210) == true);
    VERIFIER_TEXTE(g, "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=210");
    VERIFIER(klipper_gcode_consigne_temp(g, sizeof(g), "heater_bed", 0) == true);
    VERIFIER_TEXTE(g, "SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=0");
    /* nom vide, NULL, avec caractère injectant, trop long, cible hors borne : false */
    snprintf(g, sizeof(g), "sentinelle");
    VERIFIER(klipper_gcode_consigne_temp(g, sizeof(g), "", 210) == false);
    VERIFIER_TEXTE(g, "sentinelle");
    VERIFIER(klipper_gcode_consigne_temp(g, sizeof(g), NULL, 210) == false);
    VERIFIER(klipper_gcode_consigne_temp(g, sizeof(g), "extruder\nM112", 210) == false); /* injection */
    VERIFIER(klipper_gcode_consigne_temp(g, sizeof(g), "extruder", 400) == false);       /* > 350 */

    /* --- arret d'urgence --- */
    VERIFIER(klipper_gcode_arret_urgence(g, sizeof(g)) == true);
    VERIFIER_TEXTE(g, "M112");
    /* tampon trop court : false, sortie intacte */
    snprintf(g, sizeof(g), "sentinelle");
    VERIFIER(klipper_gcode_arret_urgence(g, 2) == false);
    VERIFIER_TEXTE(g, "sentinelle");

    /* --- extrude --- */
    /* distance positive entière : pas de décimale superflue */
    VERIFIER(klipper_gcode_extrude(g, sizeof(g), 10.0f, 300) == true);
    VERIFIER_TEXTE(g,
        "SAVE_GCODE_STATE NAME=ktouch_extrude\nM83\nG1 E10 F300\nRESTORE_GCODE_STATE NAME=ktouch_extrude");
    /* distance négative (rétraction) : signe conservé */
    VERIFIER(klipper_gcode_extrude(g, sizeof(g), -10.0f, 300) == true);
    VERIFIER_TEXTE(g,
        "SAVE_GCODE_STATE NAME=ktouch_extrude\nM83\nG1 E-10 F300\nRESTORE_GCODE_STATE NAME=ktouch_extrude");
    /* distance fractionnaire : formatage décimal sans zéros de fin */
    VERIFIER(klipper_gcode_extrude(g, sizeof(g), 2.5f, 300) == true);
    VERIFIER_TEXTE(g,
        "SAVE_GCODE_STATE NAME=ktouch_extrude\nM83\nG1 E2.5 F300\nRESTORE_GCODE_STATE NAME=ktouch_extrude");
    /* distance nulle, NaN, hors borne, vitesse hors borne : false, sortie intacte */
    snprintf(g, sizeof(g), "sentinelle");
    VERIFIER(klipper_gcode_extrude(g, sizeof(g), 0.0f, 300) == false);
    VERIFIER_TEXTE(g, "sentinelle");
    VERIFIER(klipper_gcode_extrude(g, sizeof(g), nan(""), 300) == false);
    VERIFIER(klipper_gcode_extrude(g, sizeof(g), 250.0f, 300) == false); /* > 200 */
    VERIFIER(klipper_gcode_extrude(g, sizeof(g), 10.0f, 0) == false);
    VERIFIER(klipper_gcode_extrude(g, sizeof(g), 10.0f, 7000) == false); /* > 6000 */
    /* tampon trop court : false */
    char court2[8];
    VERIFIER(klipper_gcode_extrude(court2, sizeof(court2), 10.0f, 300) == false);

    /* --- activer outil --- */
    VERIFIER(klipper_gcode_activer_outil(g, sizeof(g), 0) == true);
    VERIFIER_TEXTE(g, "ACTIVATE_EXTRUDER EXTRUDER=extruder");
    VERIFIER(klipper_gcode_activer_outil(g, sizeof(g), 1) == true);
    VERIFIER_TEXTE(g, "ACTIVATE_EXTRUDER EXTRUDER=extruder1");
    VERIFIER(klipper_gcode_activer_outil(g, sizeof(g), 2) == true);
    VERIFIER_TEXTE(g, "ACTIVATE_EXTRUDER EXTRUDER=extruder2");
    /* indice >= KLIPPER_EXTRUDEURS_MAX : false, sortie intacte */
    snprintf(g, sizeof(g), "sentinelle");
    VERIFIER(klipper_gcode_activer_outil(g, sizeof(g), KLIPPER_EXTRUDEURS_MAX) == false);
    VERIFIER_TEXTE(g, "sentinelle");
    /* tampon trop court : false */
    VERIFIER(klipper_gcode_activer_outil(court, sizeof(court), 0) == false);

    /* --- ventilateur --- */
    /* pct=50 : (50*255+50)/100 = 128 */
    VERIFIER(klipper_gcode_ventilateur(g, sizeof(g), 50) == true);
    VERIFIER_TEXTE(g, "M106 S128");
    /* pct=100 : (100*255+50)/100 = 255 */
    VERIFIER(klipper_gcode_ventilateur(g, sizeof(g), 100) == true);
    VERIFIER_TEXTE(g, "M106 S255");
    /* pct=0 : (0*255+50)/100 = 0 */
    VERIFIER(klipper_gcode_ventilateur(g, sizeof(g), 0) == true);
    VERIFIER_TEXTE(g, "M106 S0");
    /* pct=25 : (25*255+50)/100 = 64 */
    VERIFIER(klipper_gcode_ventilateur(g, sizeof(g), 25) == true);
    VERIFIER_TEXTE(g, "M106 S64");
    /* pct > 100 : false, sortie intacte */
    snprintf(g, sizeof(g), "sentinelle");
    VERIFIER(klipper_gcode_ventilateur(g, sizeof(g), 101) == false);
    VERIFIER_TEXTE(g, "sentinelle");
    /* tampon trop court : false */
    char court3[8];
    VERIFIER(klipper_gcode_ventilateur(court3, sizeof(court3), 50) == false);

    /* --- imprimer fichier --- */
    VERIFIER(klipper_gcode_imprimer_fichier(g, sizeof(g), "a.gcode") == true);
    VERIFIER_TEXTE(g, "SDCARD_PRINT_FILE FILENAME=a.gcode");
    VERIFIER(klipper_gcode_imprimer_fichier(g, sizeof(g), "sub/b.gcode") == true);
    VERIFIER_TEXTE(g, "SDCARD_PRINT_FILE FILENAME=sub/b.gcode");
    /* nom NULL : false, sortie intacte */
    snprintf(g, sizeof(g), "sentinelle");
    VERIFIER(klipper_gcode_imprimer_fichier(g, sizeof(g), NULL) == false);
    VERIFIER_TEXTE(g, "sentinelle");
    /* nom vide : false */
    VERIFIER(klipper_gcode_imprimer_fichier(g, sizeof(g), "") == false);
    /* tampon trop court : false */
    char court4[8];
    VERIFIER(klipper_gcode_imprimer_fichier(court4, sizeof(court4), "a.gcode") == false);

    /* --- vitesse d'impression --- */
    VERIFIER(klipper_gcode_vitesse_impression(g, sizeof(g), 100) == true);
    VERIFIER_TEXTE(g, "M220 S100");
    VERIFIER(klipper_gcode_vitesse_impression(g, sizeof(g), 1) == true);
    VERIFIER_TEXTE(g, "M220 S1");
    VERIFIER(klipper_gcode_vitesse_impression(g, sizeof(g), 300) == true);
    VERIFIER_TEXTE(g, "M220 S300");
    /* bornes exactes rejetées : 0 et 301 */
    snprintf(g, sizeof(g), "sentinelle");
    VERIFIER(klipper_gcode_vitesse_impression(g, sizeof(g), 0) == false);
    VERIFIER_TEXTE(g, "sentinelle");
    VERIFIER(klipper_gcode_vitesse_impression(g, sizeof(g), 301) == false);
    /* tampon trop court : false */
    VERIFIER(klipper_gcode_vitesse_impression(court, sizeof(court), 100) == false);

    /* --- flux --- */
    VERIFIER(klipper_gcode_flux(g, sizeof(g), 100) == true);
    VERIFIER_TEXTE(g, "M221 S100");
    /* bornes exactes rejetées : 0 et 301 */
    snprintf(g, sizeof(g), "sentinelle");
    VERIFIER(klipper_gcode_flux(g, sizeof(g), 0) == false);
    VERIFIER_TEXTE(g, "sentinelle");
    VERIFIER(klipper_gcode_flux(g, sizeof(g), 301) == false);
    /* tampon trop court : false */
    VERIFIER(klipper_gcode_flux(court, sizeof(court), 100) == false);

    /* --- offset Z --- */
    VERIFIER(klipper_gcode_offset_z(g, sizeof(g), 50, false) == true);
    VERIFIER_TEXTE(g, "SET_GCODE_OFFSET Z_ADJUST=0.05 MOVE=1");
    VERIFIER(klipper_gcode_offset_z(g, sizeof(g), -10, false) == true);
    VERIFIER_TEXTE(g, "SET_GCODE_OFFSET Z_ADJUST=-0.01 MOVE=1");
    VERIFIER(klipper_gcode_offset_z(g, sizeof(g), 2000, false) == true);
    VERIFIER_TEXTE(g, "SET_GCODE_OFFSET Z_ADJUST=2 MOVE=1");
    VERIFIER(klipper_gcode_offset_z(g, sizeof(g), 0, true) == true);
    VERIFIER_TEXTE(g, "SET_GCODE_OFFSET Z=0 MOVE=1");
    /* delta nul sans reset, delta hors borne : false, sortie intacte */
    snprintf(g, sizeof(g), "sentinelle");
    VERIFIER(klipper_gcode_offset_z(g, sizeof(g), 0, false) == false);
    VERIFIER_TEXTE(g, "sentinelle");
    VERIFIER(klipper_gcode_offset_z(g, sizeof(g), 2001, false) == false);
    VERIFIER(klipper_gcode_offset_z(g, sizeof(g), -2001, false) == false);
    /* tampon trop court : false, sur les deux formes (reset et ajustement) */
    VERIFIER(klipper_gcode_offset_z(court, sizeof(court), 50, false) == false);
    VERIFIER(klipper_gcode_offset_z(court, sizeof(court), 0, true) == false);

    /* --- calibration Z --- */
    VERIFIER(klipper_gcode_calibration_z(g, sizeof(g), KLIPPER_ZCAL_PROBE) == true);
    VERIFIER_TEXTE(g, "PROBE_CALIBRATE");
    VERIFIER(klipper_gcode_calibration_z(g, sizeof(g), KLIPPER_ZCAL_ENDSTOP) == true);
    VERIFIER_TEXTE(g, "Z_ENDSTOP_CALIBRATE");
    VERIFIER(klipper_gcode_calibration_z(g, sizeof(g), KLIPPER_ZCAL_ACCEPT) == true);
    VERIFIER_TEXTE(g, "ACCEPT");
    VERIFIER(klipper_gcode_calibration_z(g, sizeof(g), KLIPPER_ZCAL_ABORT) == true);
    VERIFIER_TEXTE(g, "ABORT");
    /* tampon trop court : false */
    VERIFIER(klipper_gcode_calibration_z(court, sizeof(court), KLIPPER_ZCAL_PROBE) == false);

    /* --- TESTZ --- */
    VERIFIER(klipper_gcode_testz(g, sizeof(g), 50) == true);
    VERIFIER_TEXTE(g, "TESTZ Z=+0.05");
    VERIFIER(klipper_gcode_testz(g, sizeof(g), -50) == true);
    VERIFIER_TEXTE(g, "TESTZ Z=-0.05");
    VERIFIER(klipper_gcode_testz(g, sizeof(g), 5000) == true);
    VERIFIER_TEXTE(g, "TESTZ Z=+5");
    /* delta nul, hors borne : false, sortie intacte */
    snprintf(g, sizeof(g), "sentinelle");
    VERIFIER(klipper_gcode_testz(g, sizeof(g), 0) == false);
    VERIFIER_TEXTE(g, "sentinelle");
    VERIFIER(klipper_gcode_testz(g, sizeof(g), 5001) == false);
    VERIFIER(klipper_gcode_testz(g, sizeof(g), -5001) == false);
    /* tampon trop court : false */
    VERIFIER(klipper_gcode_testz(court, sizeof(court), 50) == false);

    /* --- niveau du lit --- */
    VERIFIER(klipper_gcode_niveau_lit(g, sizeof(g), KLIPPER_LIT_SCREWS) == true);
    VERIFIER_TEXTE(g, "SCREWS_TILT_CALCULATE");
    VERIFIER(klipper_gcode_niveau_lit(g, sizeof(g), KLIPPER_LIT_ZTILT) == true);
    VERIFIER_TEXTE(g, "Z_TILT_ADJUST");
    VERIFIER(klipper_gcode_niveau_lit(g, sizeof(g), KLIPPER_LIT_QGL) == true);
    VERIFIER_TEXTE(g, "QUAD_GANTRY_LEVEL");
    VERIFIER(klipper_gcode_niveau_lit(g, sizeof(g), KLIPPER_LIT_DISABLE) == true);
    VERIFIER_TEXTE(g, "M84");
    /* tampon trop court : false */
    VERIFIER(klipper_gcode_niveau_lit(court, sizeof(court), KLIPPER_LIT_SCREWS) == false);

    /* --- limite de vitesse --- */
    VERIFIER(klipper_gcode_limite_vitesse(g, sizeof(g), KLIPPER_LIM_VELOCITY, 250) == true);
    VERIFIER_TEXTE(g, "SET_VELOCITY_LIMIT VELOCITY=250");
    VERIFIER(klipper_gcode_limite_vitesse(g, sizeof(g), KLIPPER_LIM_ACCEL, 3000) == true);
    VERIFIER_TEXTE(g, "SET_VELOCITY_LIMIT ACCEL=3000");
    VERIFIER(klipper_gcode_limite_vitesse(g, sizeof(g), KLIPPER_LIM_SQV, 5) == true);
    VERIFIER_TEXTE(g, "SET_VELOCITY_LIMIT SQUARE_CORNER_VELOCITY=5");
    VERIFIER(klipper_gcode_limite_vitesse(g, sizeof(g), KLIPPER_LIM_ACCEL_TO_DECEL, 1500) == true);
    VERIFIER_TEXTE(g, "SET_VELOCITY_LIMIT ACCEL_TO_DECEL=1500");
    VERIFIER(klipper_gcode_limite_vitesse(g, sizeof(g), KLIPPER_LIM_VELOCITY, 100000) == true);
    VERIFIER_TEXTE(g, "SET_VELOCITY_LIMIT VELOCITY=100000");
    /* valeur nulle, hors borne haute : false, sortie intacte */
    snprintf(g, sizeof(g), "sentinelle");
    VERIFIER(klipper_gcode_limite_vitesse(g, sizeof(g), KLIPPER_LIM_VELOCITY, 0) == false);
    VERIFIER_TEXTE(g, "sentinelle");
    VERIFIER(klipper_gcode_limite_vitesse(g, sizeof(g), KLIPPER_LIM_VELOCITY, 100001) == false);
    /* tampon trop court : false */
    VERIFIER(klipper_gcode_limite_vitesse(court, sizeof(court), KLIPPER_LIM_VELOCITY, 250) == false);

    /* --- rétraction : longueurs (µm -> mm) --- */
    VERIFIER(klipper_gcode_retraction_longueur(g, sizeof(g), KLIPPER_RETR_LENGTH, 1500) == true);
    VERIFIER_TEXTE(g, "SET_RETRACTION RETRACT_LENGTH=1.5");
    VERIFIER(klipper_gcode_retraction_longueur(g, sizeof(g), KLIPPER_RETR_EXTRA, 0) == true);
    VERIFIER_TEXTE(g, "SET_RETRACTION UNRETRACT_EXTRA_LENGTH=0");
    VERIFIER(klipper_gcode_retraction_longueur(g, sizeof(g), KLIPPER_RETR_LENGTH, 20000) == true);
    VERIFIER_TEXTE(g, "SET_RETRACTION RETRACT_LENGTH=20");
    /* champs de vitesse rejetés ici, borne haute rejetée : false, sortie intacte */
    snprintf(g, sizeof(g), "sentinelle");
    VERIFIER(klipper_gcode_retraction_longueur(g, sizeof(g), KLIPPER_RETR_SPEED, 40) == false);
    VERIFIER_TEXTE(g, "sentinelle");
    VERIFIER(klipper_gcode_retraction_longueur(g, sizeof(g), KLIPPER_RETR_UNRETRACT_SPEED, 40) == false);
    VERIFIER(klipper_gcode_retraction_longueur(g, sizeof(g), KLIPPER_RETR_LENGTH, 20001) == false);
    /* tampon trop court : false */
    VERIFIER(klipper_gcode_retraction_longueur(court, sizeof(court), KLIPPER_RETR_LENGTH, 1500) == false);

    /* --- rétraction : vitesses (mm/s entier) --- */
    VERIFIER(klipper_gcode_retraction_vitesse(g, sizeof(g), KLIPPER_RETR_SPEED, 40) == true);
    VERIFIER_TEXTE(g, "SET_RETRACTION RETRACT_SPEED=40");
    VERIFIER(klipper_gcode_retraction_vitesse(g, sizeof(g), KLIPPER_RETR_UNRETRACT_SPEED, 1000) == true);
    VERIFIER_TEXTE(g, "SET_RETRACTION UNRETRACT_SPEED=1000");
    VERIFIER(klipper_gcode_retraction_vitesse(g, sizeof(g), KLIPPER_RETR_SPEED, 1) == true);
    VERIFIER_TEXTE(g, "SET_RETRACTION RETRACT_SPEED=1");
    /* champs de longueur rejetés ici, borne basse/haute rejetées : false, sortie intacte */
    snprintf(g, sizeof(g), "sentinelle");
    VERIFIER(klipper_gcode_retraction_vitesse(g, sizeof(g), KLIPPER_RETR_LENGTH, 40) == false);
    VERIFIER_TEXTE(g, "sentinelle");
    VERIFIER(klipper_gcode_retraction_vitesse(g, sizeof(g), KLIPPER_RETR_EXTRA, 40) == false);
    VERIFIER(klipper_gcode_retraction_vitesse(g, sizeof(g), KLIPPER_RETR_SPEED, 0) == false);
    VERIFIER(klipper_gcode_retraction_vitesse(g, sizeof(g), KLIPPER_RETR_SPEED, 1001) == false);
    /* tampon trop court : false */
    VERIFIER(klipper_gcode_retraction_vitesse(court, sizeof(court), KLIPPER_RETR_SPEED, 40) == false);
    section_input_shaper();
}
