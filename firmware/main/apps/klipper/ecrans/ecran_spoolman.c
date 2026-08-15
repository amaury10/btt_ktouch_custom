/* Implémentation : voir ecran_spoolman.h pour le contrat et le raisonnement
 * de mise en page.
 *
 * Géométrie 742x436 : en-tête (bobine active / hors ligne), colonne de 5
 * rangées pleine largeur, ligne de pagination, rangée Refresh/Clear. Les
 * cinq rangées sont créées UNE FOIS à la construction puis masquées/
 * remplies à chaque pompage (jamais détruites/recréées : contrairement à la
 * grille du bed mesh, le nombre de rangées est fixe) -- même politique que
 * ecran_fichiers.c. */
#include "ecran_spoolman.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

#include "backend.h"
#include "confirmation.h"
#include "moonraker_ws.h" /* moonraker_ws_demander_bobines() -- bouton Refresh */
#include "source_etat.h"
#include "spoolman_store.h"

#define LARGEUR_CONTENU 742
#define HAUTEUR_CONTENU 436
#define MARGE 20

#define ENTETE_Y       8
#define ENTETE_HAUTEUR 22

#define GRILLE_Y       36
#define BOUTON_ECART_Y  6
#define BOUTON_LARGEUR (LARGEUR_CONTENU - 2 * MARGE)
#define BOUTON_HAUTEUR 52
#define GRILLE_BAS (GRILLE_Y + ECRAN_SPOOLMAN_PAGE_TAILLE * BOUTON_HAUTEUR + \
                    (ECRAN_SPOOLMAN_PAGE_TAILLE - 1) * BOUTON_ECART_Y)

#define PASTILLE_TAILLE 18
#define PASTILLE_X      10
#define LIBELLE_X       (PASTILLE_X + PASTILLE_TAILLE + 10)

#define PIED_HAUTEUR 44
#define PIED_Y (GRILLE_BAS + 8)
#define BOUTON_PAGE_LARGEUR 60
#define BOUTON_PIED_LARGEUR 150

/* Même convention que ecran_fichiers.c/ecran_macros.c : bande couverte par
 * le bandeau de notification de habillage.c, en coordonnées ABSOLUES. */
#define BARRE_HAUTEUR_ECRAN   44
#define HAUTEUR_ECRAN_TOTALE 480
#define BANDEAU_HAUTEUR_ECRAN 60
#define BANDEAU_Y_ECRAN (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280 /* peremption ET couleur de filament inconnue */
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_BOUTON_ACTIF     0x1F4D3A /* vert sombre : la bobine chargee */
#define COULEUR_TEXTE_BOUTON     0xFFFFFF
#define COULEUR_AVERTISSEMENT    0xF1C40F

#define BOUTON_DESACTIVE_MELANGE 90

_Static_assert(MARGE + BOUTON_LARGEUR + MARGE <= LARGEUR_CONTENU,
               "le bouton pleine largeur deborde de la largeur du contenu");
_Static_assert(ENTETE_Y + ENTETE_HAUTEUR <= GRILLE_Y,
               "l'en-tete chevauche la premiere rangee");
_Static_assert(GRILLE_BAS <= PIED_Y, "la colonne de bobines chevauche le pied de page");
_Static_assert(PIED_Y + PIED_HAUTEUR <= HAUTEUR_CONTENU,
               "le pied de page deborde de la hauteur du contenu");
_Static_assert(BARRE_HAUTEUR_ECRAN + PIED_Y + PIED_HAUTEUR <= BANDEAU_Y_ECRAN,
               "le pied de page chevauche la bande du bandeau de notification de l'habillage");

/* ------------------------------------------------------------------------
 * Envoi de la selection
 * ---------------------------------------------------------------------- */

/* {"spool_id":N} / {"spool_id":null} -- construit par cJSON plutot qu'un
 * snprintf, meme politique que les autres ecrans (voir
 * construire_arguments_gcode() dans rail_actions.c). */
#define SPOOLMAN_ARGS_MAX 48

static void envoyer_selection(int32_t id, bool aucune)
{
    cJSON *racine = cJSON_CreateObject();
    if (racine == NULL) {
        return;
    }
    cJSON *valeur = aucune ? cJSON_AddNullToObject(racine, "spool_id")
                           : cJSON_AddNumberToObject(racine, "spool_id", (double)id);
    if (valeur == NULL) {
        cJSON_Delete(racine);
        return;
    }
    char *texte = cJSON_PrintUnformatted(racine);
    cJSON_Delete(racine);
    if (texte == NULL) {
        return;
    }
    char arguments[SPOOLMAN_ARGS_MAX];
    size_t longueur = strlen(texte);
    if (longueur < sizeof(arguments)) {
        memcpy(arguments, texte, longueur + 1);
        ui_commander(BACKEND_ACTION_SPOOLMAN, arguments);
    }
    cJSON_free(texte);
    /* Pas de mise a jour locale de l'etat : Moonraker confirme par
       notify_active_spool_set (RPC_MSG_SPOOL_ACTIF), qui alimente le store.
       L'ecran ne devance jamais le serveur -- meme regle que
       ecran_input_shaper.c. */
}

static void rappel_selection(bool confirme, void *contexte)
{
    ecran_spoolman_ctx_t *ctx = contexte;
    if (confirme && ctx != NULL) {
        envoyer_selection(ctx->id_attente, /*aucune=*/false);
    }
}

static void rappel_effacer(bool confirme, void *contexte)
{
    (void)contexte;
    if (confirme) {
        envoyer_selection(0, /*aucune=*/true);
    }
}

/* ------------------------------------------------------------------------
 * Rappels d'interaction
 * ---------------------------------------------------------------------- */

/* Indice dans ctx->liste de la bobine affichee a `emplacement` sur la page
 * courante, ou -1 si cet emplacement est vide. */
static int indice_affiche(const ecran_spoolman_ctx_t *ctx, uint8_t emplacement)
{
    uint32_t indice = (uint32_t)ctx->page * ECRAN_SPOOLMAN_PAGE_TAILLE + emplacement;
    if (indice >= ctx->liste.nb) {
        return -1;
    }
    return (int)indice;
}

static void bouton_bobine_cb(lv_event_t *e)
{
    const ecran_spoolman_emplacement_t *place = lv_event_get_user_data(e);
    if (place == NULL || place->ctx == NULL) {
        return;
    }
    ecran_spoolman_ctx_t *ctx = place->ctx;
    int indice = indice_affiche(ctx, place->emplacement);
    if (indice < 0) {
        return; /* rangee vide : rien a selectionner (elle est masquee de toute facon) */
    }
    const spoolman_bobine_t *bobine = &ctx->liste.bobines[indice];
    if (bobine->id == ctx->etat.id_actif) {
        return; /* deja active : un tap ne doit pas ouvrir une confirmation inutile */
    }

    ctx->id_attente = bobine->id;
    snprintf(ctx->nom_attente, sizeof(ctx->nom_attente), "%s",
             bobine->filament[0] != '\0' ? bobine->filament : "spool");

    char message[96];
    snprintf(message, sizeof(message), "Filament used will be counted against '%s'.",
             ctx->nom_attente);
    confirmation_ouvrir("Set as loaded spool?", message, "Set active", /*destructif=*/false,
                        rappel_selection, ctx);
}

static void bouton_effacer_cb(lv_event_t *e)
{
    (void)e;
    confirmation_ouvrir("Clear active spool?", "No filament use will be counted afterwards.",
                        "Clear", /*destructif=*/true, rappel_effacer, NULL);
}

static void bouton_rafraichir_cb(lv_event_t *e)
{
    (void)e;
    /* Declenche seulement : la reponse arrive de facon asynchrone sur la
       tache WS et atterrit dans le store, d'ou le prochain pompage la tirera
       (voir moonraker_ws_demander_bobines()). */
    moonraker_ws_demander_bobines();
}

static void bouton_precedent_cb(lv_event_t *e)
{
    ecran_spoolman_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx != NULL && ctx->page > 0) {
        ctx->page--;
        ctx->derniere_generation--; /* force le redessin au prochain pompage */
    }
}

static void bouton_suivant_cb(lv_event_t *e)
{
    ecran_spoolman_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL) {
        return;
    }
    uint8_t nb_pages = (uint8_t)((ctx->liste.nb + ECRAN_SPOOLMAN_PAGE_TAILLE - 1) /
                                 ECRAN_SPOOLMAN_PAGE_TAILLE);
    if (nb_pages == 0) {
        nb_pages = 1;
    }
    if (ctx->page + 1 < nb_pages) {
        ctx->page++;
        ctx->derniere_generation--;
    }
}

/* ------------------------------------------------------------------------
 * Rendu
 * ---------------------------------------------------------------------- */

static void bouton_definir_desactive(lv_obj_t *bouton, bool desactive)
{
    if (desactive) {
        lv_obj_add_state(bouton, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(bouton, LV_STATE_DISABLED);
    }
}

/* "1000 g" / "820 / 1000 g" / "? g" -- la bobine sans poids initial declare
 * affiche "?", JAMAIS 0 g (qui ferait croire a une bobine vide). */
static void formater_poids(const spoolman_bobine_t *bobine, char *sortie, size_t taille)
{
    if (!bobine->restant_connu) {
        snprintf(sortie, taille, "? g");
    } else if (bobine->total_g > 0.0f) {
        snprintf(sortie, taille, "%.0f / %.0f g", (double)bobine->restant_g,
                 (double)bobine->total_g);
    } else {
        snprintf(sortie, taille, "%.0f g", (double)bobine->restant_g);
    }
}

static void remplir_rangee(ecran_spoolman_ctx_t *ctx, uint8_t emplacement, int indice)
{
    lv_obj_t *bouton = ctx->boutons[emplacement];
    if (indice < 0) {
        lv_obj_add_flag(bouton, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(bouton, LV_OBJ_FLAG_HIDDEN);

    const spoolman_bobine_t *bobine = &ctx->liste.bobines[indice];
    bool actif = (bobine->id == ctx->etat.id_actif);

    lv_obj_set_style_bg_color(bouton,
                              lv_color_hex(actif ? COULEUR_BOUTON_ACTIF : COULEUR_BOUTON), 0);
    lv_obj_set_style_bg_color(ctx->pastilles[emplacement],
                              lv_color_hex(bobine->couleur_connue ? bobine->couleur
                                                                  : COULEUR_GRISE), 0);

    char poids[24];
    formater_poids(bobine, poids, sizeof(poids));

    /* Un seul libellé par rangée (même choix que ecran_fichiers.c) :
       "[v] Prusament - Galaxy Black (PETG)   820 / 1000 g". Le fabricant
       est omis s'il est inconnu plutôt que d'afficher un tiret orphelin. */
    char texte[128];
    if (bobine->fabricant[0] != '\0') {
        snprintf(texte, sizeof(texte), "%s%s - %s%s%s%s   %s",
                 actif ? LV_SYMBOL_OK " " : "",
                 bobine->fabricant,
                 bobine->filament[0] != '\0' ? bobine->filament : "(unnamed)",
                 bobine->matiere[0] != '\0' ? " (" : "",
                 bobine->matiere[0] != '\0' ? bobine->matiere : "",
                 bobine->matiere[0] != '\0' ? ")" : "",
                 poids);
    } else {
        snprintf(texte, sizeof(texte), "%s%s%s%s%s   %s",
                 actif ? LV_SYMBOL_OK " " : "",
                 bobine->filament[0] != '\0' ? bobine->filament : "(unnamed)",
                 bobine->matiere[0] != '\0' ? " (" : "",
                 bobine->matiere[0] != '\0' ? bobine->matiere : "",
                 bobine->matiere[0] != '\0' ? ")" : "",
                 poids);
    }
    lv_label_set_text(ctx->labels[emplacement], texte);
    bouton_definir_desactive(bouton, ctx->donnees_perimees);
}

static void redessiner(ecran_spoolman_ctx_t *ctx)
{
    /* En-tête : l'information la plus utile d'abord -- la liaison, puis la
       bobine active. Une liste vide affichée sans dire que Spoolman est
       injoignable serait un mensonge par omission. */
    char entete[96];
    if (ctx->etat.statut_connu && !ctx->etat.connecte) {
        snprintf(entete, sizeof(entete), "Spoolman offline");
    } else if (ctx->etat.id_actif == SPOOLMAN_AUCUNE_BOBINE) {
        snprintf(entete, sizeof(entete), "No active spool");
    } else {
        const char *nom = NULL;
        for (uint8_t i = 0; i < ctx->liste.nb; i++) {
            if (ctx->liste.bobines[i].id == ctx->etat.id_actif) {
                nom = ctx->liste.bobines[i].filament;
                break;
            }
        }
        if (nom != NULL && nom[0] != '\0') {
            snprintf(entete, sizeof(entete), "Active: %s", nom);
        } else {
            /* Bobine active absente de la liste (archivée depuis, ou liste
               tronquée) : dire son identifiant reste honnête. */
            snprintf(entete, sizeof(entete), "Active: spool #%ld", (long)ctx->etat.id_actif);
        }
    }
    if (ctx->liste.tronquee) {
        size_t n = strlen(entete);
        snprintf(entete + n, sizeof(entete) - n, "   (list truncated)");
    }
    lv_label_set_text(ctx->entete, entete);
    lv_obj_set_style_text_color(ctx->entete,
                                lv_color_hex(ctx->donnees_perimees ? COULEUR_GRISE
                                             : (ctx->etat.statut_connu && !ctx->etat.connecte)
                                                   ? COULEUR_AVERTISSEMENT
                                                   : COULEUR_TEXTE_SECONDAIRE), 0);

    uint8_t nb_pages = (uint8_t)((ctx->liste.nb + ECRAN_SPOOLMAN_PAGE_TAILLE - 1) /
                                 ECRAN_SPOOLMAN_PAGE_TAILLE);
    if (nb_pages == 0) {
        nb_pages = 1;
    }
    if (ctx->page >= nb_pages) {
        ctx->page = (uint8_t)(nb_pages - 1); /* la liste a rétréci sous nos pieds */
    }

    for (uint8_t emplacement = 0; emplacement < ECRAN_SPOOLMAN_PAGE_TAILLE; emplacement++) {
        remplir_rangee(ctx, emplacement, indice_affiche(ctx, emplacement));
    }

    /* "No spools" : distinguer « la liste est vide » de « on n'a jamais
       reçu de liste » -- le second cas n'est pas une absence de bobines. */
    if (ctx->liste.nb == 0) {
        lv_obj_clear_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(ctx->vide, ctx->liste.connue
                                         ? "No spools - add them in the Spoolman web UI"
                                         : "Loading spools...");
    } else {
        lv_obj_add_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN);
    }

    char page_texte[24];
    snprintf(page_texte, sizeof(page_texte), "%u/%u", (unsigned)(ctx->page + 1),
             (unsigned)nb_pages);
    lv_label_set_text(ctx->page_label, page_texte);
    bouton_definir_desactive(ctx->bouton_precedent, ctx->page == 0);
    bouton_definir_desactive(ctx->bouton_suivant, ctx->page + 1 >= nb_pages);
    bouton_definir_desactive(ctx->bouton_effacer,
                             ctx->donnees_perimees ||
                                 ctx->etat.id_actif == SPOOLMAN_AUCUNE_BOBINE);
    bouton_definir_desactive(ctx->bouton_rafraichir, ctx->donnees_perimees);
}

/* ------------------------------------------------------------------------
 * Construction
 * ---------------------------------------------------------------------- */

static lv_obj_t *bouton_creer(lv_obj_t *parent, const char *texte, lv_coord_t x, lv_coord_t y,
                              lv_coord_t largeur, lv_coord_t hauteur, const lv_font_t *police,
                              lv_text_align_t alignement)
{
    lv_obj_t *bouton = lv_button_create(parent);
    lv_obj_set_size(bouton, largeur, hauteur);
    lv_obj_set_pos(bouton, x, y);
    lv_obj_set_style_bg_color(bouton, lv_color_hex(COULEUR_BOUTON), 0);
    lv_color_t couleur_desactivee =
        lv_color_mix(lv_color_hex(COULEUR_BOUTON), lv_color_hex(COULEUR_FOND),
                     BOUTON_DESACTIVE_MELANGE);
    lv_obj_set_style_bg_color(bouton, couleur_desactivee, LV_STATE_DISABLED);
    lv_obj_set_style_border_width(bouton, 0, 0);
    lv_obj_set_style_shadow_width(bouton, 0, 0);
    lv_obj_set_style_radius(bouton, 8, 0);

    lv_obj_t *label = lv_label_create(bouton);
    lv_obj_set_style_text_font(label, police, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_GRISE), LV_STATE_DISABLED);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, alignement, 0);
    lv_obj_set_width(label, largeur - 12);
    /* Sans cet appel, le PREMIER lv_label_set_text() verrait une largeur de
       contenu pas encore résolue -- même piège, même correctif que
       ecran_fichiers.c/ecran_macros.c. */
    lv_obj_update_layout(label);
    lv_label_set_text(label, texte);
    lv_obj_center(label);
    return bouton;
}

static void ecran_spoolman_construire(lv_obj_t *parent, void *contexte)
{
    ecran_spoolman_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    ctx->page = 0;
    ctx->id_attente = SPOOLMAN_AUCUNE_BOBINE;
    ctx->nom_attente[0] = '\0';
    memset(&ctx->liste, 0, sizeof(ctx->liste));
    ctx->etat.id_actif = SPOOLMAN_AUCUNE_BOBINE;
    ctx->etat.connecte = false;
    ctx->etat.statut_connu = false;

    ctx->entete = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->entete, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->entete, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_obj_set_pos(ctx->entete, MARGE, ENTETE_Y);
    lv_obj_set_width(ctx->entete, BOUTON_LARGEUR);
    lv_label_set_text(ctx->entete, "");

    ctx->vide = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->vide, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->vide, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_obj_set_style_text_align(ctx->vide, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(ctx->vide, BOUTON_LARGEUR);
    lv_obj_set_pos(ctx->vide, MARGE, GRILLE_Y + 80);
    lv_label_set_text(ctx->vide, "");
    lv_obj_add_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN);

    for (uint8_t emplacement = 0; emplacement < ECRAN_SPOOLMAN_PAGE_TAILLE; emplacement++) {
        lv_coord_t y = (lv_coord_t)(GRILLE_Y + emplacement * (BOUTON_HAUTEUR + BOUTON_ECART_Y));
        ctx->emplacements[emplacement].ctx = ctx;
        ctx->emplacements[emplacement].emplacement = emplacement;

        lv_obj_t *bouton = bouton_creer(parent, "", MARGE, y, BOUTON_LARGEUR, BOUTON_HAUTEUR,
                                        &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
        ctx->boutons[emplacement] = bouton;
        ctx->labels[emplacement] = lv_obj_get_child(bouton, 0);
        /* Le libellé laisse la place à la pastille : décalé à droite et
           rétréci d'autant, sinon le texte passerait dessous. */
        lv_obj_set_width(ctx->labels[emplacement], BOUTON_LARGEUR - LIBELLE_X - 12);
        lv_obj_align(ctx->labels[emplacement], LV_ALIGN_LEFT_MID, LIBELLE_X, 0);

        lv_obj_t *pastille = lv_obj_create(bouton);
        lv_obj_remove_style_all(pastille);
        lv_obj_set_size(pastille, PASTILLE_TAILLE, PASTILLE_TAILLE);
        lv_obj_align(pastille, LV_ALIGN_LEFT_MID, PASTILLE_X, 0);
        lv_obj_set_style_bg_opa(pastille, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(pastille, lv_color_hex(COULEUR_GRISE), 0);
        lv_obj_set_style_radius(pastille, 4, 0);
        ctx->pastilles[emplacement] = pastille;

        lv_obj_add_event_cb(bouton, bouton_bobine_cb, LV_EVENT_CLICKED,
                            &ctx->emplacements[emplacement]);
        lv_obj_add_flag(bouton, LV_OBJ_FLAG_HIDDEN);
    }

    /* Pied de page : pagination à gauche, actions à droite. */
    ctx->bouton_precedent = bouton_creer(parent, LV_SYMBOL_LEFT, MARGE, PIED_Y,
                                         BOUTON_PAGE_LARGEUR, PIED_HAUTEUR,
                                         &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_event_cb(ctx->bouton_precedent, bouton_precedent_cb, LV_EVENT_CLICKED, ctx);

    ctx->page_label = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->page_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->page_label, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_obj_set_style_text_align(ctx->page_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(ctx->page_label, 70);
    lv_obj_set_pos(ctx->page_label, MARGE + BOUTON_PAGE_LARGEUR + 8, PIED_Y + 12);
    lv_label_set_text(ctx->page_label, "1/1");

    ctx->bouton_suivant = bouton_creer(parent, LV_SYMBOL_RIGHT,
                                       MARGE + BOUTON_PAGE_LARGEUR + 86, PIED_Y,
                                       BOUTON_PAGE_LARGEUR, PIED_HAUTEUR,
                                       &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_event_cb(ctx->bouton_suivant, bouton_suivant_cb, LV_EVENT_CLICKED, ctx);

    lv_coord_t effacer_x = (lv_coord_t)(LARGEUR_CONTENU - MARGE - BOUTON_PIED_LARGEUR);
    lv_coord_t rafraichir_x = (lv_coord_t)(effacer_x - BOUTON_PIED_LARGEUR - 12);
    ctx->bouton_rafraichir = bouton_creer(parent, "Refresh", rafraichir_x, PIED_Y,
                                          BOUTON_PIED_LARGEUR, PIED_HAUTEUR,
                                          &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_event_cb(ctx->bouton_rafraichir, bouton_rafraichir_cb, LV_EVENT_CLICKED, NULL);

    ctx->bouton_effacer = bouton_creer(parent, "Clear active", effacer_x, PIED_Y,
                                       BOUTON_PIED_LARGEUR, PIED_HAUTEUR,
                                       &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_event_cb(ctx->bouton_effacer, bouton_effacer_cb, LV_EVENT_CLICKED, NULL);

    /* Génération échantillonnée AVANT la lecture (même leçon que
       ecran_bed_mesh.c, revue L5) : un dépôt WS qui tombe pendant cette
       lecture doit laisser une génération plus récente, pour que le prochain
       pompage redessine. */
    uint32_t generation = spoolman_generation();
    spoolman_lire_liste(&ctx->liste);
    spoolman_lire_etat(&ctx->etat);
    redessiner(ctx);
    ctx->derniere_generation = generation;
}

static void ecran_spoolman_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    /* L'écran ne lit RIEN de etat_klipper_t : sa source est le store dédié
       spoolman_store.h (même choix documenté que ecran_usb.c/ecran_bed_mesh.c).
       `donnees_perimees` reste utile : il grise l'interface quand la liaison
       Moonraker est morte, auquel cas aucune commande ne partirait. */
    (void)etat;
    ecran_spoolman_ctx_t *ctx = contexte;
    if (ctx == NULL) {
        return;
    }
    uint32_t generation = spoolman_generation();
    if (generation != ctx->derniere_generation || donnees_perimees != ctx->donnees_perimees) {
        ctx->donnees_perimees = donnees_perimees;
        spoolman_lire_liste(&ctx->liste);
        spoolman_lire_etat(&ctx->etat);
        redessiner(ctx);
        ctx->derniere_generation = generation;
    }
}

const ecran_desc_t ECRAN_SPOOLMAN = {
    .id = "spoolman",
    .titre = "Spoolman",
    .taille_contexte = sizeof(ecran_spoolman_ctx_t),
    .construire = ecran_spoolman_construire,
    .mettre_a_jour = ecran_spoolman_mettre_a_jour,
    .detruire = NULL,
};
