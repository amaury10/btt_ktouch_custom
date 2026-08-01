/* Implémentation : voir ecran_temperatures.h pour le contrat et d'où vient
 * cette logique (section températures de l'ancien ecran_accueil_idle.c, git
 * ce47e3a, supprimé tâche 7 de la refonte accueil/déplacer).
 *
 * Mise en page (742x436, dans le conteneur de navigation à droite du rail
 * persistant, sous la barre d'état construite par habillage.c) : la zone de
 * température en haut (extrudeurs présents puis plateau, géométrie par
 * palier -- klipper_paliers.h, IDENTIQUE à ecran_accueil_hub.c, copiée
 * depuis ce fichier), puis la rangée de 4 préréglages juste en dessous. Pas
 * de ligne de position/outil actif, pas de zone de contrôles (jog/homing) :
 * cet écran n'a que les tuiles réglables et les préréglages (voir le
 * commentaire de tête du .h pour ce qui a été délibérément laissé de côté).
 *
 * ÉCART par rapport à ecran_accueil_hub.c : les tuiles sont ICI cliquables
 * (LV_OBJ_FLAG_CLICKABLE, cellule_bouton_cb() ci-dessous ouvre le clavier
 * numérique) -- c'est précisément la fonction que le hub avait délibérément
 * omise (voir son commentaire "ÉCART délibéré") et que cet écran dédié
 * rétablit. */
#include "ecran_temperatures.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "backend.h"
#include "clavier.h"
#include "habillage.h" /* habillage_notifier() */
#include "klipper_gcode.h"
#include "klipper_paliers.h"
#include "source_etat.h"
#include "tuile.h" /* ui_format_temperature() */

#define LARGEUR_CONTENU 742 /* 800 - RAIL_LARGEUR (58), voir habillage.c */
#define HAUTEUR_CONTENU 436 /* 480 - BARRE_HAUTEUR (44), voir habillage.c */

#define MARGE        14
#define GRILLE_ECART  6

#define ZONE_ECART 14 /* écart vertical entre la zone de température et la rangée de préréglages */

#define TEMP_ZONE_Y 6

/* --- Géométrie de la zone de température : copie EXACTE des constantes de
 * ecran_accueil_hub.c (elles-mêmes copiées de l'ancien ecran_accueil_idle.c,
 * supprimé tâche 7), voir le commentaire de tête de ce fichier et celui de
 * ecran_temperatures.h ("Réutilisation") pour pourquoi une copie plutôt
 * qu'un partage. ------------------------------------------------------- */
#define CELL_LARGEUR_1COL 714
#define CELL_LARGEUR_2COL 354

#define CELL_HAUTEUR_MONO    100
#define CELL_HAUTEUR_MOYEN    70
#define CELL_HAUTEUR_COMPACT  44

#define LIGNES_PIRE_CAS_MONO    2 /* 1 extrudeur + 1 plateau, colonnes=1 */
#define LIGNES_PIRE_CAS_MOYEN   3 /* 4 extrudeurs + 1 plateau, colonnes=2 */
#define LIGNES_PIRE_CAS_COMPACT 5 /* 8 extrudeurs + 1 plateau, colonnes=2 */

#define ZONE_TEMP_HAUTEUR_MAX 244

/* Rangée de préréglages, juste sous la zone de température -- 44 px de
 * hauteur (contre 24 dans l'ancien ecran_accueil_idle.c) : cet écran n'a NI
 * jog NI homing à se partager la hauteur restante, rien n'empêche de viser
 * directement la cible tactile minimale (44px) plutôt que le compromis
 * serré que l'ancien idle devait faire (voir son commentaire "ÉCART tâche
 * 6"). */
#define PRESETS_Y (TEMP_ZONE_Y + ZONE_TEMP_HAUTEUR_MAX + ZONE_ECART)
#define PRESETS_HAUTEUR    44
#define PRESETS_ECART_BOUTON 8 /* meme rythme que ECART_BOUTONS dans selecteur_pas.c */

/* Même convention que BARRE_HAUTEUR_ECRAN/BANDEAU_HAUTEUR_ECRAN/
 * BANDEAU_Y_ECRAN dans ecran_accueil_hub.c (voir son commentaire complet) :
 * bande couverte par le bandeau de notification de habillage.c, en
 * coordonnées ABSOLUES d'écran. */
#define BARRE_HAUTEUR_ECRAN   44
#define HAUTEUR_ECRAN_TOTALE 480
#define BANDEAU_HAUTEUR_ECRAN 60
#define BANDEAU_Y_ECRAN (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)

_Static_assert(2 * MARGE + CELL_LARGEUR_1COL == LARGEUR_CONTENU,
                "la cellule pleine largeur (palier MONO) ne remplit plus exactement 742px");
_Static_assert(2 * MARGE + 2 * CELL_LARGEUR_2COL + GRILLE_ECART == LARGEUR_CONTENU,
                "les deux colonnes (paliers MOYEN/COMPACT) ne remplissent plus exactement 742px");

_Static_assert(LIGNES_PIRE_CAS_MONO * CELL_HAUTEUR_MONO + (LIGNES_PIRE_CAS_MONO - 1) * GRILLE_ECART
                   <= ZONE_TEMP_HAUTEUR_MAX,
                "le palier MONO deborde de la zone de temperature reservee");
_Static_assert(LIGNES_PIRE_CAS_MOYEN * CELL_HAUTEUR_MOYEN + (LIGNES_PIRE_CAS_MOYEN - 1) * GRILLE_ECART
                   <= ZONE_TEMP_HAUTEUR_MAX,
                "le palier MOYEN deborde de la zone de temperature reservee");
_Static_assert(LIGNES_PIRE_CAS_COMPACT * CELL_HAUTEUR_COMPACT + (LIGNES_PIRE_CAS_COMPACT - 1) * GRILLE_ECART
                   == ZONE_TEMP_HAUTEUR_MAX,
                "le palier COMPACT (le plus haut des trois) ne definit plus ZONE_TEMP_HAUTEUR_MAX");

_Static_assert(PRESETS_Y + PRESETS_HAUTEUR <= HAUTEUR_CONTENU,
                "la rangee de preregalges deborde de la hauteur du contenu");
/* Même garde-fou que MENU_ZONE_Y/MENU_ZONE_HAUTEUR dans ecran_accueil_hub.c
 * ("PLAFOND REEL de CONTROLES_HAUTEUR" dans l'ancien idle) : le bas de la
 * rangée de préréglages, en coordonnées ABSOLUES d'écran, doit rester
 * au-dessus du bandeau de notification -- sans quoi une notification
 * recouvrirait ET bloquerait le tap sur les préréglages. */
_Static_assert(BARRE_HAUTEUR_ECRAN + PRESETS_Y + PRESETS_HAUTEUR <= BANDEAU_Y_ECRAN,
                "la rangee de preregalges chevauche la bande du bandeau de notification de l'habillage");

#define COULEUR_FOND             0x10161D
#define COULEUR_FOND_CELLULE     0x1B2430
#define COULEUR_TEXTE_PRINCIPAL  0xFFFFFF
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280 /* meme gris de peremption que le reste de ui/ */
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF
/* Bleu dedie a « outil actif », copie de l'ancien ecran_accueil_idle.c --
 * jamais le vert de habillage_couleur_liaison(). */
#define COULEUR_ACTIF 0x3B82F6

/* Préréglages de température (brief : "PLA 210/60, PETG 240/80, ABS
 * 250/100, Off"). Off n'a pas besoin de sa propre paire de constantes
 * nommées "PREREGLAGE_OFF_*" (0 EST la valeur, pas un choix arbitraire à
 * documenter) -- klipper_gcode_consigne_temp() accepte déjà 0 comme cible
 * valide ("0 = eteindre", voir klipper_gcode.h). */
#define PREREGLAGE_PLA_BUSE      210u
#define PREREGLAGE_PLA_PLATEAU    60u
#define PREREGLAGE_PETG_BUSE     240u
#define PREREGLAGE_PETG_PLATEAU   80u
#define PREREGLAGE_ABS_BUSE      250u
#define PREREGLAGE_ABS_PLATEAU  100u

/* Tampon suffisant pour {"script":"<gcode>"} : KLIPPER_GCODE_MAX (160,
 * klipper_gcode.h) plus la marge du wrapper JSON (guillemets, cle "script",
 * accolades) -- meme raisonnement que ECRAN_ACCUEIL_IDLE_GCODE_ARGS_MAX
 * dans l'ancien ecran_accueil_idle.c (supprime tache 7). */
#define GCODE_ARGS_MAX (KLIPPER_GCODE_MAX + 32)

/* Géométrie dérivée du palier courant : `colonnes` et `police_valeur`
 * viennent de klipper_paliers.h (jamais recopiés en dur, voir son en-tête) ;
 * `largeur`/`hauteur`/`afficher_consigne` sont propres à cet écran. Copie de
 * hub_geometrie_t/idle_geometrie_t (voir le commentaire de tête). */
typedef struct {
    uint8_t          colonnes;
    lv_coord_t       largeur;
    lv_coord_t       hauteur;
    const lv_font_t *police_valeur;
    bool             afficher_consigne;
} temp_geometrie_t;

static const lv_font_t *police_pour_taille(uint8_t taille)
{
    switch (taille) {
    case 48:
        return &lv_font_montserrat_48;
    case 28:
        return &lv_font_montserrat_28;
    case 20:
    default:
        return &lv_font_montserrat_20;
    }
}

static temp_geometrie_t geometrie_pour_palier(palier_outils_t palier)
{
    temp_geometrie_t g;
    g.colonnes = palier_colonnes(palier);
    g.police_valeur = police_pour_taille(palier_taille_police(palier));
    switch (palier) {
    case PALIER_MONO:
        g.largeur = CELL_LARGEUR_1COL;
        g.hauteur = CELL_HAUTEUR_MONO;
        g.afficher_consigne = true;
        break;
    case PALIER_MOYEN:
        g.largeur = CELL_LARGEUR_2COL;
        g.hauteur = CELL_HAUTEUR_MOYEN;
        g.afficher_consigne = true;
        break;
    case PALIER_COMPACT:
    default:
        /* Au palier COMPACT, pas de consigne inline (spec, identique à
         * l'ancien idle) -- la cellule n'a plus la place d'un réglage
         * direct ; le tap ouvre quand même le clavier numérique
         * (cellule_bouton_cb() plus bas), qui EST la "vue détaillée" de ce
         * palier. */
        g.largeur = CELL_LARGEUR_2COL;
        g.hauteur = CELL_HAUTEUR_COMPACT;
        g.afficher_consigne = false;
        break;
    }
    return g;
}

/* Crée les quatre objets LVGL d'une cellule et les range dans `c` -- copie
 * de cellule_creer() de ecran_accueil_hub.c/l'ancien ecran_accueil_idle.c,
 * SAUF que LV_OBJ_FLAG_CLICKABLE est posé ICI (contrairement au hub) : cette
 * cellule ouvre le clavier numérique au tap, voir cellule_bouton_cb() plus
 * bas et le commentaire de tête de ce fichier. */
static void cellule_creer(ecran_temperatures_cellule_t *c, lv_obj_t *parent)
{
    c->racine = lv_obj_create(parent);
    lv_obj_remove_style_all(c->racine);
    lv_obj_set_style_bg_color(c->racine, lv_color_hex(COULEUR_FOND_CELLULE), 0);
    lv_obj_set_style_bg_opa(c->racine, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c->racine, 10, 0);
    lv_obj_set_style_border_color(c->racine, lv_color_hex(COULEUR_ACTIF), 0);
    lv_obj_clear_flag(c->racine, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c->racine, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(c->racine, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c->racine, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(c->racine, 2, 0);

    c->nom = lv_label_create(c->racine);
    lv_obj_set_style_text_font(c->nom, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(c->nom, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(c->nom, "");

    c->valeur = lv_label_create(c->racine);
    lv_obj_set_style_text_color(c->valeur, lv_color_hex(COULEUR_TEXTE_PRINCIPAL), 0);
    lv_label_set_text(c->valeur, "");

    c->consigne = lv_label_create(c->racine);
    lv_obj_set_style_text_font(c->consigne, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(c->consigne, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(c->consigne, "");
}

/* Construit {"script":"<script>"} via cJSON (jamais un snprintf a la main) :
 * meme choix, meme raison que construire_arguments_gcode() de
 * ecran_deplacer.c/l'ancien ecran_accueil_idle.c -- un vrai octet 0x0A brut
 * dans une chaine JSON violerait RFC 8259. Rend false SANS TOUCHER `sortie`
 * si un argument est invalide, si cJSON echoue (memoire), ou si le resultat
 * ne tient pas dans `taille`. */
static bool construire_arguments_gcode(const char *script, char *sortie, size_t taille)
{
    if (script == NULL || sortie == NULL || taille == 0) {
        return false;
    }
    cJSON *racine = cJSON_CreateObject();
    if (racine == NULL) {
        return false;
    }
    if (cJSON_AddStringToObject(racine, "script", script) == NULL) {
        cJSON_Delete(racine);
        return false;
    }
    char *texte = cJSON_PrintUnformatted(racine);
    cJSON_Delete(racine);
    if (texte == NULL) {
        return false;
    }
    size_t longueur = strlen(texte);
    bool ok = longueur < taille;
    if (ok) {
        memcpy(sortie, texte, longueur + 1);
    }
    cJSON_free(texte);
    return ok;
}

static void envoyer_gcode(const char *script)
{
    if (script == NULL) {
        return;
    }
    char arguments[GCODE_ARGS_MAX];
    if (!construire_arguments_gcode(script, arguments, sizeof(arguments))) {
        return; /* ne devrait jamais arriver : GCODE_ARGS_MAX suffit toujours */
    }
    ui_commander(BACKEND_ACTION_GCODE, arguments);
}

/* ------------------------------------------------------------------------
 * Consignes de temperature manuelles (clavier numerique) + prereglages --
 * copie de la section equivalente de l'ancien ecran_accueil_idle.c (git
 * ce47e3a, supprime tache 7), voir le commentaire de tete du .h.
 * ------------------------------------------------------------------------ */

const char ECRAN_TEMPERATURES_TITRE_BUSE[]    = "Nozzle target";
const char ECRAN_TEMPERATURES_TITRE_PLATEAU[] = "Bed target";

/* Nom du chauffeur Klipper d'un extrudeur : indice 0 => "extruder", indice N
 * (1..7) => "extruderN" -- PAS "extruder0" pour l'indice 0. Rend faux SANS
 * toucher `sortie` si le tampon est trop court ou si `indice` deborde
 * KLIPPER_EXTRUDEURS_MAX (defensif, comme le reste de ce fichier vis-a-vis
 * d'un etat malforme). */
static bool nom_chauffeur_extrudeur(uint8_t indice, char *sortie, size_t taille)
{
    if (sortie == NULL || taille == 0 || indice >= KLIPPER_EXTRUDEURS_MAX) {
        return false;
    }
    int ecrit = (indice == 0) ? snprintf(sortie, taille, "extruder")
                               : snprintf(sortie, taille, "extruder%u", (unsigned)indice);
    return ecrit >= 0 && (size_t)ecrit < taille;
}

/* Convertit une consigne backend (float, potentiellement NaN/negative/hors
 * plage -- voir etat_klipper_t) en entier [0, 350] affichable/envoyable :
 * meme borne que klipper_gcode_consigne_temp(). isnan() teste
 * EXPLICITEMENT en premier -- comparer un NaN a 0.0f rend faux dans les DEUX
 * sens, ce qui laisserait tomber dans la conversion (uint16_t)v finale avec
 * un NaN : comportement indefini en C (6.3.1.4), jamais accepte
 * silencieusement ici. */
static uint16_t consigne_u16(float valeur)
{
    if (isnan(valeur) || valeur <= 0.0f) {
        return 0;
    }
    if (valeur > (float)ECRAN_TEMPERATURES_TEMP_MAX) {
        return ECRAN_TEMPERATURES_TEMP_MAX;
    }
    return (uint16_t)valeur;
}

/* Rend `info->ctx` -> nom du chauffeur Klipper de CETTE cellule (extrudeur
 * ou "heater_bed"), dans `sortie`. Factorise entre cellule_bouton_cb() (pour
 * le titre "Nozzle"/"Bed") et cellule_clavier_rappel() (pour le gcode
 * reellement envoye) -- les deux ne doivent jamais diverger sur QUEL
 * chauffeur une cellule precise designe. */
static bool cellule_info_nom_chauffeur(const ecran_temperatures_cellule_info_t *info, char *sortie, size_t taille)
{
    if (info->est_plateau) {
        int ecrit = snprintf(sortie, taille, "heater_bed");
        return ecrit >= 0 && (size_t)ecrit < taille;
    }
    return nom_chauffeur_extrudeur(info->indice_extrudeur, sortie, taille);
}

/* Rappel du clavier numerique ouvert par cellule_bouton_cb() ci-dessous --
 * `contexte` EST directement `&ctx->cellule_infos[i]`. `valeur == NULL`
 * (annule) : rien (spec). Sinon parse en entier, borne
 * [ECRAN_TEMPERATURES_TEMP_MIN, ECRAN_TEMPERATURES_TEMP_MAX] -- une saisie
 * hors borne OU non numerique => notification d'erreur, AUCUN gcode envoye
 * (spec : "jamais de consigne aberrante envoyee a la machine"). */
static void cellule_clavier_rappel(const char *valeur, void *contexte)
{
    ecran_temperatures_cellule_info_t *info = contexte;
    if (valeur == NULL || info == NULL) {
        return;
    }

    /* strtol() plutot que sscanf("%d") : rend un pointeur de fin qu'on peut
     * comparer au terme de la chaine, la seule facon fiable de distinguer
     * "210" (valide) de "210abc"/"12.5"/""/"  " (non numerique). Chaine vide
     * (clavier valide sans rien saisir) : `fin == valeur`, rejetee
     * explicitement ci-dessous. */
    char *fin = NULL;
    errno = 0;
    long cible = strtol(valeur, &fin, 10);
    bool numerique = (fin != valeur) && (fin != NULL) && (*fin == '\0') && (errno == 0);
    bool dans_bornes = numerique && cible >= ECRAN_TEMPERATURES_TEMP_MIN && cible <= ECRAN_TEMPERATURES_TEMP_MAX;

    if (!dans_bornes) {
        habillage_notifier("Invalid temperature (0-350)", true);
        return;
    }

    char chauffeur[40];
    if (!cellule_info_nom_chauffeur(info, chauffeur, sizeof(chauffeur))) {
        return; /* ne devrait jamais arriver : indice/tampon toujours valides depuis ce clavier */
    }
    char script[KLIPPER_GCODE_MAX];
    if (!klipper_gcode_consigne_temp(script, sizeof(script), chauffeur, (uint16_t)cible)) {
        return; /* ne devrait jamais arriver : chauffeur/cible deja valides ci-dessus */
    }
    envoyer_gcode(script);
}

/* Tap sur une cellule de temperature. `info` est passe DIRECTEMENT comme
 * `contexte` a clavier_ouvrir() : aucun etat "en attente" a poser avant
 * l'ouverture, donc aucune garde de reentrance requise ici --
 * clavier_ouvrir() est deja lui-meme un singleton qui refuse/journalise une
 * seconde ouverture (voir clavier.h) sans que ce rappel ait rien ecrit au
 * prealable qu'un second tap pourrait corrompre. */
static void cellule_bouton_cb(lv_event_t *e)
{
    ecran_temperatures_cellule_info_t *info = lv_event_get_user_data(e);
    lv_obj_t *cible = lv_event_get_target(e);
    if (info == NULL || info->ctx == NULL || cible == NULL) {
        return;
    }

    const char *titre = info->est_plateau ? ECRAN_TEMPERATURES_TITRE_PLATEAU : ECRAN_TEMPERATURES_TITRE_BUSE;
    char valeur_initiale[8];
    snprintf(valeur_initiale, sizeof(valeur_initiale), "%u", (unsigned)info->consigne_courante);
    clavier_ouvrir(titre, valeur_initiale, CLAVIER_NUMERIQUE, cellule_clavier_rappel, info);
}

/* Prereglage : DEUX gcodes, toujours -- la buse ACTIVE (ctx->outil_actif_connu,
 * relu AU MOMENT DU CLIC) puis le plateau, decision figee ("chaque preset
 * envoie DEUX gcodes ... jamais un seul combine"). "Off" (cibles 0/0) n'est
 * pas un cas special : c'est le MEME chemin, juste avec des cibles nulles --
 * klipper_gcode_consigne_temp() accepte 0 comme cible valide ("eteindre"). */
static void preset_bouton_cb(lv_event_t *e)
{
    ecran_temperatures_preset_info_t *info = lv_event_get_user_data(e);
    lv_obj_t *cible = lv_event_get_target(e);
    if (info == NULL || info->ctx == NULL || cible == NULL) {
        return;
    }

    char chauffeur_buse[16];
    if (nom_chauffeur_extrudeur(info->ctx->outil_actif_connu, chauffeur_buse, sizeof(chauffeur_buse))) {
        char script_buse[KLIPPER_GCODE_MAX];
        if (klipper_gcode_consigne_temp(script_buse, sizeof(script_buse), chauffeur_buse, info->cible_buse)) {
            envoyer_gcode(script_buse);
        }
    }

    char script_plateau[KLIPPER_GCODE_MAX];
    if (klipper_gcode_consigne_temp(script_plateau, sizeof(script_plateau), "heater_bed", info->cible_plateau)) {
        envoyer_gcode(script_plateau);
    }
}

static void ecran_temperatures_construire(lv_obj_t *parent, void *contexte)
{
    ecran_temperatures_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* --- tuiles de temperature : pool au pire cas, masquees tant que
     * mettre_a_jour() n'a pas tourne. --------------------------------- */
    for (size_t i = 0; i < ECRAN_TEMPERATURES_CELLULES_MAX; i++) {
        cellule_creer(&ctx->cellules[i], parent);
        lv_obj_add_flag(ctx->cellules[i].racine, LV_OBJ_FLAG_HIDDEN);
        /* ctx pose ICI, une fois pour toutes -- meme idiome que
         * jog_infos[i].ctx dans l'ancien ecran_accueil_idle.c ("jamais NULL
         * une fois construire() passe"). est_plateau/indice_extrudeur/
         * consigne_courante restent a leur valeur zero-initialisee (calloc
         * du contexte, voir ecran.h) tant que mettre_a_jour() n'a pas tourne
         * au moins une fois pour CETTE cellule -- inoffensif, la cellule est
         * masquee jusque-la. */
        ctx->cellule_infos[i].ctx = ctx;
        lv_obj_add_event_cb(ctx->cellules[i].racine, cellule_bouton_cb, LV_EVENT_CLICKED, &ctx->cellule_infos[i]);
    }

    /* --- rangee de prereglages, quatre boutons a largeur egale -- meme
     * idiome flex que selecteur_pas.c (racine FLEX_ROW, chaque bouton
     * flex_grow(1)). Voir ECRAN_TEMPERATURES_PRESET_* (le .h) pour
     * l'indexation et PREREGLAGE_* (tete de ce fichier) pour les cibles. */
    ctx->zone_preregalges = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->zone_preregalges);
    lv_obj_clear_flag(ctx->zone_preregalges, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(ctx->zone_preregalges, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ctx->zone_preregalges, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(ctx->zone_preregalges, PRESETS_ECART_BOUTON, 0);
    lv_obj_set_size(ctx->zone_preregalges, LARGEUR_CONTENU - 2 * MARGE, PRESETS_HAUTEUR);
    lv_obj_set_pos(ctx->zone_preregalges, MARGE, PRESETS_Y);

    static const struct {
        const char *libelle;
        uint16_t    cible_buse;
        uint16_t    cible_plateau;
    } PRESET_DEFS[ECRAN_TEMPERATURES_PRESET_NB] = {
        [ECRAN_TEMPERATURES_PRESET_PLA]  = { "PLA 210/60",  PREREGLAGE_PLA_BUSE,  PREREGLAGE_PLA_PLATEAU },
        [ECRAN_TEMPERATURES_PRESET_PETG] = { "PETG 240/80", PREREGLAGE_PETG_BUSE, PREREGLAGE_PETG_PLATEAU },
        [ECRAN_TEMPERATURES_PRESET_ABS]  = { "ABS 250/100", PREREGLAGE_ABS_BUSE,  PREREGLAGE_ABS_PLATEAU },
        [ECRAN_TEMPERATURES_PRESET_OFF]  = { "Off",         0,                    0 },
    };
    for (uint8_t i = 0; i < ECRAN_TEMPERATURES_PRESET_NB; i++) {
        lv_obj_t *bouton = lv_button_create(ctx->zone_preregalges);
        /* lv_obj_remove_style_all() : meme raison que dans
         * ecran_accueil_hub.c/l'ancien ecran_accueil_idle.c -- theme par
         * defaut + transition animee otes, pour ne pas alourdir
         * style_trans_ll cote host-test. */
        lv_obj_remove_style_all(bouton);
        lv_obj_set_height(bouton, LV_PCT(100));
        lv_obj_set_flex_grow(bouton, 1);
        lv_obj_set_style_bg_opa(bouton, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bouton, lv_color_hex(COULEUR_BOUTON), 0);
        lv_obj_set_style_border_width(bouton, 0, 0);
        lv_obj_set_style_shadow_width(bouton, 0, 0);
        lv_obj_set_style_radius(bouton, 6, 0);

        lv_obj_t *label = lv_label_create(bouton);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
        lv_label_set_text(label, PRESET_DEFS[i].libelle);
        lv_obj_center(label);

        ctx->preset_boutons[i] = bouton;
        ctx->preset_infos[i].ctx = ctx;
        ctx->preset_infos[i].cible_buse = PRESET_DEFS[i].cible_buse;
        ctx->preset_infos[i].cible_plateau = PRESET_DEFS[i].cible_plateau;
        lv_obj_add_event_cb(bouton, preset_bouton_cb, LV_EVENT_CLICKED, &ctx->preset_infos[i]);
    }
}

static void ecran_temperatures_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    ecran_temperatures_ctx_t *ctx = contexte;
    const etat_klipper_t *e = etat;
    if (ctx == NULL || e == NULL) {
        return;
    }

    /* Defense contre un etat corrompu/malforme : nb_extrudeurs promet 0..8
     * (voir etat_klipper.h) mais rien de ce cote-ci ne DOIT lui faire
     * confiance aveuglement -- un nombre plus grand ecrirait hors du pool de
     * cellules (ECRAN_TEMPERATURES_CELLULES_MAX, dimensionne exactement sur
     * KLIPPER_EXTRUDEURS_MAX + 1). */
    uint8_t nb_extrudeurs = e->nb_extrudeurs;
    if (nb_extrudeurs > KLIPPER_EXTRUDEURS_MAX) {
        nb_extrudeurs = KLIPPER_EXTRUDEURS_MAX;
    }

    palier_outils_t palier = palier_outils(nb_extrudeurs);
    temp_geometrie_t geo = geometrie_pour_palier(palier);

    /* --- Contenu : extrudeurs presents puis plateau, dans cet ordre ----- */
    size_t total = 0;
    char valeur[16];
    char consigne[16];
    char nom[8];

    for (uint8_t i = 0; i < nb_extrudeurs; i++) {
        if (!e->extrudeurs[i].presente) {
            /* nb_extrudeurs promet que 0..nb_extrudeurs-1 sont presents
             * (voir etat_klipper.h) ; garde defensive quand meme, meme
             * politique que le reste de ce fichier vis-a-vis d'un etat
             * malforme. */
            continue;
        }
        ecran_temperatures_cellule_t *c = &ctx->cellules[total];
        snprintf(nom, sizeof(nom), "T%u", (unsigned)i);
        lv_label_set_text(c->nom, nom);
        ui_format_temperature(valeur, sizeof(valeur), e->extrudeurs[i].actuelle);
        lv_label_set_text(c->valeur, valeur);
        if (geo.afficher_consigne) {
            ui_format_temperature(consigne, sizeof(consigne), e->extrudeurs[i].consigne);
            lv_label_set_text(c->consigne, consigne);
            lv_obj_clear_flag(c->consigne, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(c->consigne, LV_OBJ_FLAG_HIDDEN);
        }
        /* Outil actif marque par une bordure distincte -- reevalue a CHAQUE
         * appel (systematique, jamais incremental) : un outil qui change de
         * "actif" a "inactif" (changeur d'outils) doit perdre sa bordure au
         * prochain appel. */
        lv_obj_set_style_border_width(c->racine, (i == e->outil_actif) ? 3 : 0, 0);
        /* Identite du chauffeur + consigne courante, relues par
         * cellule_bouton_cb()/le clavier au moment du tap -- voir le
         * commentaire de ecran_temperatures_cellule_info_t (le .h). */
        ctx->cellule_infos[total].est_plateau = false;
        ctx->cellule_infos[total].indice_extrudeur = i;
        ctx->cellule_infos[total].consigne_courante = consigne_u16(e->extrudeurs[i].consigne);
        total++;
    }

    if (e->plateau.presente) {
        ecran_temperatures_cellule_t *c = &ctx->cellules[total];
        lv_label_set_text(c->nom, "Bed");
        ui_format_temperature(valeur, sizeof(valeur), e->plateau.actuelle);
        lv_label_set_text(c->valeur, valeur);
        if (geo.afficher_consigne) {
            ui_format_temperature(consigne, sizeof(consigne), e->plateau.consigne);
            lv_label_set_text(c->consigne, consigne);
            lv_obj_clear_flag(c->consigne, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(c->consigne, LV_OBJ_FLAG_HIDDEN);
        }
        /* Le plateau n'est jamais "l'outil actif" (outil_actif indexe
         * extrudeurs[], jamais le plateau, voir etat_klipper_t). */
        lv_obj_set_style_border_width(c->racine, 0, 0);
        ctx->cellule_infos[total].est_plateau = true;
        ctx->cellule_infos[total].indice_extrudeur = 0; /* non utilise, est_plateau=true */
        ctx->cellule_infos[total].consigne_courante = consigne_u16(e->plateau.consigne);
        total++;
    }

    /* --- Geometrie + visibilite : appliquees a TOUT le pool, y compris les
     * cellules au-dela de `total` (masquees) -- systematique, jamais
     * incremental. */
    for (size_t i = 0; i < ECRAN_TEMPERATURES_CELLULES_MAX; i++) {
        ecran_temperatures_cellule_t *c = &ctx->cellules[i];
        if (i >= total) {
            lv_obj_add_flag(c->racine, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_clear_flag(c->racine, LV_OBJ_FLAG_HIDDEN);
        size_t ligne = i / geo.colonnes;
        size_t colonne = i % geo.colonnes;
        lv_coord_t x = MARGE + (lv_coord_t)(colonne * (geo.largeur + GRILLE_ECART));
        lv_coord_t y = TEMP_ZONE_Y + (lv_coord_t)(ligne * (geo.hauteur + GRILLE_ECART));
        lv_obj_set_size(c->racine, geo.largeur, geo.hauteur);
        lv_obj_set_pos(c->racine, x, y);
        lv_obj_set_style_text_font(c->valeur, geo.police_valeur, 0);
    }

    /* `outil_actif_connu` est relu par preset_bouton_cb() AU MOMENT DU CLIC
     * -- la buse que "PLA"/"PETG"/"ABS"/"Off" ciblent est TOUJOURS la buse
     * active, jamais un indice fige au dernier changement d'outil. Mis a
     * jour ICI meme si aucune ligne "Active: T.." n'est affichee sur cet
     * ecran (contrairement a l'ancien idle) : les prereglages en ont besoin
     * quand meme. */
    ctx->outil_actif_connu = e->outil_actif;

    /* --- Grisage integral, style RESOLU : cellules -- systematique a
     * chaque appel, jamais incremental (meme lecon que
     * tuile_griser()/ecran_accueil_hub.c). */
    uint32_t couleur_texte = donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_SECONDAIRE;
    uint32_t couleur_valeur = donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_PRINCIPAL;
    for (size_t i = 0; i < total; i++) {
        ecran_temperatures_cellule_t *c = &ctx->cellules[i];
        lv_obj_set_style_text_color(c->nom, lv_color_hex(couleur_texte), 0);
        lv_obj_set_style_text_color(c->valeur, lv_color_hex(couleur_valeur), 0);
        lv_obj_set_style_text_color(c->consigne, lv_color_hex(couleur_texte), 0);
    }
}

const ecran_desc_t ECRAN_TEMPERATURES = {
    .id = "temperatures",
    .titre = "Temperature",
    .taille_contexte = sizeof(ecran_temperatures_ctx_t),
    .construire = ecran_temperatures_construire,
    .mettre_a_jour = ecran_temperatures_mettre_a_jour,
    .detruire = NULL,
};
