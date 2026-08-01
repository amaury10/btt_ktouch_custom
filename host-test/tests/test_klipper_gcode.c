#include "petit_test.h"
#include "klipper_gcode.h"
#include "core/etat_klipper.h"

#include <math.h>

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
}
