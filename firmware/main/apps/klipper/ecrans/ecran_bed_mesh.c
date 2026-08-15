/* Implémentation : voir ecran_bed_mesh.h pour le contrat.
 *
 * Mise en page (742x436), enrichie au retour matériel du 2026-08-15
 * (légende + panneau d'infos + liste de profils demandés à la première
 * calibration réelle sur la CR-10 S5, mesh 21x21) :
 *   - colonne gauche : grille de chaleur (cellules bleu -> vert -> rouge sur
 *     [z_min..z_max]) + barre de légende verticale (dégradé fixe, seules les
 *     valeurs z_min/z_max changent) ;
 *   - colonne droite : panneau d'infos Name / Size / Max / Min / Range, avec
 *     la POSITION machine [x, y] des points extrêmes
 *     (bed_mesh_position_point()) ;
 *   - rangée du bas : Calibrate / Clear (confirmations) + Profiles, qui
 *     ouvre une liste modale des profils sauvegardés (bed_mesh_profils_lire,
 *     alimentée par la query WS ponctuelle) -- charger un profil passe par
 *     BED_MESH_PROFILE LOAD (constructeur host-testé de klipper_gcode.h) :
 *     la carte chargée revient alors d'elle-même par le flux d'abonnement.
 * Le gcode part par le chemin standard (idiome ecran_actions :
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
#include "klipper_gcode.h" /* klipper_gcode_bed_mesh_profil_load() */
#include "source_etat.h"


#define LARGEUR_CONTENU 742
#define HAUTEUR_CONTENU 436
#define MARGE 14

#define ENTETE_Y       8
#define ENTETE_HAUTEUR 26

#define GRILLE_Y        (ENTETE_Y + ENTETE_HAUTEUR + 8)
#define GRILLE_HAUTEUR  300
/* 430 -> 320 (retour utilisateur 2026-08-15) : la hauteur borne de toute
 * facon les cellules carrees (~292 px utiles pour un 21x21), la zone large
 * ne faisait que pousser le panneau d'infos -- resserree pour lui rendre
 * ~120 px. */
#define GRILLE_LARGEUR  320
#define GRILLE_ECART    2 /* liseré entre cellules (le fond du conteneur transparaît) */

#define LEGENDE_X        (MARGE + GRILLE_LARGEUR + 12)
#define LEGENDE_LARGEUR  18
#define LEGENDE_SEGMENTS 25 /* 25 x 12 px = les 300 px de la grille */
#define LEGENDE_ETIQ_X   (LEGENDE_X + LEGENDE_LARGEUR + 6)

/* 560 -> 440 : suit le resserrement de la grille ; le panneau gagne en
 * largeur ce qu'il faut pour la police 20 (lisibilite dalle 5"). */
#define INFOS_X       440
#define INFOS_LARGEUR (LARGEUR_CONTENU - INFOS_X - MARGE)

#define BOUTONS_Y       (GRILLE_Y + GRILLE_HAUTEUR + 10)
#define BOUTON_HAUTEUR  44
#define BOUTON_LARGEUR  200
#define BOUTON_ECART    16

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF
#define COULEUR_MODAL_FOND       0x1B2530
#define COULEUR_Z_BAS            0x2980B9 /* bleu : point le plus bas */
#define COULEUR_Z_MILIEU         0x2ECC71 /* vert : milieu de plage */
#define COULEUR_Z_HAUT           0xE74C3C /* rouge : point le plus haut */

_Static_assert(BOUTONS_Y + BOUTON_HAUTEUR <= HAUTEUR_CONTENU,
               "les boutons debordent de la hauteur du contenu");
_Static_assert(3 * BOUTON_LARGEUR + 2 * BOUTON_ECART <= LARGEUR_CONTENU - 2 * MARGE,
               "les trois boutons debordent de la largeur du contenu");
_Static_assert(LEGENDE_ETIQ_X < INFOS_X, "la legende empiete sur le panneau d'infos");

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

/* Couleur du dégradé bleu -> vert -> rouge pour une fraction [0..1] --
 * partagée entre les cellules de la grille et la barre de légende (la même
 * rampe, sinon la légende mentirait). */
static lv_color_t couleur_fraction(float fraction)
{
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

/* Couleur d'une cellule : fraction de [z_min..z_max]. Plage nulle (mesh
 * parfaitement plat, ou un seul point) : tout vert. */
static lv_color_t couleur_cellule(float z, float z_min, float z_max)
{
    float plage = z_max - z_min;
    if (plage <= 0.000001f) {
        return lv_color_hex(COULEUR_Z_MILIEU);
    }
    return couleur_fraction((z - z_min) / plage);
}

/* Scratch de lecture du store (~2,6 Ko depuis BED_MESH_MAX=25) : JAMAIS sur
 * la pile LVGL (contrat de bed_mesh_lire() + leçon etat-vs-piles) -- PSRAM
 * paresseux côté ESP, statique côté host (mono-tâche LVGL, une seule
 * instance d'écran). */
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

/* Panneau d'infos : format calqué sur la demande utilisateur du 2026-08-15
 * (Name / Size / Max [x, y] + mm / Min [x, y] + mm / Range). Les positions
 * machine des extrêmes sortent de bed_mesh_position_point(). */
static void remplir_infos(lv_obj_t *etiquette, const bed_mesh_t *mesh)
{
    if (!mesh->present || mesh->nb_x == 0 || mesh->nb_y == 0) {
        lv_label_set_text(etiquette, "");
        return;
    }

    /* Positions des extrêmes : re-balayage avec indices (le store ne retient
       que les valeurs z_min/z_max, pas où elles sont). */
    uint8_t ligne_min = 0, colonne_min = 0, ligne_max = 0, colonne_max = 0;
    float   z_min = mesh->z[0][0], z_max = mesh->z[0][0];
    for (uint8_t ligne = 0; ligne < mesh->nb_y; ligne++) {
        for (uint8_t colonne = 0; colonne < mesh->nb_x; colonne++) {
            float z = mesh->z[ligne][colonne];
            if (z < z_min) {
                z_min = z;
                ligne_min = ligne;
                colonne_min = colonne;
            }
            if (z > z_max) {
                z_max = z;
                ligne_max = ligne;
                colonne_max = colonne;
            }
        }
    }
    float x_min = 0.0f, y_min = 0.0f, x_max = 0.0f, y_max = 0.0f;
    bool positions_ok = bed_mesh_position_point(mesh, ligne_max, colonne_max, &x_max, &y_max) &&
                        bed_mesh_position_point(mesh, ligne_min, colonne_min, &x_min, &y_min);

    /* Format compact "libellé : valeur", une ligne par info (retour
       utilisateur 2026-08-15 : le format exact importe moins que de VOIR
       les valeurs -- en police 20, une ligne par info tient dans la
       largeur, deux n'y tiendraient plus en hauteur). %.4g sur les
       positions : "162.5" reste "162.5", "420.0" devient "420". */
    char infos[256];
    if (positions_ok) {
        snprintf(infos, sizeof(infos),
                 "%s\n%ux%u\n\nMax  %.3f\n[%.4g, %.4g]\n\nMin  %.3f\n[%.4g, %.4g]\n\nRange  %.3f",
                 mesh->profil[0] != '\0' ? mesh->profil : "(none)",
                 (unsigned)mesh->nb_x, (unsigned)mesh->nb_y,
                 (double)z_max, (double)x_max, (double)y_max,
                 (double)z_min, (double)x_min, (double)y_min,
                 (double)(z_max - z_min));
    } else {
        snprintf(infos, sizeof(infos),
                 "%s\n%ux%u\n\nMax  %.3f\n\nMin  %.3f\n\nRange  %.3f",
                 mesh->profil[0] != '\0' ? mesh->profil : "(none)",
                 (unsigned)mesh->nb_x, (unsigned)mesh->nb_y,
                 (double)z_max, (double)z_min, (double)(z_max - z_min));
    }
    lv_label_set_text(etiquette, infos);
}

/* Reconstruit grille + légende + infos depuis le store -- appelée au premier
 * rendu et à chaque changement de génération. Détruit puis recrée les
 * cellules : un mesh change à la calibration, pas en continu (voir
 * ecran_bed_mesh.h). */
static bool reconstruire(ecran_bed_mesh_ctx_t *ctx)
{
    bed_mesh_t *mesh = scratch_obtenir();
    if (mesh == NULL) {
        return false; /* PSRAM épuisée : le rappelant NE DOIT PAS estampiller
                         la génération (revue 2026-08-15 L4), sinon ce
                         pompage-ci serait marqué vu et jamais retenté */
    }
    bed_mesh_lire(mesh);

    if (!mesh->present) {
        lv_label_set_text(ctx->entete, "No mesh - run Calibrate or load a profile");
    } else if (mesh->tronquee) {
        lv_label_set_text(ctx->entete, "Mesh larger than the display grid - truncated");
    } else {
        lv_label_set_text(ctx->entete, "");
    }

    remplir_infos(ctx->panneau_infos, mesh);

    bool legende_visible = mesh->present && mesh->nb_x > 0 && mesh->nb_y > 0;
    if (legende_visible) {
        char valeur[24];
        snprintf(valeur, sizeof(valeur), "%.3f", (double)mesh->z_max);
        lv_label_set_text(ctx->etiquette_z_haut, valeur);
        snprintf(valeur, sizeof(valeur), "%.3f", (double)mesh->z_min);
        lv_label_set_text(ctx->etiquette_z_bas, valeur);
    } else {
        lv_label_set_text(ctx->etiquette_z_haut, "");
        lv_label_set_text(ctx->etiquette_z_bas, "");
    }

    lv_obj_clean(ctx->zone_grille); /* détruit toutes les cellules précédentes */
    ctx->nb_cellules_x = mesh->nb_x;
    ctx->nb_cellules_y = mesh->nb_y;
    if (!legende_visible) {
        return true; /* rien à dessiner, mais la lecture a bien eu lieu */
    }

    lv_coord_t cellule_l = (lv_coord_t)((GRILLE_LARGEUR - (mesh->nb_x - 1) * GRILLE_ECART) / mesh->nb_x);
    lv_coord_t cellule_h = (lv_coord_t)((GRILLE_HAUTEUR - (mesh->nb_y - 1) * GRILLE_ECART) / mesh->nb_y);
    /* Cellules carrées (la plus petite dimension gagne) : un mesh 5x5 sur
       une zone large resterait sinon des bandes étirées illisibles. */
    lv_coord_t cote = cellule_l < cellule_h ? cellule_l : cellule_h;
    lv_coord_t grille_l = (lv_coord_t)(mesh->nb_x * cote + (mesh->nb_x - 1) * GRILLE_ECART);
    lv_coord_t grille_h = (lv_coord_t)(mesh->nb_y * cote + (mesh->nb_y - 1) * GRILLE_ECART);
    lv_coord_t origine_x = (lv_coord_t)((GRILLE_LARGEUR - grille_l) / 2);
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

/* ------------------------------------------------------------------------
 * Liste modale des profils sauvegardés
 * ---------------------------------------------------------------------- */

static void modal_fermer(ecran_bed_mesh_ctx_t *ctx)
{
    if (ctx->modal != NULL) {
        lv_obj_del(ctx->modal);
        ctx->modal = NULL;
    }
}

static void modal_fermer_cb(lv_event_t *e)
{
    ecran_bed_mesh_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx != NULL) {
        modal_fermer(ctx);
    }
}

static void rappel_charger_profil(bool confirme, void *contexte)
{
    ecran_bed_mesh_ctx_t *ctx = contexte;
    if (!confirme || ctx == NULL) {
        return;
    }
    char script[64];
    /* Constructeur pur host-testé (jamais un snprintf local) -- un nom que
       la barrière refuse a déjà été écarté de la liste à l'affichage. */
    if (klipper_gcode_bed_mesh_profil_load(script, sizeof(script), ctx->profil_choisi)) {
        envoyer_gcode(script);
    }
}

static void rangee_profil_cb(lv_event_t *e)
{
    ecran_bed_mesh_ctx_t *ctx = lv_event_get_user_data(e);
    lv_obj_t *bouton = lv_event_get_target(e);
    if (ctx == NULL || bouton == NULL) {
        return;
    }
    lv_obj_t *etiquette = lv_obj_get_child(bouton, 0);
    if (etiquette == NULL) {
        return;
    }
    /* Le texte de la rangée = le nom, éventuellement suivi du suffixe
       "  (active)". Un nom valide ne contient JAMAIS d'espace (barrière du
       constructeur gcode, la liste écarte déjà les autres) : couper au
       premier espace redonne donc le nom exact -- copié dans
       ctx->profil_choisi AVANT la fermeture du modal (qui détruit le texte). */
    const char *texte = lv_label_get_text(etiquette);
    if (texte == NULL || texte[0] == '\0') {
        return;
    }
    size_t fin = strcspn(texte, " ");
    if (fin == 0 || fin >= sizeof(ctx->profil_choisi)) {
        return;
    }
    memcpy(ctx->profil_choisi, texte, fin);
    ctx->profil_choisi[fin] = '\0';
    modal_fermer(ctx); /* AVANT la confirmation : jamais deux modaux empilés */
    char message[64];
    snprintf(message, sizeof(message), "Load bed mesh profile '%s'?", ctx->profil_choisi);
    confirmation_ouvrir("Load profile?", message, "Load", /*destructif=*/false,
                        rappel_charger_profil, ctx);
}

static void modal_ouvrir(ecran_bed_mesh_ctx_t *ctx)
{
    if (ctx->modal != NULL) {
        return; /* déjà ouvert */
    }

    /* La liste ET le profil actif, lus à l'ouverture (pas de rafraîchissement
       modal ouvert : la liste ne change qu'à un SAVE_CONFIG, qui redémarre
       Klippy et referme de toute façon la session). ~230 o + ~2,6 Ko via le
       scratch partagé : rien sur la pile au-delà de profils. */
    bed_mesh_profils_t profils;
    bed_mesh_profils_lire(&profils);
    const char *profil_actif = "";
    bed_mesh_t *mesh = scratch_obtenir();
    if (mesh != NULL) {
        bed_mesh_lire(mesh);
        if (mesh->present) {
            profil_actif = mesh->profil;
        }
    }

    lv_obj_t *voile = lv_obj_create(ctx->parent);
    lv_obj_remove_style_all(voile);
    lv_obj_set_size(voile, LARGEUR_CONTENU, HAUTEUR_CONTENU);
    lv_obj_set_pos(voile, 0, 0);
    lv_obj_set_style_bg_color(voile, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(voile, LV_OPA_60, 0);
    lv_obj_clear_flag(voile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(voile, LV_OBJ_FLAG_CLICKABLE); /* absorbe les taps hors panneau */
    ctx->modal = voile;

    /* Rangées au MÊME gabarit que la liste de fichiers (ecran_fichiers.c :
       hauteur 52, écart 6, rayon 8, texte 14 aligné à gauche, points de
       suspension) -- retour utilisateur 2026-08-15 : "homogène". */
    lv_coord_t rangee_h = 52;
    lv_coord_t nb_rangees = (profils.nb > 0) ? profils.nb : 1; /* 1 = "No saved profiles" */
    lv_coord_t panneau_h = (lv_coord_t)(52 + nb_rangees * (rangee_h + 6) + 56);
    if (panneau_h > HAUTEUR_CONTENU - 16) {
        panneau_h = HAUTEUR_CONTENU - 16; /* 8 profils max : n'arrive qu'en théorie */
    }
    lv_obj_t *panneau = lv_obj_create(voile);
    lv_obj_remove_style_all(panneau);
    lv_obj_set_size(panneau, 500, panneau_h);
    lv_obj_align(panneau, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(panneau, lv_color_hex(COULEUR_MODAL_FOND), 0);
    lv_obj_set_style_bg_opa(panneau, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panneau, 8, 0);
    lv_obj_clear_flag(panneau, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *titre = lv_label_create(panneau);
    lv_obj_set_style_text_font(titre, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(titre, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_obj_set_pos(titre, 16, 12);
    lv_label_set_text(titre, profils.tronques ? "Saved profiles (list truncated)" : "Saved profiles");

    lv_coord_t y = 52;
    if (profils.nb == 0) {
        lv_obj_t *vide = lv_label_create(panneau);
        lv_obj_set_style_text_font(vide, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(vide, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
        lv_obj_set_pos(vide, 16, (lv_coord_t)(y + 12));
        lv_label_set_text(vide, "No saved profiles (SAVE_CONFIG after calibrating)");
        y = (lv_coord_t)(y + rangee_h + 6);
    }
    for (uint8_t i = 0; i < profils.nb; i++) {
        lv_obj_t *rangee = lv_button_create(panneau);
        lv_coord_t rangee_l = 500 - 32;
        lv_obj_set_size(rangee, rangee_l, rangee_h);
        lv_obj_set_pos(rangee, 16, y);
        lv_obj_set_style_bg_color(rangee, lv_color_hex(COULEUR_BOUTON), 0);
        lv_obj_set_style_border_width(rangee, 0, 0);
        lv_obj_set_style_shadow_width(rangee, 0, 0);
        lv_obj_set_style_radius(rangee, 8, 0);
        lv_obj_t *etiquette = lv_label_create(rangee);
        lv_obj_set_style_text_font(etiquette, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(etiquette, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
        bool actif = (profil_actif[0] != '\0') && (strcmp(profils.noms[i], profil_actif) == 0);
        char texte[40];
        snprintf(texte, sizeof(texte), "%s%s", profils.noms[i], actif ? "  (active)" : "");
        lv_label_set_long_mode(etiquette, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(etiquette, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_width(etiquette, rangee_l - 12);
        lv_obj_update_layout(etiquette); /* meme piege de largeur que ecran_fichiers.c */
        lv_label_set_text(etiquette, texte);
        lv_obj_center(etiquette);
        /* Le rappel retrouve le nom en coupant le texte du label au premier
           espace (voir rangee_profil_cb) : un nom écarté par la barrière du
           constructeur gcode n'est de toute façon pas chargeable, on ne
           l'affiche que pour l'honnêteté de la liste. */
        lv_obj_add_event_cb(rangee, rangee_profil_cb, LV_EVENT_CLICKED, ctx);
        y = (lv_coord_t)(y + rangee_h + 6);
    }

    lv_obj_t *fermer = lv_button_create(panneau);
    lv_obj_set_size(fermer, 140, 40);
    lv_obj_align(fermer, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(fermer, lv_color_hex(COULEUR_BOUTON), 0);
    lv_obj_set_style_border_width(fermer, 0, 0);
    lv_obj_set_style_shadow_width(fermer, 0, 0);
    lv_obj_set_style_radius(fermer, 8, 0);
    lv_obj_t *etiquette_fermer = lv_label_create(fermer);
    lv_obj_set_style_text_color(etiquette_fermer, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_label_set_text(etiquette_fermer, "Close");
    lv_obj_center(etiquette_fermer);
    lv_obj_add_event_cb(fermer, modal_fermer_cb, LV_EVENT_CLICKED, ctx);
}

static void bouton_profils_cb(lv_event_t *e)
{
    ecran_bed_mesh_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx != NULL) {
        modal_ouvrir(ctx);
    }
}

/* ------------------------------------------------------------------------
 * Construction de l'écran
 * ---------------------------------------------------------------------- */

static lv_obj_t *bouton_creer(lv_obj_t *parent, lv_coord_t x, const char *texte,
                              lv_event_cb_t rappel, void *contexte)
{
    lv_obj_t *bouton = lv_button_create(parent);
    lv_obj_set_size(bouton, BOUTON_LARGEUR, BOUTON_HAUTEUR);
    lv_obj_set_pos(bouton, x, BOUTONS_Y);
    lv_obj_set_style_bg_color(bouton, lv_color_hex(COULEUR_BOUTON), 0);
    lv_obj_t *etiquette = lv_label_create(bouton);
    lv_obj_set_style_text_color(etiquette, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_label_set_text(etiquette, texte);
    lv_obj_center(etiquette);
    lv_obj_add_event_cb(bouton, rappel, LV_EVENT_CLICKED, contexte);
    return bouton;
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
    ctx->parent = parent;
    ctx->modal = NULL;
    ctx->profil_choisi[0] = '\0';

    ctx->entete = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->entete, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->entete, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_obj_set_pos(ctx->entete, MARGE, ENTETE_Y);
    lv_obj_set_size(ctx->entete, LARGEUR_CONTENU - 2 * MARGE, ENTETE_HAUTEUR);
    lv_label_set_text(ctx->entete, "");

    ctx->zone_grille = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->zone_grille);
    lv_obj_set_pos(ctx->zone_grille, MARGE, GRILLE_Y);
    lv_obj_set_size(ctx->zone_grille, GRILLE_LARGEUR, GRILLE_HAUTEUR);
    lv_obj_clear_flag(ctx->zone_grille, LV_OBJ_FLAG_SCROLLABLE);

    /* Barre de légende : le dégradé est FIXE (c'est la rampe de couleurs,
       pas les valeurs) -- construite une fois ici, seules les étiquettes
       z_min/z_max changent à chaque mesh (reconstruire()). Segment 0 en
       haut = z_max (rouge), comme la grille (le haut = le plus haut). */
    lv_coord_t segment_h = GRILLE_HAUTEUR / LEGENDE_SEGMENTS;
    for (int i = 0; i < LEGENDE_SEGMENTS; i++) {
        lv_obj_t *segment = lv_obj_create(parent);
        lv_obj_remove_style_all(segment);
        lv_obj_set_size(segment, LEGENDE_LARGEUR, segment_h);
        lv_obj_set_pos(segment, LEGENDE_X, (lv_coord_t)(GRILLE_Y + i * segment_h));
        lv_obj_set_style_bg_opa(segment, LV_OPA_COVER, 0);
        float fraction = 1.0f - (float)i / (float)(LEGENDE_SEGMENTS - 1);
        lv_obj_set_style_bg_color(segment, couleur_fraction(fraction), 0);
    }

    ctx->etiquette_z_haut = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->etiquette_z_haut, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->etiquette_z_haut, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_obj_set_pos(ctx->etiquette_z_haut, LEGENDE_ETIQ_X, GRILLE_Y);
    lv_label_set_text(ctx->etiquette_z_haut, "");

    ctx->etiquette_z_bas = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->etiquette_z_bas, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->etiquette_z_bas, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_obj_set_pos(ctx->etiquette_z_bas, LEGENDE_ETIQ_X, GRILLE_Y + GRILLE_HAUTEUR - 18);
    lv_label_set_text(ctx->etiquette_z_bas, "");

    ctx->panneau_infos = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->panneau_infos, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->panneau_infos, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_obj_set_pos(ctx->panneau_infos, INFOS_X, GRILLE_Y);
    lv_obj_set_width(ctx->panneau_infos, INFOS_LARGEUR);
    lv_label_set_text(ctx->panneau_infos, "");

    lv_coord_t boutons_x = (LARGEUR_CONTENU - (3 * BOUTON_LARGEUR + 2 * BOUTON_ECART)) / 2;
    ctx->bouton_calibrer = bouton_creer(parent, boutons_x, "Calibrate", bouton_calibrer_cb, NULL);
    ctx->bouton_effacer = bouton_creer(parent, (lv_coord_t)(boutons_x + BOUTON_LARGEUR + BOUTON_ECART),
                                       "Clear", bouton_effacer_cb, NULL);
    ctx->bouton_profils = bouton_creer(parent, (lv_coord_t)(boutons_x + 2 * (BOUTON_LARGEUR + BOUTON_ECART)),
                                       "Profiles", bouton_profils_cb, ctx);

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
