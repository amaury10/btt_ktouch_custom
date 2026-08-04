/* Implementation : voir ecran_power.h pour le contrat.
 *
 * Mise en page (742x436, dans le conteneur de navigation a droite du rail
 * persistant, sous la barre d'etat construite par habillage.c) : une seule
 * colonne pleine largeur, SCROLLABLE (pas de pagination, voir le commentaire
 * de tete du .h), une ligne par prise -- nom a gauche, etat ON/OFF colore a
 * droite. Ossature reprise de `zone_chauffants` (ecran_accueil_hub.c, meme
 * idiome de conteneur scrollable a pool fixe masque/rempli) pour le
 * defilement, et de ecran_fichiers.c pour la ligne cliquable + confirmation
 * + copie defensive avant tap. */
#include "ecran_power.h"

#include <stdio.h>
#include <string.h>

#include "backend.h"
#include "confirmation.h"
#include "source_etat.h"

#define LARGEUR_CONTENU 742 /* 800 - RAIL_LARGEUR (58), voir habillage.c */
#define HAUTEUR_CONTENU 436

#define MARGE 20

/* --- Bande couverte par le bandeau de notification de habillage.c, en
 * coordonnees ABSOLUES d'ecran -- meme convention que ecran_fichiers.c. --- */
#define BARRE_HAUTEUR_ECRAN   44
#define HAUTEUR_ECRAN_TOTALE 480
#define BANDEAU_HAUTEUR_ECRAN 60
#define BANDEAU_Y_ECRAN (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)

/* Zone scrollable : demarre a MARGE du haut du contenu, s'arrete avant la
 * bande du bandeau (assert plus bas, coordonnees ABSOLUES) -- POWER_DEVICES_MAX
 * (8) lignes de LIGNE_HAUTEUR+LIGNE_ECART_Y depassent cette hauteur visible,
 * c'est justement ce que `zone` scrolle. */
#define ZONE_Y       16
#define ZONE_HAUTEUR 352

#define LIGNE_HAUTEUR      52
#define LIGNE_ECART_Y       8
#define LIGNE_LARGEUR (LARGEUR_CONTENU - 2 * MARGE)

_Static_assert(ZONE_Y + ZONE_HAUTEUR <= HAUTEUR_CONTENU,
               "la zone scrollable deborde de la hauteur du contenu");
_Static_assert(BARRE_HAUTEUR_ECRAN + ZONE_Y + ZONE_HAUTEUR <= BANDEAU_Y_ECRAN,
               "la zone scrollable chevauche la bande du bandeau de notification de l'habillage");

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280 /* meme gris de peremption/OFF que le reste de ui/ */
#define COULEUR_VERT             0x2ECC71 /* meme vert "en ligne" que habillage.c (LIAISON_EN_LIGNE) */
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF

/* Meme raisonnement que BOUTON_DESACTIVE_MELANGE dans ecran_fichiers.c. */
#define BOUTON_DESACTIVE_MELANGE 90

/* Tampon suffisant pour {"device":"<nom>","action":"toggle"} -- 32 octets de
 * marge fixe au-dela de POWER_NOM_MAX couvre largement les guillemets/cles
 * litterales (mesure : 32 octets de squelette JSON). */
#define POWER_ARGS_MAX (POWER_NOM_MAX + 40)

static void bouton_definir_desactive(lv_obj_t *bouton, bool desactive)
{
    if (desactive) {
        lv_obj_add_state(bouton, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(bouton, LV_STATE_DISABLED);
    }
}

/* Cree une ligne (bouton pleine largeur + deux labels enfants, nom a gauche
 * et etat a droite) -- meme paire de styles DEFAULT/DISABLED que
 * bouton_creer() dans ecran_fichiers.c pour le fond du bouton. */
static lv_obj_t *ligne_creer(lv_obj_t *parent, lv_coord_t y, lv_obj_t **out_nom, lv_obj_t **out_etat)
{
    lv_obj_t *bouton = lv_button_create(parent);
    lv_obj_remove_style_all(bouton);
    lv_obj_set_size(bouton, LIGNE_LARGEUR, LIGNE_HAUTEUR);
    lv_obj_set_pos(bouton, 0, y);
    lv_obj_set_style_bg_opa(bouton, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bouton, lv_color_hex(COULEUR_BOUTON), 0);
    lv_color_t couleur_desactivee =
        lv_color_mix(lv_color_hex(COULEUR_BOUTON), lv_color_hex(COULEUR_FOND), BOUTON_DESACTIVE_MELANGE);
    lv_obj_set_style_bg_color(bouton, couleur_desactivee, LV_STATE_DISABLED);
    lv_obj_set_style_border_width(bouton, 0, 0);
    lv_obj_set_style_shadow_width(bouton, 0, 0);
    lv_obj_set_style_radius(bouton, 8, 0);
    lv_obj_clear_flag(bouton, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *nom = lv_label_create(bouton);
    lv_obj_set_style_text_font(nom, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(nom, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    /* Points de suspension plutot que deborder sur un nom de prise long
     * (jusqu'a POWER_NOM_MAX-1 caracteres) -- meme technique que le nom de
     * fichier de ecran_fichiers.c. */
    lv_label_set_long_mode(nom, LV_LABEL_LONG_DOT);
    lv_obj_set_width(nom, LIGNE_LARGEUR - 140);
    /* Sans cet appel, le PREMIER lv_label_set_text() verrait une largeur de
     * contenu pas encore resolue -- meme piege, meme correctif que
     * ecran_fichiers.c/ecran_macros.c. */
    lv_obj_update_layout(nom);
    lv_label_set_text(nom, "");
    lv_obj_align(nom, LV_ALIGN_LEFT_MID, 16, 0);

    lv_obj_t *etat = lv_label_create(bouton);
    lv_obj_set_style_text_font(etat, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(etat, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(etat, "");
    lv_obj_align(etat, LV_ALIGN_RIGHT_MID, -16, 0);

    *out_nom = nom;
    *out_etat = etat;
    return bouton;
}

/* Recalcule entierement quelles lignes sont visibles/masquees et leur texte
 * -- appelee UNIQUEMENT quand `power_devices_t.generation` a change (voir
 * ecran_power_mettre_a_jour()), jamais a chaque cycle. Meme discipline
 * "systematique, jamais incrementale" que afficher_page() dans
 * ecran_fichiers.c. */
static void afficher(ecran_power_ctx_t *ctx)
{
    bool vide = (ctx->nb == 0);
    if (vide) {
        lv_obj_clear_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->zone, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ctx->zone, LV_OBJ_FLAG_HIDDEN);
    }

    for (uint8_t i = 0; i < POWER_DEVICES_MAX; i++) {
        if (!vide && i < ctx->nb) {
            lv_label_set_text(ctx->labels_nom[i], ctx->copie_noms[i]);
            lv_label_set_text(ctx->labels_etat[i], ctx->copie_allumee[i] ? "ON" : "OFF");
            lv_obj_clear_flag(ctx->lignes[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ctx->lignes[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/* Applique le grisage de peremption + la couleur ON/OFF -- appelee a CHAQUE
 * mettre_a_jour(), independamment de `afficher()` : `donnees_perimees` peut
 * changer sans qu'aucune prise n'ait bouge (voir le commentaire de tete du
 * .h). Ne touche jamais le TEXTE des labels, seulement leur couleur/l'etat
 * DISABLED du bouton. */
static void appliquer_apparence(ecran_power_ctx_t *ctx)
{
    lv_obj_set_style_text_color(
        ctx->vide, lv_color_hex(ctx->donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_SECONDAIRE), 0);

    for (uint8_t i = 0; i < ctx->nb && i < POWER_DEVICES_MAX; i++) {
        bouton_definir_desactive(ctx->lignes[i], ctx->donnees_perimees);
        uint32_t couleur_nom = ctx->donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_BOUTON;
        uint32_t couleur_etat =
            ctx->donnees_perimees ? COULEUR_GRISE : (ctx->copie_allumee[i] ? COULEUR_VERT : COULEUR_GRISE);
        lv_obj_set_style_text_color(ctx->labels_nom[i], lv_color_hex(couleur_nom), 0);
        lv_obj_set_style_text_color(ctx->labels_etat[i], lv_color_hex(couleur_etat), 0);
    }
}

/* Rappel de la confirmation de bascule : ne bascule que si l'utilisateur a
 * REELLEMENT confirme (voir confirmation.h). `ctx->nom_attente` a ete rempli
 * par ligne_cb() juste avant l'ouverture du dialogue -- toujours le nom
 * REELLEMENT affiche au moment du tap, jamais une relecture du store.
 *
 * Construction manuelle (snprintf borne) du JSON plutot que cJSON : un nom
 * de prise Moonraker est un identifiant de section `[power NOM]`
 * (alphanumerique/underscore, jamais d'espace ni de guillemet -- meme
 * convention qu'un nom de macro Klipper), aucun echappement n'est donc
 * necessaire au-dela du bornage de taille. */
static void rappel_confirmer_toggle(bool confirme, void *contexte)
{
    ecran_power_ctx_t *ctx = contexte;
    if (!confirme || ctx == NULL) {
        return;
    }

    char args[POWER_ARGS_MAX];
    int ecrit = snprintf(args, sizeof(args), "{\"device\":\"%s\",\"action\":\"toggle\"}", ctx->nom_attente);
    if (ecrit < 0 || (size_t)ecrit >= sizeof(args)) {
        return; /* ne devrait jamais arriver : POWER_ARGS_MAX suffit toujours */
    }
    ui_commander(BACKEND_ACTION_POWER, args);
}

/* Tap sur une ligne : ouvre une confirmation ("Toggle device?") -- JAMAIS
 * d'action directe, une prise peut couper l'imprimante ou une impression en
 * cours (voir le commentaire de tete du .h). */
static void ligne_cb(lv_event_t *e)
{
    ecran_power_emplacement_t *info = lv_event_get_user_data(e);
    if (info == NULL || info->ctx == NULL) {
        return;
    }
    ecran_power_ctx_t *ctx = info->ctx;
    if (ctx->donnees_perimees) {
        /* Garde defensive : LV_STATE_DISABLED bloque deja un appui tactile
         * reel, mais host-test envoie l'evenement directement via
         * lv_obj_send_event() -- meme raisonnement que bouton_fichier_cb()
         * dans ecran_fichiers.c. */
        return;
    }
    uint8_t indice = info->emplacement;
    if (indice >= ctx->nb) {
        return; /* ligne cachee/vide a cette position ; ne devrait jamais arriver via un vrai doigt */
    }

    /* Copie bornee manuelle, PAS snprintf(dst, N, "%s", src) -- meme piege,
     * meme correctif que bouton_fichier_cb() dans ecran_fichiers.c
     * (-Werror=format-truncation, gcc ne peut pas prouver `src` borne). */
    const char *nom = ctx->copie_noms[indice];
    size_t longueur = strlen(nom);
    if (longueur >= sizeof(ctx->nom_attente)) {
        longueur = sizeof(ctx->nom_attente) - 1;
    }
    memcpy(ctx->nom_attente, nom, longueur);
    ctx->nom_attente[longueur] = '\0';

    confirmation_ouvrir("Toggle device?", ctx->nom_attente, "Toggle", /*destructif=*/false,
                         rappel_confirmer_toggle, ctx);
}

static void ecran_power_construire(lv_obj_t *parent, void *contexte)
{
    ecran_power_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* "No power devices" (jamais un ecran muet, meme regle que "No
     * files"/"No macros") -- meme position/taille que `zone` ci-dessous,
     * les deux sont mutuellement exclusifs (voir afficher()). */
    ctx->vide = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->vide, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->vide, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_obj_set_style_text_align(ctx->vide, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(ctx->vide, "No power devices");
    lv_obj_set_pos(ctx->vide, MARGE, ZONE_Y);
    lv_obj_set_size(ctx->vide, LARGEUR_CONTENU - 2 * MARGE, ZONE_HAUTEUR);
    lv_obj_add_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN);

    /* Conteneur SCROLLABLE a hauteur FIXE portant le pool ENTIER
     * (POWER_DEVICES_MAX) -- meme idiome que `zone_chauffants` dans
     * ecran_accueil_hub.c (voir son commentaire complet), barre de
     * defilement gardee VISIBLE malgre `lv_obj_remove_style_all()`. */
    ctx->zone = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->zone);
    lv_obj_set_pos(ctx->zone, MARGE, ZONE_Y);
    lv_obj_set_size(ctx->zone, LIGNE_LARGEUR, ZONE_HAUTEUR);
    lv_obj_set_style_bg_opa(ctx->zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctx->zone, 0, 0);
    lv_obj_set_style_pad_all(ctx->zone, 0, 0);
    lv_obj_add_flag(ctx->zone, LV_OBJ_FLAG_SCROLLABLE); /* defensif : deja pose par lv_obj_create() */
    lv_obj_set_scroll_dir(ctx->zone, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ctx->zone, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(ctx->zone, LV_OPA_60, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(ctx->zone, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(ctx->zone, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(ctx->zone, 4, LV_PART_SCROLLBAR);

    for (uint8_t i = 0; i < POWER_DEVICES_MAX; i++) {
        lv_coord_t y = (lv_coord_t)(i * (LIGNE_HAUTEUR + LIGNE_ECART_Y));
        lv_obj_t *nom = NULL;
        lv_obj_t *etat = NULL;
        ctx->lignes[i] = ligne_creer(ctx->zone, y, &nom, &etat);
        ctx->labels_nom[i] = nom;
        ctx->labels_etat[i] = etat;
        lv_obj_add_flag(ctx->lignes[i], LV_OBJ_FLAG_HIDDEN);

        ctx->emplacements[i].ctx = ctx;
        ctx->emplacements[i].emplacement = i;
        lv_obj_add_event_cb(ctx->lignes[i], ligne_cb, LV_EVENT_CLICKED, &ctx->emplacements[i]);
    }

    ctx->nb = 0;
    ctx->derniere_generation = 0;
    /* true : force le premier afficher() meme si le store n'a JAMAIS ete
     * ecrit (generation vaudrait alors deja 0, indistinguable d'un
     * `derniere_generation` initialise a 0 sans ce drapeau). */
    ctx->premiere_maj = true;
    ctx->donnees_perimees = false;
    ctx->nom_attente[0] = '\0';
}

static void ecran_power_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    (void)etat; /* la liste des prises ne vit pas dans etat_klipper_t, voir le commentaire de tete du .h */
    ecran_power_ctx_t *ctx = contexte;
    if (ctx == NULL) {
        return;
    }

    power_devices_t pd;
    power_devices_lire(&pd);

    bool neuf = ctx->premiere_maj || pd.generation != ctx->derniere_generation;
    if (neuf) {
        ctx->premiere_maj = false;
        ctx->derniere_generation = pd.generation;

        uint8_t nb_source = pd.nb;
        if (nb_source > POWER_DEVICES_MAX) {
            nb_source = POWER_DEVICES_MAX; /* defensif : le store lui-meme borne deja a POWER_DEVICES_MAX */
        }
        ctx->nb = 0;
        for (uint8_t i = 0; i < nb_source; i++) {
            /* Copie DEFENSIVEMENT bornee AVANT de lire le premier caractere
             * -- meme raisonnement que ecran_fichiers.c/ecran_macros.c :
             * `pd.devices[i].nom` peut occuper la totalite de POWER_NOM_MAX
             * sans octet nul (power_devices_t est un POD a champs fixes). */
            char nom_borne[POWER_NOM_MAX + 1];
            memcpy(nom_borne, pd.devices[i].nom, POWER_NOM_MAX);
            nom_borne[POWER_NOM_MAX] = '\0';
            if (nom_borne[0] == '\0') {
                continue; /* emplacement vide, ne devrait normalement pas arriver dans les indices < nb */
            }
            size_t longueur = strlen(nom_borne);
            if (longueur >= POWER_NOM_MAX) {
                longueur = POWER_NOM_MAX - 1;
            }
            memcpy(ctx->copie_noms[ctx->nb], nom_borne, longueur);
            ctx->copie_noms[ctx->nb][longueur] = '\0';
            ctx->copie_allumee[ctx->nb] = pd.devices[i].allumee;
            ctx->nb++;
        }
        afficher(ctx);
    }

    ctx->donnees_perimees = donnees_perimees;
    appliquer_apparence(ctx);
}

const ecran_desc_t ECRAN_POWER = {
    .id = "power",
    .titre = "Power",
    .taille_contexte = sizeof(ecran_power_ctx_t),
    .construire = ecran_power_construire,
    .mettre_a_jour = ecran_power_mettre_a_jour,
    .detruire = NULL,
};
