/* Implémentation : voir ecran_input_shaper.h pour le contrat et le choix
 * d'interaction (cycle synchronisé sur l'état distant).
 *
 * Mise en page (742x436) : deux blocs empilés (axe X puis axe Y), chacun =
 * titre + bouton type + bouton fréquence ; mention « volatile » en bas.
 * gcode via le chemin standard (idiome ecran_actions/ecran_bed_mesh). */
#include "ecran_input_shaper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"

#include "backend.h"
#include "klipper_gcode.h" /* constructeurs SET_INPUT_SHAPER host-testes */
#include "clavier.h"
#include "etat_klipper.h"
#include "habillage.h" /* habillage_notifier() -- frequence hors bornes */
#include "source_etat.h"

#define LARGEUR_CONTENU 742
#define HAUTEUR_CONTENU 436
#define MARGE 20

#define BLOC_X_Y        24
#define BLOC_Y_Y        190
#define TITRE_HAUTEUR   26
#define BOUTON_HAUTEUR  56
#define BOUTON_TYPE_L   300
#define BOUTON_FREQ_L   220
#define MENTION_Y       (HAUTEUR_CONTENU - 40)

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF

/* Les six types Klipper, dans l'ordre de cycle. */
static const char *const TYPES[] = { "zv", "mzv", "zvd", "ei", "2hump_ei", "3hump_ei" };
#define NB_TYPES (sizeof(TYPES) / sizeof(TYPES[0]))

/* Même envoi gcode que ecran_bed_mesh.c (idiome ecran_actions). */
static void envoyer_gcode(const char *script)
{
    cJSON *racine = cJSON_CreateObject();
    if (racine == NULL) {
        return;
    }
    if (cJSON_AddStringToObject(racine, "script", script) == NULL) {
        cJSON_Delete(racine);
        return;
    }
    char *texte = cJSON_PrintUnformatted(racine);
    cJSON_Delete(racine);
    if (texte == NULL) {
        return;
    }
    char arguments[128];
    size_t longueur = strlen(texte);
    if (longueur < sizeof(arguments)) {
        memcpy(arguments, texte, longueur + 1);
        ui_commander(BACKEND_ACTION_GCODE, arguments);
    }
    cJSON_free(texte);
}

/* Type suivant dans le cycle -- type inconnu/vide (jamais reçu) : repart de
 * TYPES[0]. */
static const char *type_suivant(const char *courant)
{
    for (size_t i = 0; i < NB_TYPES; i++) {
        if (strcmp(TYPES[i], courant) == 0) {
            return TYPES[(i + 1) % NB_TYPES];
        }
    }
    return TYPES[0];
}

static void cycler_type(ecran_input_shaper_ctx_t *ctx, bool axe_y)
{
    const char *suivant = type_suivant(axe_y ? ctx->type_y : ctx->type_x);
    char script[64];
    /* Constructeur pur host-teste (revue 2026-08-15 L10), jamais un snprintf
       local -- meme regle que tous les panneaux de reglages. */
    if (klipper_gcode_input_shaper_type(script, sizeof(script), axe_y ? 'Y' : 'X', suivant)) {
        envoyer_gcode(script);
    }
    /* Pas de mise à jour locale du libellé : l'état distant (notify WS)
       fera foi au prochain pompage -- l'écran ne devance jamais
       l'imprimante (voir ecran_input_shaper.h). */
}

static void bouton_type_x_cb(lv_event_t *e)
{
    ecran_input_shaper_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx != NULL) {
        cycler_type(ctx, false);
    }
}

static void bouton_type_y_cb(lv_event_t *e)
{
    ecran_input_shaper_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx != NULL) {
        cycler_type(ctx, true);
    }
}

static void rappel_frequence(const char *valeur, void *contexte)
{
    ecran_input_shaper_ctx_t *ctx = contexte;
    if (ctx == NULL || valeur == NULL || valeur[0] == '\0') {
        return;
    }
    float frequence = strtof(valeur, NULL);
    char script[64];
    /* Les bornes 10-150 Hz vivent dans le constructeur (host-testees,
       revue 2026-08-15 L10) : ici on ne fait que notifier son refus. */
    if (!klipper_gcode_input_shaper_freq(script, sizeof(script),
                                         ctx->axe_y_en_saisie ? 'Y' : 'X', frequence)) {
        habillage_notifier("Frequency must be 10-150 Hz", true);
        return;
    }
    envoyer_gcode(script);
}

static void bouton_freq_x_cb(lv_event_t *e)
{
    ecran_input_shaper_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL) {
        return;
    }
    ctx->axe_y_en_saisie = false;
    char initiale[16];
    snprintf(initiale, sizeof(initiale), "%.1f", (double)ctx->freq_x);
    clavier_ouvrir("Shaper frequency X (Hz)", initiale, CLAVIER_NUMERIQUE, rappel_frequence, ctx);
}

static void bouton_freq_y_cb(lv_event_t *e)
{
    ecran_input_shaper_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL) {
        return;
    }
    ctx->axe_y_en_saisie = true;
    char initiale[16];
    snprintf(initiale, sizeof(initiale), "%.1f", (double)ctx->freq_y);
    clavier_ouvrir("Shaper frequency Y (Hz)", initiale, CLAVIER_NUMERIQUE, rappel_frequence, ctx);
}

static lv_obj_t *bouton_creer(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t largeur,
                              lv_event_cb_t rappel, void *contexte)
{
    lv_obj_t *bouton = lv_button_create(parent);
    lv_obj_set_size(bouton, largeur, BOUTON_HAUTEUR);
    lv_obj_set_pos(bouton, x, y);
    lv_obj_set_style_bg_color(bouton, lv_color_hex(COULEUR_BOUTON), 0);
    lv_obj_t *label = lv_label_create(bouton);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_label_set_text(label, "");
    lv_obj_center(label);
    lv_obj_add_event_cb(bouton, rappel, LV_EVENT_CLICKED, contexte);
    return bouton;
}

static void titre_creer(lv_obj_t *parent, lv_coord_t y, const char *texte)
{
    lv_obj_t *titre = lv_label_create(parent);
    lv_obj_set_style_text_font(titre, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(titre, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_obj_set_pos(titre, MARGE, y);
    lv_label_set_text(titre, texte);
}

static void ecran_input_shaper_construire(lv_obj_t *parent, void *contexte)
{
    ecran_input_shaper_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    titre_creer(parent, BLOC_X_Y, "X axis shaper (tap type to cycle)");
    ctx->bouton_type_x = bouton_creer(parent, MARGE, BLOC_X_Y + TITRE_HAUTEUR, BOUTON_TYPE_L,
                                      bouton_type_x_cb, ctx);
    ctx->bouton_freq_x = bouton_creer(parent, MARGE + BOUTON_TYPE_L + 20, BLOC_X_Y + TITRE_HAUTEUR,
                                      BOUTON_FREQ_L, bouton_freq_x_cb, ctx);

    titre_creer(parent, BLOC_Y_Y, "Y axis shaper (tap type to cycle)");
    ctx->bouton_type_y = bouton_creer(parent, MARGE, BLOC_Y_Y + TITRE_HAUTEUR, BOUTON_TYPE_L,
                                      bouton_type_y_cb, ctx);
    ctx->bouton_freq_y = bouton_creer(parent, MARGE + BOUTON_TYPE_L + 20, BLOC_Y_Y + TITRE_HAUTEUR,
                                      BOUTON_FREQ_L, bouton_freq_y_cb, ctx);

    lv_obj_t *mention = lv_label_create(parent);
    lv_obj_set_style_text_font(mention, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(mention, lv_color_hex(COULEUR_GRISE), 0);
    lv_obj_set_pos(mention, MARGE, MENTION_Y);
    lv_label_set_text(mention, "Not saved to printer config (lost on firmware restart)");

    ctx->type_x[0] = '\0';
    ctx->type_y[0] = '\0';
    ctx->freq_x = 0.0f;
    ctx->freq_y = 0.0f;
    ctx->axe_y_en_saisie = false;
}

static void ecran_input_shaper_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    ecran_input_shaper_ctx_t *ctx = contexte;
    const etat_klipper_t *e = etat;
    if (ctx == NULL || e == NULL) {
        return;
    }

    /* Copies bornees (memes precautions -Werror=format-truncation que les
       autres ecrans : tailles source/destination identiques). */
    size_t n = strlen(e->shaper_type_x);
    if (n >= sizeof(ctx->type_x)) {
        n = sizeof(ctx->type_x) - 1;
    }
    memcpy(ctx->type_x, e->shaper_type_x, n);
    ctx->type_x[n] = '\0';
    n = strlen(e->shaper_type_y);
    if (n >= sizeof(ctx->type_y)) {
        n = sizeof(ctx->type_y) - 1;
    }
    memcpy(ctx->type_y, e->shaper_type_y, n);
    ctx->type_y[n] = '\0';
    ctx->freq_x = e->shaper_freq_x;
    ctx->freq_y = e->shaper_freq_y;

    char texte[32];
    snprintf(texte, sizeof(texte), "%s", ctx->type_x[0] != '\0' ? ctx->type_x : "(not set)");
    lv_label_set_text(lv_obj_get_child(ctx->bouton_type_x, 0), texte);
    snprintf(texte, sizeof(texte), "%.1f Hz", (double)ctx->freq_x);
    lv_label_set_text(lv_obj_get_child(ctx->bouton_freq_x, 0), texte);
    snprintf(texte, sizeof(texte), "%s", ctx->type_y[0] != '\0' ? ctx->type_y : "(not set)");
    lv_label_set_text(lv_obj_get_child(ctx->bouton_type_y, 0), texte);
    snprintf(texte, sizeof(texte), "%.1f Hz", (double)ctx->freq_y);
    lv_label_set_text(lv_obj_get_child(ctx->bouton_freq_y, 0), texte);

    /* Donnees perimees : boutons grises (meme discipline que les panneaux),
       toujours cliquables -- une commande partira quand la liaison reviendra
       ou echouera proprement via le canal ui_commande_echec(). */
    lv_opa_t opacite = donnees_perimees ? LV_OPA_50 : LV_OPA_COVER;
    lv_obj_set_style_opa(ctx->bouton_type_x, opacite, 0);
    lv_obj_set_style_opa(ctx->bouton_freq_x, opacite, 0);
    lv_obj_set_style_opa(ctx->bouton_type_y, opacite, 0);
    lv_obj_set_style_opa(ctx->bouton_freq_y, opacite, 0);
}

const ecran_desc_t ECRAN_INPUT_SHAPER = {
    .id = "input_shaper",
    .titre = "Input Shaper",
    .taille_contexte = sizeof(ecran_input_shaper_ctx_t),
    .construire = ecran_input_shaper_construire,
    .mettre_a_jour = ecran_input_shaper_mettre_a_jour,
    .detruire = NULL,
};
