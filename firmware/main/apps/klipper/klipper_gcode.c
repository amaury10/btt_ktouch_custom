#include "klipper_gcode.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/etat_klipper.h"

/* Formate `valeur` en millimètres avec au plus 2 décimales, sans zéro de fin
 * superflu ni point isolé ("10.00" -> "10", "-0.10" -> "-0.1", "1.25" ->
 * "1.25"). `snprintf("%.2f")` produit toujours une décimale (précision
 * fixe) ; on retire ensuite les '0' de fin puis, s'il ne reste que le point,
 * le point lui-même. Rend false SANS toucher `sortie` si le tampon est trop
 * court -- même politique que les fonctions publiques de ce fichier. */
static bool formater_mm(char *sortie, size_t taille, float valeur)
{
    int ecrit = snprintf(sortie, taille, "%.2f", (double)valeur);
    if (ecrit < 0 || (size_t)ecrit >= taille) {
        return false;
    }
    size_t fin = (size_t)ecrit;
    if (memchr(sortie, '.', fin) != NULL) {
        while (fin > 0 && sortie[fin - 1] == '0') {
            fin--;
        }
        if (fin > 0 && sortie[fin - 1] == '.') {
            fin--;
        }
        sortie[fin] = '\0';
    }
    return true;
}

bool klipper_gcode_jog(char *sortie, size_t taille,
                       char axe, float distance_mm, uint16_t vitesse_mm_min)
{
    if (sortie == NULL || taille == 0) {
        return false;
    }
    if (axe != 'X' && axe != 'Y' && axe != 'Z') {
        return false;
    }
    /* isfinite() avant tout usage arithmétique de distance_mm : un NaN ou un
     * infini franchirait la comparaison de borne suivante de façon
     * imprévisible (NaN) ou la validerait à tort (inf n'est PAS <= 1000
     * seulement si le sens de la comparaison est le bon -- autant l'exclure
     * explicitement en premier). */
    if (!isfinite(distance_mm) || distance_mm == 0.0f) {
        return false;
    }
    if (fabsf(distance_mm) > 1000.0f) {
        return false;
    }
    if (vitesse_mm_min < 1 || vitesse_mm_min > 60000) {
        return false;
    }

    char distance_texte[16];
    if (!formater_mm(distance_texte, sizeof(distance_texte), distance_mm)) {
        return false;
    }

    char tampon[KLIPPER_GCODE_MAX];
    int ecrit = snprintf(tampon, sizeof(tampon),
        "SAVE_GCODE_STATE NAME=ktouch_jog\nG91\nG1 %c%s F%u\nRESTORE_GCODE_STATE NAME=ktouch_jog",
        axe, distance_texte, (unsigned)vitesse_mm_min);
    if (ecrit < 0 || (size_t)ecrit >= sizeof(tampon)) {
        return false;
    }
    if ((size_t)ecrit >= taille) {
        /* Tampon appelant trop court : jamais de troncature silencieuse. */
        return false;
    }
    memcpy(sortie, tampon, (size_t)ecrit + 1);
    return true;
}

bool klipper_gcode_home(char *sortie, size_t taille, uint8_t axes_masque)
{
    if (sortie == NULL || taille == 0) {
        return false;
    }

    /* Bits hauts (hors X/Y/Z) ignorés dès l'entrée -- voir le commentaire de
     * klipper_gcode_home() dans le .h : un masque qui n'en garde aucun se
     * traite alors comme 0, donc "tout". */
    uint8_t masque = axes_masque & 0x07u;

    char tampon[KLIPPER_GCODE_MAX];
    int ecrit = snprintf(tampon, sizeof(tampon), "G28");
    if (ecrit < 0 || (size_t)ecrit >= sizeof(tampon)) {
        return false;
    }
    size_t pos = (size_t)ecrit;

    if (masque != 0 && masque != 0x07u) {
        static const struct {
            uint8_t bit;
            char lettre;
        } AXES[3] = {
            {0x01u, 'X'},
            {0x02u, 'Y'},
            {0x04u, 'Z'},
        };
        for (size_t i = 0; i < 3; i++) {
            if ((masque & AXES[i].bit) == 0) {
                continue;
            }
            ecrit = snprintf(tampon + pos, sizeof(tampon) - pos, " %c", AXES[i].lettre);
            if (ecrit < 0 || (size_t)ecrit >= sizeof(tampon) - pos) {
                return false;
            }
            pos += (size_t)ecrit;
        }
    }

    if (pos >= taille) {
        return false;
    }
    memcpy(sortie, tampon, pos + 1);
    return true;
}

bool klipper_gcode_consigne_temp(char *sortie, size_t taille,
                                 const char *chauffeur, uint16_t cible_c)
{
    if (sortie == NULL || taille == 0) {
        return false;
    }
    if (chauffeur == NULL || chauffeur[0] == '\0') {
        return false;
    }
    size_t longueur = strlen(chauffeur);
    if (longueur > 32) {
        return false;
    }
    /* Barrière anti-injection : un nom d'objet Klipper valide n'est QUE
     * alphanumérique + underscore -- tout autre octet (un '\n' suivi d'une
     * commande arbitraire comme "M112", par exemple) ne doit jamais
     * atteindre la chaîne gcode envoyée au firmware. */
    for (size_t i = 0; i < longueur; i++) {
        char c = chauffeur[i];
        bool autorise = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                         (c >= '0' && c <= '9') || c == '_';
        if (!autorise) {
            return false;
        }
    }
    if (cible_c > 350) {
        return false;
    }

    char tampon[KLIPPER_GCODE_MAX];
    int ecrit = snprintf(tampon, sizeof(tampon),
        "SET_HEATER_TEMPERATURE HEATER=%s TARGET=%u", chauffeur, (unsigned)cible_c);
    if (ecrit < 0 || (size_t)ecrit >= sizeof(tampon)) {
        return false;
    }
    if ((size_t)ecrit >= taille) {
        return false;
    }
    memcpy(sortie, tampon, (size_t)ecrit + 1);
    return true;
}

bool klipper_gcode_arret_urgence(char *sortie, size_t taille)
{
    if (sortie == NULL || taille == 0) {
        return false;
    }

    char tampon[KLIPPER_GCODE_MAX];
    int ecrit = snprintf(tampon, sizeof(tampon), "M112");
    if (ecrit < 0 || (size_t)ecrit >= sizeof(tampon)) {
        return false;
    }
    if ((size_t)ecrit >= taille) {
        /* Tampon appelant trop court : jamais de troncature silencieuse. */
        return false;
    }
    memcpy(sortie, tampon, (size_t)ecrit + 1);
    return true;
}

bool klipper_gcode_extrude(char *sortie, size_t taille,
                           float distance_mm, uint16_t vitesse_mm_min)
{
    if (sortie == NULL || taille == 0) {
        return false;
    }
    /* isfinite() avant tout usage arithmétique de distance_mm : un NaN ou un
     * infini franchirait la comparaison de borne suivante de façon
     * imprévisible (NaN) ou la validerait à tort. */
    if (!isfinite(distance_mm) || distance_mm == 0.0f) {
        return false;
    }
    if (fabsf(distance_mm) > 200.0f) {
        return false;
    }
    if (vitesse_mm_min < 1 || vitesse_mm_min > 6000) {
        return false;
    }

    char distance_texte[16];
    if (!formater_mm(distance_texte, sizeof(distance_texte), distance_mm)) {
        return false;
    }

    char tampon[KLIPPER_GCODE_MAX];
    int ecrit = snprintf(tampon, sizeof(tampon),
        "SAVE_GCODE_STATE NAME=ktouch_extrude\nM83\nG1 E%s F%u\nRESTORE_GCODE_STATE NAME=ktouch_extrude",
        distance_texte, (unsigned)vitesse_mm_min);
    if (ecrit < 0 || (size_t)ecrit >= sizeof(tampon)) {
        return false;
    }
    if ((size_t)ecrit >= taille) {
        /* Tampon appelant trop court : jamais de troncature silencieuse. */
        return false;
    }
    memcpy(sortie, tampon, (size_t)ecrit + 1);
    return true;
}

bool klipper_gcode_activer_outil(char *sortie, size_t taille, uint8_t indice)
{
    if (sortie == NULL || taille == 0) {
        return false;
    }
    if (indice >= KLIPPER_EXTRUDEURS_MAX) {
        return false;
    }

    char tampon[KLIPPER_GCODE_MAX];
    int ecrit;
    if (indice == 0) {
        ecrit = snprintf(tampon, sizeof(tampon), "ACTIVATE_EXTRUDER EXTRUDER=extruder");
    } else {
        ecrit = snprintf(tampon, sizeof(tampon), "ACTIVATE_EXTRUDER EXTRUDER=extruder%u", (unsigned)indice);
    }
    if (ecrit < 0 || (size_t)ecrit >= sizeof(tampon)) {
        return false;
    }
    if ((size_t)ecrit >= taille) {
        /* Tampon appelant trop court : jamais de troncature silencieuse. */
        return false;
    }
    memcpy(sortie, tampon, (size_t)ecrit + 1);
    return true;
}
