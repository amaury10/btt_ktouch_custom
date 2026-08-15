/* Implémentation : voir ecran_bed_mesh.h pour le contrat.
 *
 * Mise en page (742x436) : rangée d'en-tête (profil + bornes Z), zone de
 * grille centrale (cellules colorées bleu -> vert -> rouge sur
 * [z_min..z_max]), rangée de boutons en bas (Calibrate / Clear, sous
 * confirmation -- une calibration dure plusieurs minutes, un Clear la
 * jette). Le gcode part par le chemin standard (idiome ecran_actions :
 * construire_arguments_gcode()/ui_commander()). */
#include "ecran_bed_mesh.h"

#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#include "cJSON.h"

#include "backend.h"
#include "bed_mesh_store.h"
#include "confirmation.h"
#include "source_etat.h"


#define LARGEUR_CONTENU 742
#define HAUTEUR_CONTENU 436
#define MARGE 14

#define ENTETE_Y       8
#define ENTETE_HAUTEUR 26

#define GRILLE_Y       (ENTETE_Y + ENTETE_HAUTEUR + 8)
#define GRILLE_HAUTEUR 300
#define GRILLE_ECART   2 /* liseré entre cellules (le fond du conteneur transparaît) */

#define BOUTONS_Y       (GRILLE_Y + GRILLE_HAUTEUR + 10)
#define BOUTON_HAUTEUR  44
#define BOUTON_LARGEUR  200

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF
#define COULEUR_Z_BAS            0x2980B9 /* bleu : point le plus bas */
#define COULEUR_Z_MILIEU         0x2ECC71 /* vert : milieu de plage */
#define COULEUR_Z_HAUT           0xE74C3C /* rouge : point le plus haut */

_Static_assert(BOUTONS_Y + BOUTON_HAUTEUR <= HAUTEUR_CONTENU,
               "les boutons debordent de la hauteur du contenu");

/* Même paire construire_arguments_gcode()/envoyer_gcode() que
 * ecran_actions.c (voir son commentaire sur cJSON plutôt qu'un snprintf). */
#define GCODE_ARGS_MAX 96

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
    char arguments[GCODE_ARGS_MAX];
    size_t longueur = strlen(texte);
    if (longueur < sizeof(arguments)) {
        memcpy(arguments, texte, longueur + 1);
        ui_commander(BACKEND_ACTION_GCODE, arguments);
    }
    cJSON_free(texte);
}

static void rappel_calibrer(bool confirme, void *contexte)
{
    (void)contexte;
    if (confirme) {
        envoyer_gcode("BED_MESH_CALIBRATE");
    }
}

static void rappel_effacer(bool confirme, void *contexte)
{
    (void)contexte;
    if (confirme) {
        envoyer_gcode("BED_MESH_CLEAR");
    }
}

static void bouton_calibrer_cb(lv_event_t *e)
{
    (void)e;
    confirmation_ouvrir("Calibrate bed mesh?", "Bed must be homed first. Takes several minutes.",
                        "Calibrate", /*destructif=*/false, rappel_calibrer, NULL);
}

static void bouton_effacer_cb(lv_event_t *e)
{
    (void)e;
    confirmation_ouvrir("Clear bed mesh?", "The measured mesh will be discarded.", "Clear",
                        /*destructif=*/true, rappel_effacer, NULL);
}

/* Couleur d'une cellule : interpolation bleu -> vert -> rouge sur la
 * fraction [0..1] de [z_min..z_max]. Plage nulle (mesh parfaitement plat,
 * ou un seul point) : tout vert. */
static lv_color_t couleur_cellule(float z, float z_min, float z_max)
{
    float plage = z_max - z_min;
    if (plage <= 0.000001f) {
        return lv_color_hex(COULEUR_Z_MILIEU);
    }
    float fraction = (z - z_min) / plage;
    if (fraction < 0.0f) {
        fraction = 0.0f;
    }
    if (fraction > 1.0f) {
        fraction = 1.0f;
    }
    if (fraction < 0.5f) {
        return lv_color_mix(lv_color_hex(COULEUR_Z_MILIEU), lv_color_hex(COULEUR_Z_BAS),
                            (uint8_t)(fraction * 2.0f * 255.0f));
    }
    return lv_color_mix(lv_color_hex(COULEUR_Z_HAUT), lv_color_hex(COULEUR_Z_MILIEU),
                        (uint8_t)((fraction - 0.5f) * 2.0f * 255.0f));
}

/* Scratch de lecture du store (~1 Ko) : JAMAIS sur la pile LVGL (contrat de
 * bed_mesh_lire() + leçon etat-vs-piles) -- PSRAM paresseux côté ESP,
 * statique côté host (mono-tâche LVGL, une seule instance d'écran). */
static bed_mesh_t *scratch_obtenir(void)
{
#ifdef ESP_PLATFORM
    static bed_mesh_t *s_scratch;
    if (s_scratch == NULL) {
        s_scratch = (bed_mesh_t *)heap_caps_malloc(sizeof(*s_scratch), MALLOC_CAP_SPIRAM);
        /* Echec (PSRAM epuisee) : NULL rendu, reconstruire() saute ce
           pompage et retentera -- deja journalise par la premiere alloc
           PSRAM qui echoue ailleurs, inutile d'ajouter du bruit ici. */
    }
    return s_scratch;
#else
    static bed_mesh_t s_scratch_hote;
    return &s_scratch_hote;
#endif
}

/* Reconstruit la grille depuis le store -- appelée au premier rendu et à
 * chaque changement de génération. Détruit puis recrée les cellules :
 * un mesh change à la calibration, pas en continu (voir ecran_bed_mesh.h). */
static bool reconstruire(ecran_bed_mesh_ctx_t *ctx)
{
    bed_mesh_t *mesh = scratch_obtenir();
    if (mesh == NULL) {
        return false; /* PSRAM épuisée : le rappelant NE DOIT PAS estampiller
                         la génération (revue 2026-08-15 L4), sinon ce
                         pompage-ci serait marqué vu et jamais retenté */
    }
    bed_mesh_lire(mesh);

    char entete[96];
    if (!mesh->present) {
        snprintf(entete, sizeof(entete), "No mesh - run Calibrate");
    } else {
        snprintf(entete, sizeof(entete), "Profile: %s   Z: %.3f .. %.3f%s",
                 mesh->profil[0] != '\0' ? mesh->profil : "(none)",
                 (double)mesh->z_min, (double)mesh->z_max,
                 mesh->tronquee ? "   (truncated)" : "");
    }
    lv_label_set_text(ctx->entete, entete);

    lv_obj_clean(ctx->zone_grille); /* détruit toutes les cellules précédentes */
    ctx->nb_cellules_x = mesh->nb_x;
    ctx->nb_cellules_y = mesh->nb_y;
    if (!mesh->present || mesh->nb_x == 0 || mesh->nb_y == 0) {
        return true; /* rien à dessiner, mais la lecture a bien eu lieu */
    }

    lv_coord_t largeur_zone = LARGEUR_CONTENU - 2 * MARGE;
    lv_coord_t cellule_l = (lv_coord_t)((largeur_zone - (mesh->nb_x - 1) * GRILLE_ECART) / mesh->nb_x);
    lv_coord_t cellule_h = (lv_coord_t)((GRILLE_HAUTEUR - (mesh->nb_y - 1) * GRILLE_ECART) / mesh->nb_y);
    /* Cellules carrées (la plus petite dimension gagne) : un mesh 5x5 sur
       une zone large resterait sinon des bandes étirées illisibles. */
    lv_coord_t cote = cellule_l < cellule_h ? cellule_l : cellule_h;
    lv_coord_t grille_l = (lv_coord_t)(mesh->nb_x * cote + (mesh->nb_x - 1) * GRILLE_ECART);
    lv_coord_t grille_h = (lv_coord_t)(mesh->nb_y * cote + (mesh->nb_y - 1) * GRILLE_ECART);
    lv_coord_t origine_x = (lv_coord_t)((largeur_zone - grille_l) / 2);
    lv_coord_t origine_y = (lv_coord_t)((GRILLE_HAUTEUR - grille_h) / 2);

    for (uint8_t ligne = 0; ligne < mesh->nb_y; ligne++) {
        for (uint8_t colonne = 0; colonne < mesh->nb_x; colonne++) {
            lv_obj_t *cellule = lv_obj_create(ctx->zone_grille);
            lv_obj_remove_style_all(cellule);
            lv_obj_set_size(cellule, cote, cote);
            /* Ligne 0 de la matrice = Y minimal du lit : affichée EN BAS
               (repère machine, l'avant du lit en bas de l'écran). */
            lv_obj_set_pos(cellule,
                           (lv_coord_t)(origine_x + colonne * (cote + GRILLE_ECART)),
                           (lv_coord_t)(origine_y + (mesh->nb_y - 1 - ligne) * (cote + GRILLE_ECART)));
            lv_obj_set_style_bg_opa(cellule, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(cellule,
                                      couleur_cellule(mesh->z[ligne][colonne], mesh->z_min, mesh->z_max), 0);
            lv_obj_set_style_radius(cellule, 2, 0);
        }
    }
    return true;
}

static void ecran_bed_mesh_construire(lv_obj_t *parent, void *contexte)
{
    ecran_bed_mesh_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    ctx->entete = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->entete, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->entete, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_obj_set_pos(ctx->entete, MARGE, ENTETE_Y);
    lv_obj_set_size(ctx->entete, LARGEUR_CONTENU - 2 * MARGE, ENTETE_HAUTEUR);
    lv_label_set_text(ctx->entete, "");

    ctx->zone_grille = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->zone_grille);
    lv_obj_set_pos(ctx->zone_grille, MARGE, GRILLE_Y);
    lv_obj_set_size(ctx->zone_grille, LARGEUR_CONTENU - 2 * MARGE, GRILLE_HAUTEUR);
    lv_obj_clear_flag(ctx->zone_grille, LV_OBJ_FLAG_SCROLLABLE);

    ctx->bouton_calibrer = lv_button_create(parent);
    lv_obj_set_size(ctx->bouton_calibrer, BOUTON_LARGEUR, BOUTON_HAUTEUR);
    lv_obj_set_pos(ctx->bouton_calibrer, LARGEUR_CONTENU / 2 - BOUTON_LARGEUR - 10, BOUTONS_Y);
    lv_obj_set_style_bg_color(ctx->bouton_calibrer, lv_color_hex(COULEUR_BOUTON), 0);
    lv_obj_t *label = lv_label_create(ctx->bouton_calibrer);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_label_set_text(label, "Calibrate");
    lv_obj_center(label);
    lv_obj_add_event_cb(ctx->bouton_calibrer, bouton_calibrer_cb, LV_EVENT_CLICKED, NULL);

    ctx->bouton_effacer = lv_button_create(parent);
    lv_obj_set_size(ctx->bouton_effacer, BOUTON_LARGEUR, BOUTON_HAUTEUR);
    lv_obj_set_pos(ctx->bouton_effacer, LARGEUR_CONTENU / 2 + 10, BOUTONS_Y);
    lv_obj_set_style_bg_color(ctx->bouton_effacer, lv_color_hex(COULEUR_BOUTON), 0);
    label = lv_label_create(ctx->bouton_effacer);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_label_set_text(label, "Clear");
    lv_obj_center(label);
    lv_obj_add_event_cb(ctx->bouton_effacer, bouton_effacer_cb, LV_EVENT_CLICKED, NULL);

    ctx->derniere_generation = 0;
    ctx->nb_cellules_x = 0;
    ctx->nb_cellules_y = 0;
    /* Génération échantillonnée AVANT la lecture (revue 2026-08-15 L5) :
       un dépôt WS qui tombe PENDANT reconstruire() doit laisser une
       génération plus récente que celle estampillée, pour que le prochain
       pompage redessine -- l'ordre inverse le marquait vu sans jamais
       l'afficher. */
    uint32_t generation = bed_mesh_generation();
    if (reconstruire(ctx)) {
        ctx->derniere_generation = generation;
    }
}

static void ecran_bed_mesh_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    /* L'écran ne lit RIEN de etat_klipper_t : sa source est le store dédié
       bed_mesh_store.h (même choix documenté que ecran_usb.c). */
    (void)etat;
    (void)donnees_perimees;
    ecran_bed_mesh_ctx_t *ctx = contexte;
    if (ctx == NULL) {
        return;
    }
    uint32_t generation = bed_mesh_generation();
    if (generation != ctx->derniere_generation) {
        if (reconstruire(ctx)) {
            ctx->derniere_generation = generation;
        }
    }
}

const ecran_desc_t ECRAN_BED_MESH = {
    .id = "bed_mesh",
    .titre = "Bed Mesh",
    .taille_contexte = sizeof(ecran_bed_mesh_ctx_t),
    .construire = ecran_bed_mesh_construire,
    .mettre_a_jour = ecran_bed_mesh_mettre_a_jour,
    .detruire = NULL,
};
