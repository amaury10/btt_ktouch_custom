/* Implémentation : voir ecran_usb.h pour le contrat.
 *
 * Mise en page (742x436, dans le conteneur de navigation à droite du rail
 * persistant, sous la barre d'état construite par habillage.c) : une rangée
 * de statut/progression tout en haut (STATUT_Y, partagée par ctx->statut_texte
 * et ctx->progression -- un seul visible à la fois, voir mettre_a_jour_statut()
 * plus bas), puis une COLONNE UNIQUE de boutons pleine largeur (5 fichiers par
 * page, mêmes dimensions verticales que ecran_fichiers.c/ecran_macros.c), une
 * ligne de pagination en bas ("< Page X/Y >"). Budget vertical serré (voir
 * les _Static_assert plus bas) : GRILLE_Y ne peut descendre que de 4px de
 * plus que ecran_fichiers.c (36 -> 40) pour laisser la place à la rangée de
 * statut sans faire déborder la pagination sous le bandeau de notification --
 * c'est ce qui a fait fusionner statut_texte/progression dans la MÊME zone
 * plutôt que deux rangées distinctes. */
#include "ecran_usb.h"

#include <stdio.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#include "confirmation.h"
#include "habillage.h" /* habillage_notifier() -- refus tardif d'un second upload */
#include "usb_fichiers.h"
#include "usb_scan.h" /* usb_scan_demarrage_paresseux() -- fix RAM interne, voir son commentaire de tete */
#include "usb_upload_http.h"

#define LARGEUR_CONTENU 742 /* 800 - RAIL_LARGEUR (58), voir habillage.c */
#define HAUTEUR_CONTENU 436

#define MARGE 20

#define STATUT_Y       8
#define STATUT_HAUTEUR 26

#define GRILLE_Y      (STATUT_Y + STATUT_HAUTEUR + 6)
#define BOUTON_ECART_Y 6
#define BOUTON_LARGEUR (LARGEUR_CONTENU - 2 * MARGE)
#define BOUTON_HAUTEUR      52
#define GRILLE_BAS (GRILLE_Y + ECRAN_USB_PAGE_TAILLE * BOUTON_HAUTEUR + \
                    (ECRAN_USB_PAGE_TAILLE - 1) * BOUTON_ECART_Y)

#define PAGINATION_HAUTEUR 44
#define PAGINATION_Y (GRILLE_BAS + 8)
#define BOUTON_PAGE_LARGEUR 60

/* Même convention que ecran_fichiers.c/ecran_macros.c : bande couverte par le
 * bandeau de notification de habillage.c, en coordonnées ABSOLUES d'écran. */
#define BARRE_HAUTEUR_ECRAN   44
#define HAUTEUR_ECRAN_TOTALE 480
#define BANDEAU_HAUTEUR_ECRAN 60
#define BANDEAU_Y_ECRAN (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF
#define COULEUR_SUCCES           0x2ECC71 /* même vert que la pastille de liaison EN_LIGNE, habillage.c */
#define COULEUR_ECHEC            0xE74C3C /* même rouge que la pastille de liaison HORS_LIGNE, habillage.c */
#define COULEUR_AVERTISSEMENT    0xF1C40F /* même ambre que ecran_fichiers.c/habillage.c */

/* Même raisonnement que BOUTON_DESACTIVE_MELANGE dans ecran_fichiers.c. */
#define BOUTON_DESACTIVE_MELANGE 90

_Static_assert(MARGE + BOUTON_LARGEUR + MARGE <= LARGEUR_CONTENU,
               "le bouton pleine largeur deborde de la largeur du contenu");
_Static_assert(STATUT_Y + STATUT_HAUTEUR <= GRILLE_Y,
               "la rangee de statut/progression chevauche la premiere rangee de la grille");
_Static_assert(GRILLE_BAS <= PAGINATION_Y,
               "la colonne de fichiers chevauche la ligne de pagination");
_Static_assert(PAGINATION_Y + PAGINATION_HAUTEUR <= HAUTEUR_CONTENU,
               "la ligne de pagination deborde de la hauteur du contenu");
_Static_assert(BARRE_HAUTEUR_ECRAN + PAGINATION_Y + PAGINATION_HAUTEUR <= BANDEAU_Y_ECRAN,
               "la ligne de pagination chevauche la bande du bandeau de notification de l'habillage");

/* Même paire de styles locaux (DEFAULT + DISABLED, bouton ET label) que
 * bouton_creer() dans ecran_fichiers.c. */
static lv_obj_t *bouton_creer(lv_obj_t *parent, const char *texte, lv_coord_t x, lv_coord_t y,
                               lv_coord_t largeur, lv_coord_t hauteur, const lv_font_t *police,
                               lv_text_align_t alignement_texte)
{
    lv_obj_t *bouton = lv_button_create(parent);
    lv_obj_set_size(bouton, largeur, hauteur);
    lv_obj_set_pos(bouton, x, y);
    lv_obj_set_style_bg_color(bouton, lv_color_hex(COULEUR_BOUTON), 0);
    lv_color_t couleur_desactivee =
        lv_color_mix(lv_color_hex(COULEUR_BOUTON), lv_color_hex(COULEUR_FOND), BOUTON_DESACTIVE_MELANGE);
    lv_obj_set_style_bg_color(bouton, couleur_desactivee, LV_STATE_DISABLED);
    lv_obj_set_style_border_width(bouton, 0, 0);
    lv_obj_set_style_shadow_width(bouton, 0, 0);
    lv_obj_set_style_radius(bouton, 8, 0);

    lv_obj_t *label = lv_label_create(bouton);
    lv_obj_set_style_text_font(label, police, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_GRISE), LV_STATE_DISABLED);
    /* Points de suspension plutot que deborder sur un chemin USB long --
     * meme technique que ecran_fichiers.c. */
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, alignement_texte, 0);
    lv_obj_set_width(label, largeur - 12);
    lv_obj_update_layout(label);
    lv_label_set_text(label, texte);
    lv_obj_center(label);

    return bouton;
}

static void bouton_definir_desactive(lv_obj_t *bouton, bool desactive)
{
    lv_obj_t *label = lv_obj_get_child(bouton, 0);
    if (desactive) {
        lv_obj_add_state(bouton, LV_STATE_DISABLED);
        if (label != NULL) {
            lv_obj_add_state(label, LV_STATE_DISABLED);
        }
    } else {
        lv_obj_remove_state(bouton, LV_STATE_DISABLED);
        if (label != NULL) {
            lv_obj_remove_state(label, LV_STATE_DISABLED);
        }
    }
}

/* min(1, ceil(nb_fichiers / ECRAN_USB_PAGE_TAILLE)) -- meme raisonnement que
 * nb_pages() dans ecran_fichiers.c. */
static uint8_t nb_pages(uint8_t nb_fichiers)
{
    if (nb_fichiers == 0) {
        return 1;
    }
    return (uint8_t)((nb_fichiers + ECRAN_USB_PAGE_TAILLE - 1) / ECRAN_USB_PAGE_TAILLE);
}

/* Recalcule entierement ce que la page courante doit montrer -- meme
 * discipline (systematique, jamais incrementale) que afficher_page() dans
 * ecran_fichiers.c. `en_cours` desactive TOUS les boutons (fichiers ET
 * pagination) : un second upload ne doit jamais pouvoir demarrer tant que le
 * premier n'est pas resolu (succes ou echec), meme raison que le singleton
 * de usb_upload_http_demarrer(). */
static void afficher_page(ecran_usb_ctx_t *ctx, bool en_cours)
{
    uint8_t pages = nb_pages(ctx->nb_fichiers);
    if (ctx->page >= pages) {
        ctx->page = (uint8_t)(pages - 1);
    }

    bool vide = (!ctx->monte) || (ctx->nb_fichiers == 0);
    if (vide) {
        /* Pendant un listage (1-2 s possibles sur un gros dossier), le store
           n'a pas encore publie : sans le cas scan_en_cours, cet ecran
           affichait "Insert a USB key" cle branchee et montee -- l'illusion
           qui a coute deux sessions de debogage (2026-08-14), voir
           usb_fichiers.h. */
        lv_label_set_text(ctx->vide, ctx->scan_en_cours ? "Reading USB key..."
                                     : ctx->monte       ? "Empty folder (no .gcode)"
                                                        : "Insert a USB key");
        lv_obj_clear_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN);
    }

    for (uint8_t emplacement = 0; emplacement < ECRAN_USB_PAGE_TAILLE; emplacement++) {
        uint16_t indice = (uint16_t)ctx->page * ECRAN_USB_PAGE_TAILLE + emplacement;
        if (!vide && indice < ctx->nb_fichiers) {
            /* Libelle = NOM seul (le chemin courant vit dans la rangee de
               statut) ; dossiers prefixes du symbole LVGL. L'entree ".."
               porte le chemin du PARENT : son "nom" serait celui du parent,
               d'ou le libelle special -- decide par ctx->a_remontee, pose a
               l'injection (jamais re-derive ici, revue 2026-08-15 L9). */
            bool est_remontee = (indice == 0 && ctx->a_remontee);
            char libelle[USB_FICHIER_CHEMIN_MAX + 8];
            if (est_remontee) {
                snprintf(libelle, sizeof(libelle), LV_SYMBOL_DIRECTORY " ..");
            } else if (ctx->dossiers_copie[indice]) {
                snprintf(libelle, sizeof(libelle), LV_SYMBOL_DIRECTORY " %s",
                         usb_chemin_nom(ctx->chemins_copie[indice]));
            } else {
                snprintf(libelle, sizeof(libelle), "%s", usb_chemin_nom(ctx->chemins_copie[indice]));
            }
            lv_label_set_text(ctx->labels[emplacement], libelle);
            lv_obj_clear_flag(ctx->boutons[emplacement], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ctx->boutons[emplacement], LV_OBJ_FLAG_HIDDEN);
        }
        bouton_definir_desactive(ctx->boutons[emplacement], en_cours);
    }

    bool page_unique = (pages <= 1) || vide;
    if (page_unique) {
        lv_obj_add_flag(ctx->bouton_precedent, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->bouton_suivant, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->page_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(ctx->bouton_precedent, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ctx->bouton_suivant, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ctx->page_label, LV_OBJ_FLAG_HIDDEN);
        char texte[16];
        snprintf(texte, sizeof(texte), "Page %u/%u", (unsigned)(ctx->page + 1), (unsigned)pages);
        lv_label_set_text(ctx->page_label, texte);
    }
    bouton_definir_desactive(ctx->bouton_precedent, en_cours || ctx->page == 0);
    bouton_definir_desactive(ctx->bouton_suivant, en_cours || (uint8_t)(ctx->page + 1) >= pages);
}

/* Rangee de statut/progression -- reevaluee a CHAQUE mettre_a_jour() (pas
 * seulement quand la generation du store de fichiers change, contrairement a
 * la liste elle-meme) : l'etat d'upload evolue en continu pendant un envoi,
 * sans jamais toucher usb_fichiers_generation(). Rend vrai si un upload est
 * EN_COURS (consomme par l'appelant pour desactiver la grille/la pagination
 * -- une seule lecture de usb_upload_http_lire() par appel, jamais une
 * seconde via usb_upload_http_en_cours() qui redemanderait le meme verrou). */
static bool mettre_a_jour_statut(ecran_usb_ctx_t *ctx)
{
    usb_upload_http_progression_t prog;
    usb_upload_http_lire(&prog);

    bool en_cours = (prog.etat == USB_UPLOAD_HTTP_EN_COURS);

    if (en_cours) {
        lv_obj_add_flag(ctx->statut_texte, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ctx->progression.racine, LV_OBJ_FLAG_HIDDEN);
        float fraction = (prog.total > 0) ? ((float)prog.envoyes / (float)prog.total) : 0.0f;
        progression_definir(&ctx->progression, fraction);
        return true;
    }

    lv_obj_add_flag(ctx->progression.racine, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ctx->statut_texte, LV_OBJ_FLAG_HIDDEN);

    char texte[128];
    uint32_t couleur = COULEUR_TEXTE_SECONDAIRE;
    if (prog.etat == USB_UPLOAD_HTTP_SUCCES) {
        snprintf(texte, sizeof(texte), "Upload OK, printing started");
        couleur = COULEUR_SUCCES;
    } else if (prog.etat == USB_UPLOAD_HTTP_ECHEC) {
        snprintf(texte, sizeof(texte), "Upload failed: %s", prog.message);
        couleur = COULEUR_ECHEC;
    } else if (ctx->scan_en_cours) {
        /* Listage en cours avec une liste encore affichee (navigation depuis
           un dossier non vide -- revue 2026-08-15 L4) : le libelle central
           "Reading..." de la grille ne s'affiche que liste vide, la rangee
           de statut porte donc l'etat occupe ici. */
        snprintf(texte, sizeof(texte), "Reading...");
        couleur = COULEUR_TEXTE_SECONDAIRE;
    } else if (ctx->monte && ctx->chemin_courant[0] != '\0') {
        /* Explorateur : la rangee de statut situe TOUJOURS l'utilisateur
           (repertoire courant) -- les libelles de la grille ne portent que
           le nom seul. La troncature s'y COMPOSE au lieu de masquer le
           chemin (revue 2026-08-15 L5 : c'est dans les dossiers bondes que
           savoir ou l'on est compte le plus). %.100s : borne dure sous la
           taille du tampon, un chemin profond est coupe net. */
        if (ctx->tronques) {
            snprintf(texte, sizeof(texte), "%.100s (folder truncated)", ctx->chemin_courant);
            couleur = COULEUR_AVERTISSEMENT;
        } else {
            snprintf(texte, sizeof(texte), "%.120s", ctx->chemin_courant);
            couleur = COULEUR_GRISE;
        }
    } else {
        texte[0] = '\0';
    }
    lv_label_set_text(ctx->statut_texte, texte);
    lv_obj_set_style_text_color(ctx->statut_texte, lv_color_hex(couleur), 0);
    return false;
}

/* Rappel de la confirmation d'envoi -- ne demarre l'upload que si
 * l'utilisateur a REELLEMENT confirme (voir confirmation.h). Un declin
 * (confirme == false) ne fait rien. `ctx->chemin_attente`/`taille_attente`
 * ont ete remplis par bouton_fichier_cb() juste avant l'ouverture du
 * dialogue -- toujours ce que le tap a REELLEMENT designe, jamais une
 * saisie manuelle. */
static void rappel_confirmer_envoi(bool confirme, void *contexte)
{
    ecran_usb_ctx_t *ctx = contexte;
    if (!confirme || ctx == NULL) {
        return;
    }
    if (!usb_upload_http_demarrer(ctx->chemin_attente, ctx->taille_attente)) {
        /* Refus tardif (course rare : deux confirmations resolues avant que
           mettre_a_jour() n'ait eu l'occasion de desactiver les boutons) --
           habillage_notifier() est le canal generique de ce genre de refus
           dans ce depot, jamais une boite d'erreur reseau propre a l'ecran
           (voir ecran.h, contrat de donnees_perimees pour le meme principe). */
        habillage_notifier("An upload is already in progress", true);
    }
}

/* Tap sur un fichier : ouvre une confirmation ("Send and print?", le nom
 * affiche dans le message) -- JAMAIS d'action directe, meme raison que
 * bouton_fichier_cb() dans ecran_fichiers.c : demarrer une impression ne
 * doit jamais partir d'un simple effleurement. Le chemin est relu ICI,
 * depuis `ctx->chemins_copie`, jamais un nom mis en cache ailleurs. */
static void bouton_fichier_cb(lv_event_t *e)
{
    ecran_usb_emplacement_t *info = lv_event_get_user_data(e);
    if (info == NULL || info->ctx == NULL) {
        return;
    }
    ecran_usb_ctx_t *ctx = info->ctx;
    if (usb_upload_http_en_cours()) {
        return; /* garde defensive : LV_STATE_DISABLED bloque deja un appui reel */
    }
    uint16_t indice = (uint16_t)ctx->page * ECRAN_USB_PAGE_TAILLE + info->emplacement;
    if (indice >= ctx->nb_fichiers) {
        return; /* bouton cache/vide sur cette page ; ne devrait jamais arriver via un vrai doigt */
    }

    /* Dossier (".." comprise) : NAVIGUER -- demande asynchrone, le store
       publiera le nouveau repertoire (generation) et mettre_a_jour() suivra.
       Jamais de confirmation pour une navigation, et la grille n'est PAS
       desactivee pendant un listage : deux taps rapprochés = derniere
       demande gagne (contrat usb_scan_demander()). */
    if (ctx->dossiers_copie[indice]) {
        usb_scan_demander(ctx->chemins_copie[indice]);
        return;
    }

    /* Copie bornee manuelle, meme piege/correctif que la boucle de
     * mettre_a_jour() plus bas (chemin_attente et chemins_copie[indice] font
     * la meme taille fixe USB_FICHIER_CHEMIN_MAX). */
    {
        const char *chemin_source = ctx->chemins_copie[indice];
        size_t longueur_copie = strlen(chemin_source);
        if (longueur_copie >= sizeof(ctx->chemin_attente)) {
            longueur_copie = sizeof(ctx->chemin_attente) - 1;
        }
        memcpy(ctx->chemin_attente, chemin_source, longueur_copie);
        ctx->chemin_attente[longueur_copie] = '\0';
    }
    ctx->taille_attente = ctx->tailles_copie[indice];

    /* Meme helper que les libelles de la grille (revue 2026-08-15, L10) :
       une seule regle de nom, le dialogue affiche EXACTEMENT ce que la
       rangee tapee affichait. */
    const char *nom = usb_chemin_nom(ctx->chemin_attente);

    confirmation_ouvrir("Send and print?", nom, "Send", /*destructif=*/false, rappel_confirmer_envoi, ctx);
}

static void bouton_precedent_cb(lv_event_t *e)
{
    ecran_usb_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL || usb_upload_http_en_cours() || ctx->page == 0) {
        return;
    }
    ctx->page--;
    afficher_page(ctx, false);
}

static void bouton_suivant_cb(lv_event_t *e)
{
    ecran_usb_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL || usb_upload_http_en_cours()) {
        return;
    }
    if ((uint8_t)(ctx->page + 1) >= nb_pages(ctx->nb_fichiers)) {
        return;
    }
    ctx->page++;
    afficher_page(ctx, false);
}

static void ecran_usb_construire(lv_obj_t *parent, void *contexte)
{
    ecran_usb_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    /* Démarrage paresseux du sous-système USB (fix RAM interne, voir le
     * commentaire de tête de usb_scan.h) : la toute PREMIÈRE ouverture de
     * cet écran est ce qui déclenche pt_usb_start() + les callbacks
     * mount/unmount, plus le boot -- idempotent (no-op après le premier
     * appel réel, et no-op complet côté host-test). Appelé ICI plutôt que
     * dans mettre_a_jour() : construire() ne tourne qu'une fois par
     * ouverture d'écran, pas à chaque pompage. */
    usb_scan_demarrage_paresseux();

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    ctx->statut_texte = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->statut_texte, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->statut_texte, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(ctx->statut_texte, "");
    lv_obj_set_pos(ctx->statut_texte, MARGE, STATUT_Y);
    lv_obj_set_size(ctx->statut_texte, LARGEUR_CONTENU - 2 * MARGE, STATUT_HAUTEUR);

    progression_creer(&ctx->progression, parent);
    lv_obj_set_size(ctx->progression.racine, LARGEUR_CONTENU - 2 * MARGE, STATUT_HAUTEUR);
    lv_obj_set_pos(ctx->progression.racine, MARGE, STATUT_Y);
    lv_obj_add_flag(ctx->progression.racine, LV_OBJ_FLAG_HIDDEN); /* aucun upload au premier affichage */

    /* "Insert a USB key" / "No .gcode files" -- jamais un ecran muet, meme
     * regle que "No files" dans ecran_fichiers.c. */
    ctx->vide = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->vide, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->vide, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_obj_set_style_text_align(ctx->vide, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(ctx->vide, "Insert a USB key");
    lv_obj_set_pos(ctx->vide, MARGE, GRILLE_Y);
    lv_obj_set_size(ctx->vide, LARGEUR_CONTENU - 2 * MARGE, PAGINATION_Y - GRILLE_Y);
    lv_obj_add_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN);

    for (uint8_t emplacement = 0; emplacement < ECRAN_USB_PAGE_TAILLE; emplacement++) {
        lv_coord_t x = MARGE;
        lv_coord_t y = GRILLE_Y + emplacement * (BOUTON_HAUTEUR + BOUTON_ECART_Y);

        ctx->boutons[emplacement] =
            bouton_creer(parent, "", x, y, BOUTON_LARGEUR, BOUTON_HAUTEUR, &lv_font_montserrat_14,
                         LV_TEXT_ALIGN_LEFT);
        ctx->labels[emplacement] = lv_obj_get_child(ctx->boutons[emplacement], 0);
        lv_obj_add_flag(ctx->boutons[emplacement], LV_OBJ_FLAG_HIDDEN);

        ctx->emplacements[emplacement].ctx = ctx;
        ctx->emplacements[emplacement].emplacement = emplacement;
        lv_obj_add_event_cb(ctx->boutons[emplacement], bouton_fichier_cb, LV_EVENT_CLICKED,
                             &ctx->emplacements[emplacement]);
    }

    lv_coord_t pagination_centre_x = LARGEUR_CONTENU / 2;
    ctx->bouton_precedent = bouton_creer(parent, "<", pagination_centre_x - BOUTON_PAGE_LARGEUR - 90,
                                          PAGINATION_Y, BOUTON_PAGE_LARGEUR, PAGINATION_HAUTEUR,
                                          &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER);
    lv_obj_add_event_cb(ctx->bouton_precedent, bouton_precedent_cb, LV_EVENT_CLICKED, ctx);

    ctx->bouton_suivant = bouton_creer(parent, ">", pagination_centre_x + 90, PAGINATION_Y,
                                        BOUTON_PAGE_LARGEUR, PAGINATION_HAUTEUR, &lv_font_montserrat_20,
                                        LV_TEXT_ALIGN_CENTER);
    lv_obj_add_event_cb(ctx->bouton_suivant, bouton_suivant_cb, LV_EVENT_CLICKED, ctx);

    ctx->page_label = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->page_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->page_label, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_text(ctx->page_label, "");
    lv_obj_set_size(ctx->page_label, 160, PAGINATION_HAUTEUR);
    lv_obj_set_pos(ctx->page_label, pagination_centre_x - 80, PAGINATION_Y);
    lv_obj_set_style_text_align(ctx->page_label, LV_TEXT_ALIGN_CENTER, 0);

    ctx->nb_fichiers = 0;
    ctx->tronques = false;
    ctx->monte = false;
    ctx->scan_en_cours = false;
    ctx->chemin_courant[0] = '\0';
    ctx->a_remontee = false;
    ctx->page = 0;
    ctx->derniere_generation = 0;
    ctx->chemin_attente[0] = '\0';
    ctx->taille_attente = 0;

    /* Premier rendu coherent avant le tout premier mettre_a_jour() -- meme
       raison que le "premier habillage_pomper()" de app_main.c : sans cela,
       l'ecran resterait sans "vide"/statut jusqu'a un evenement externe.
       `usb_fichiers_generation()` vaut potentiellement deja > 0 ici (scan
       tourne des le montage, avant meme que cet ecran soit ouvert) -- forcer
       `derniere_generation = 0` juste au-dessus garantit que le tout premier
       mettre_a_jour() recopie bien le store courant plutot que de le
       supposer deja a jour. */
    bool en_cours = mettre_a_jour_statut(ctx);
    afficher_page(ctx, en_cours);
}

static void ecran_usb_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    /* Cet ecran ne lit AUCUN champ de etat_klipper_t (pont USB -> Moonraker
       independant du polling Klipper habituel) -- ni `etat` ni
       `donnees_perimees` ne sont utilises, meme choix documente que les
       ecrans purement locaux du reste de ce depot (voir ecran.h). */
    (void)etat;
    (void)donnees_perimees;

    ecran_usb_ctx_t *ctx = contexte;
    if (ctx == NULL) {
        return;
    }

    uint32_t generation = usb_fichiers_generation();
    if (generation != ctx->derniere_generation) {
        /* JAMAIS un usb_fichiers_t local (revue du 2026-08-15, L1) : ~8,8 Ko
           depuis la borne a 64 entrees, sur la pile LVGL de 12 Ko deja
           chargee de plusieurs cadres -- la classe exacte du bug historique
           etat_klipper_t vs piles (voir la memoire du projet), invisible en
           host-test. Scratch PSRAM persistant : cet ecran ne tourne que sur
           la tache LVGL, une seule instance a la fois, pas de concurrence.
           PSRAM epuisee : on saute la mise a jour (retente au prochain
           pompage), jamais un repli pile/RAM interne. */
#ifdef ESP_PLATFORM
        static usb_fichiers_t *instantane;
        if (instantane == NULL) {
            instantane = (usb_fichiers_t *)heap_caps_malloc(sizeof(*instantane), MALLOC_CAP_SPIRAM);
            if (instantane == NULL) {
                return;
            }
        }
#else
        static usb_fichiers_t instantane_hote;
        usb_fichiers_t *const instantane = &instantane_hote;
#endif
        usb_fichiers_t *fics = instantane;
        usb_fichiers_lire(fics);
        fics->chemin_courant[USB_FICHIER_CHEMIN_MAX - 1] = '\0'; /* defensif : POD a champs fixes */

        /* Changement de repertoire : repartir en page 0 -- l'utilisateur
           vient de naviguer, la pagination de l'ancien dossier n'a plus de
           sens. */
        if (strcmp(fics->chemin_courant, ctx->chemin_courant) != 0) {
            ctx->page = 0;
            memcpy(ctx->chemin_courant, fics->chemin_courant, USB_FICHIER_CHEMIN_MAX);
        }

        ctx->nb_fichiers = 0;
        ctx->a_remontee = false;

        /* Entree ".." en tete, HORS racine : remonter est une entree de la
           grille comme une autre (choix utilisateur valide, voir la spec).
           Son chemin est le PARENT du repertoire courant (usb_chemin_parent(),
           pur et teste host -- revue 2026-08-15 L9, la version inline n'etait
           exercee qu'au tap reel). `a_remontee` est LA source de verite de
           cette injection, relue par l'affichage. */
        if (fics->monte && strcmp(fics->chemin_courant, USB_RACINE) != 0 &&
            fics->chemin_courant[0] != '\0') {
            usb_chemin_parent(ctx->chemins_copie[0], sizeof(ctx->chemins_copie[0]),
                              fics->chemin_courant);
            ctx->tailles_copie[0] = 0;
            ctx->dossiers_copie[0] = true;
            ctx->a_remontee = true;
            ctx->nb_fichiers = 1;
        }

        uint8_t nb_source = fics->nb;
        if (nb_source > USB_FICHIERS_MAX) {
            nb_source = USB_FICHIERS_MAX; /* garde defensive, ne devrait jamais arriver */
        }
        for (uint8_t i = 0; i < nb_source; i++) {
            char chemin_borne[USB_FICHIER_CHEMIN_MAX];
            memcpy(chemin_borne, fics->fichiers[i].chemin, USB_FICHIER_CHEMIN_MAX);
            chemin_borne[USB_FICHIER_CHEMIN_MAX - 1] = '\0'; /* defensif : POD a champs fixes, voir klipper_fichiers.h */
            if (chemin_borne[0] == '\0') {
                continue; /* emplacement vide */
            }
            /* Copie bornee manuelle, PAS snprintf(dst, N, "%s", src) -- `src`
             * (chemin_borne) et `dst` (ctx->chemins_copie[i]) font la MEME
             * taille fixe (USB_FICHIER_CHEMIN_MAX), ce que gcc ne peut pas
             * prouver sans troncature possible a la compilation ; meme piege,
             * meme correctif que ecran_fichiers.c/ecran_macros.c (voir leur
             * commentaire sur -Werror=format-truncation). */
            {
                size_t longueur_copie = strlen(chemin_borne);
                if (longueur_copie >= sizeof(ctx->chemins_copie[0])) {
                    longueur_copie = sizeof(ctx->chemins_copie[0]) - 1;
                }
                memcpy(ctx->chemins_copie[ctx->nb_fichiers], chemin_borne, longueur_copie);
                ctx->chemins_copie[ctx->nb_fichiers][longueur_copie] = '\0';
            }
            ctx->tailles_copie[ctx->nb_fichiers] = fics->fichiers[i].taille;
            ctx->dossiers_copie[ctx->nb_fichiers] = fics->fichiers[i].est_dossier;
            ctx->nb_fichiers++;
        }
        ctx->tronques = fics->tronques;
        ctx->monte = fics->monte;
        ctx->scan_en_cours = fics->scan_en_cours;
        ctx->derniere_generation = generation;
    }

    bool en_cours = mettre_a_jour_statut(ctx);
    afficher_page(ctx, en_cours);
}

const ecran_desc_t ECRAN_USB = {
    .id = "usb",
    .titre = "USB",
    .taille_contexte = sizeof(ecran_usb_ctx_t),
    .construire = ecran_usb_construire,
    .mettre_a_jour = ecran_usb_mettre_a_jour,
    .detruire = NULL,
};
