#include "klipper_gcode.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "etat_klipper.h" /* KLIPPER_EXTRUDEURS_MAX -- sans prefixe core/, meme
                            * convention que tout le reste du depot (ecran_*.h,
                            * habillage.c) : core/ est deja dans le chemin
                            * d'inclusion des trois cibles (le prefixe core/
                            * cassait le build simulateur, dont le path pointe
                            * directement sur firmware/main/core). */

/* Formate `valeur` avec au plus `decimales` chiffres après la virgule, sans
 * zéro de fin superflu ni point isolé ("10.00" -> "10", "-0.10" -> "-0.1",
 * "1.25" -> "1.25"). Si `forcer_signe` est vrai, un '+' explicite précède
 * toute valeur positive (syntaxe TESTZ) -- une valeur négative garde son '-'
 * dans les deux cas, `snprintf` ne le double jamais. `snprintf("%+.*f")`
 * produit toujours `decimales` chiffres (précision fixe) ; on retire ensuite
 * les '0' de fin puis, s'il ne reste que le point, le point lui-même. Rend
 * false SANS toucher `sortie` si le tampon est trop court -- même politique
 * que les fonctions publiques de ce fichier. */
static bool formater_nombre(char *sortie, size_t taille, float valeur, unsigned decimales, bool forcer_signe)
{
    const char *format = forcer_signe ? "%+.*f" : "%.*f";
    /* (int)decimales : `%.*f` lit sa précision via va_arg(int) -- passer un
     * `unsigned` serait un décalage de type variadique (UB standard, inoffensif
     * ici mais évité). */
    int ecrit = snprintf(sortie, taille, format, (int)decimales, (double)valeur);
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

/* Formate `valeur` en millimètres avec au plus 2 décimales -- alias historique
 * de formater_nombre() pour le jog/l'extrusion (distance flottante déjà en
 * mm, jamais de signe forcé). */
static bool formater_mm(char *sortie, size_t taille, float valeur)
{
    return formater_nombre(sortie, taille, valeur, 2, false);
}

/* Formate `valeur_um` (un nombre entier de micromètres) en millimètres avec
 * au plus 3 décimales -- résolution exacte pour convertir un µm entier en mm
 * (1 µm = 0.001 mm), utilisé par offset_z/testz/retraction_longueur qui
 * reçoivent tous leur borne en µm plutôt qu'en mm flottant. */
static bool formater_mm_depuis_um(char *sortie, size_t taille, int32_t valeur_um, bool forcer_signe)
{
    return formater_nombre(sortie, taille, (float)valeur_um / 1000.0f, 3, forcer_signe);
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

bool klipper_gcode_ventilateur(char *sortie, size_t taille, uint8_t pct)
{
    if (sortie == NULL || taille == 0) {
        return false;
    }
    if (pct > 100) {
        return false;
    }

    /* Calcul de la valeur PWM (0-255) à partir du pourcentage (0-100).
     * Formule : (pct * 255 + 50) / 100 pour arrondir au plus proche.
     * Utilise (int) pour éviter tout dépassement uint8 dans le calcul. */
    uint8_t valeur = (uint8_t)(((int)pct * 255 + 50) / 100);

    char tampon[KLIPPER_GCODE_MAX];
    int ecrit = snprintf(tampon, sizeof(tampon), "M106 S%u", (unsigned)valeur);
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

bool klipper_gcode_imprimer_fichier(char *sortie, size_t taille, const char *nom)
{
    if (sortie == NULL || taille == 0) {
        return false;
    }
    if (nom == NULL || nom[0] == '\0') {
        return false;
    }

    char tampon[KLIPPER_GCODE_MAX];
    int ecrit = snprintf(tampon, sizeof(tampon), "SDCARD_PRINT_FILE FILENAME=%s", nom);
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

bool klipper_gcode_vitesse_impression(char *sortie, size_t taille, uint16_t pct)
{
    if (sortie == NULL || taille == 0) {
        return false;
    }
    if (pct < 1 || pct > 300) {
        return false;
    }

    char tampon[KLIPPER_GCODE_MAX];
    int ecrit = snprintf(tampon, sizeof(tampon), "M220 S%u", (unsigned)pct);
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

bool klipper_gcode_flux(char *sortie, size_t taille, uint16_t pct)
{
    if (sortie == NULL || taille == 0) {
        return false;
    }
    if (pct < 1 || pct > 300) {
        return false;
    }

    char tampon[KLIPPER_GCODE_MAX];
    int ecrit = snprintf(tampon, sizeof(tampon), "M221 S%u", (unsigned)pct);
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

bool klipper_gcode_offset_z(char *sortie, size_t taille, int32_t delta_um, bool reset)
{
    if (sortie == NULL || taille == 0) {
        return false;
    }

    char tampon[KLIPPER_GCODE_MAX];
    int ecrit;
    if (reset) {
        /* delta_um ignoré -- le reset ramène toujours l'offset à zéro,
         * quelle que soit la valeur passée par l'appelant. */
        ecrit = snprintf(tampon, sizeof(tampon), "SET_GCODE_OFFSET Z=0 MOVE=1");
    } else {
        if (delta_um == 0) {
            /* Un delta nul n'est un script valide QUE via reset=true : ici,
             * ce serait un SET_GCODE_OFFSET Z_ADJUST=0 qui ne fait rien --
             * signe probable d'un appelant qui a oublié de vérifier avant
             * d'appeler. */
            return false;
        }
        if (delta_um < -2000 || delta_um > 2000) {
            return false;
        }
        char valeur_texte[16];
        if (!formater_mm_depuis_um(valeur_texte, sizeof(valeur_texte), delta_um, false)) {
            return false;
        }
        ecrit = snprintf(tampon, sizeof(tampon), "SET_GCODE_OFFSET Z_ADJUST=%s MOVE=1", valeur_texte);
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

bool klipper_gcode_calibration_z(char *sortie, size_t taille, klipper_zcal_action_t action)
{
    if (sortie == NULL || taille == 0) {
        return false;
    }

    const char *commande;
    switch (action) {
        case KLIPPER_ZCAL_PROBE:   commande = "PROBE_CALIBRATE"; break;
        case KLIPPER_ZCAL_ENDSTOP: commande = "Z_ENDSTOP_CALIBRATE"; break;
        case KLIPPER_ZCAL_ACCEPT:  commande = "ACCEPT"; break;
        case KLIPPER_ZCAL_ABORT:   commande = "ABORT"; break;
        default: return false;
    }

    char tampon[KLIPPER_GCODE_MAX];
    int ecrit = snprintf(tampon, sizeof(tampon), "%s", commande);
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

bool klipper_gcode_testz(char *sortie, size_t taille, int32_t delta_um)
{
    if (sortie == NULL || taille == 0) {
        return false;
    }
    if (delta_um == 0) {
        return false;
    }
    if (delta_um < -5000 || delta_um > 5000) {
        return false;
    }

    char valeur_texte[16];
    if (!formater_mm_depuis_um(valeur_texte, sizeof(valeur_texte), delta_um, true)) {
        return false;
    }

    char tampon[KLIPPER_GCODE_MAX];
    int ecrit = snprintf(tampon, sizeof(tampon), "TESTZ Z=%s", valeur_texte);
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

bool klipper_gcode_niveau_lit(char *sortie, size_t taille, klipper_lit_action_t action)
{
    if (sortie == NULL || taille == 0) {
        return false;
    }

    const char *commande;
    switch (action) {
        case KLIPPER_LIT_SCREWS:  commande = "SCREWS_TILT_CALCULATE"; break;
        case KLIPPER_LIT_ZTILT:   commande = "Z_TILT_ADJUST"; break;
        case KLIPPER_LIT_QGL:     commande = "QUAD_GANTRY_LEVEL"; break;
        case KLIPPER_LIT_DISABLE: commande = "M84"; break;
        default: return false;
    }

    char tampon[KLIPPER_GCODE_MAX];
    int ecrit = snprintf(tampon, sizeof(tampon), "%s", commande);
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

bool klipper_gcode_limite_vitesse(char *sortie, size_t taille, klipper_lim_champ_t champ, uint32_t valeur)
{
    if (sortie == NULL || taille == 0) {
        return false;
    }

    const char *nom_champ;
    switch (champ) {
        case KLIPPER_LIM_VELOCITY:       nom_champ = "VELOCITY"; break;
        case KLIPPER_LIM_ACCEL:          nom_champ = "ACCEL"; break;
        case KLIPPER_LIM_SQV:            nom_champ = "SQUARE_CORNER_VELOCITY"; break;
        case KLIPPER_LIM_ACCEL_TO_DECEL: nom_champ = "ACCEL_TO_DECEL"; break;
        default: return false;
    }
    if (valeur < 1 || valeur > 100000) {
        return false;
    }

    char tampon[KLIPPER_GCODE_MAX];
    int ecrit = snprintf(tampon, sizeof(tampon), "SET_VELOCITY_LIMIT %s=%u", nom_champ, (unsigned)valeur);
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

bool klipper_gcode_retraction_longueur(char *sortie, size_t taille, klipper_retr_champ_t champ, uint32_t valeur_um)
{
    if (sortie == NULL || taille == 0) {
        return false;
    }

    /* Seuls les deux champs de longueur de l'enum sont reconnus ici -- les
     * deux champs de vitesse (SPEED/UNRETRACT_SPEED) rendent false : voir
     * klipper_gcode_retraction_vitesse() pour ceux-là. */
    const char *nom_champ;
    switch (champ) {
        case KLIPPER_RETR_LENGTH: nom_champ = "RETRACT_LENGTH"; break;
        case KLIPPER_RETR_EXTRA:  nom_champ = "UNRETRACT_EXTRA_LENGTH"; break;
        default: return false;
    }
    if (valeur_um > 20000) {
        return false;
    }

    char valeur_texte[16];
    if (!formater_mm_depuis_um(valeur_texte, sizeof(valeur_texte), (int32_t)valeur_um, false)) {
        return false;
    }

    char tampon[KLIPPER_GCODE_MAX];
    int ecrit = snprintf(tampon, sizeof(tampon), "SET_RETRACTION %s=%s", nom_champ, valeur_texte);
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

bool klipper_gcode_retraction_vitesse(char *sortie, size_t taille, klipper_retr_champ_t champ, uint32_t mm_s)
{
    if (sortie == NULL || taille == 0) {
        return false;
    }

    /* Seuls les deux champs de vitesse de l'enum sont reconnus ici -- les
     * deux champs de longueur (LENGTH/EXTRA) rendent false : voir
     * klipper_gcode_retraction_longueur() pour ceux-là. */
    const char *nom_champ;
    switch (champ) {
        case KLIPPER_RETR_SPEED:           nom_champ = "RETRACT_SPEED"; break;
        case KLIPPER_RETR_UNRETRACT_SPEED: nom_champ = "UNRETRACT_SPEED"; break;
        default: return false;
    }
    if (mm_s < 1 || mm_s > 1000) {
        return false;
    }

    char tampon[KLIPPER_GCODE_MAX];
    int ecrit = snprintf(tampon, sizeof(tampon), "SET_RETRACTION %s=%u", nom_champ, (unsigned)mm_s);
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
