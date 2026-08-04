/* Implementation : voir ecran_console.h pour le contrat.
 *
 * Mise en page (742x436, dans le conteneur de navigation a droite du rail
 * persistant, sous la barre d'etat construite par habillage.c) : un
 * scrollback plein largeur en haut (ZONE_Y..ZONE_Y+ZONE_HAUTEUR, conteneur
 * scrollable a un unique label multi-lignes), une seule rangee en bas
 * (SAISIE_Y) portant trois elements cote a cote -- le champ de saisie
 * (cliquable, ouvre le clavier tactile existant), "Envoyer", "Effacer".
 * Toutes les constantes de position derivent les unes des autres et sont
 * verifiees par les _Static_assert plus bas -- meme discipline que
 * ecran_power.c/ecran_fichiers.c. */
#include "ecran_console.h"

#include <stdio.h>
#include <string.h>

#include "backend.h"
#include "json_util.h"
#include "source_etat.h"

#define LARGEUR_CONTENU 742 /* 800 - RAIL_LARGEUR (58), voir habillage.c */
#define HAUTEUR_CONTENU 436

#define MARGE 20

/* --- Bande couverte par le bandeau de notification de habillage.c, en
 * coordonnees ABSOLUES d'ecran -- meme convention que ecran_power.c. --- */
#define BARRE_HAUTEUR_ECRAN   44
#define HAUTEUR_ECRAN_TOTALE 480
#define BANDEAU_HAUTEUR_ECRAN 60
#define BANDEAU_Y_ECRAN (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)

/* Rangee de saisie : positionnee EN PREMIER (comme PAGINATION_Y dans
 * ecran_fichiers.c) juste au-dessus de la limite absolue imposee par le
 * bandeau, jamais derivee du bas de la zone de contenu -- le _Static_assert
 * plus bas verifie la meme chose en coordonnees ABSOLUES d'ecran. */
#define SAISIE_HAUTEUR 52
#define SAISIE_Y ((BANDEAU_Y_ECRAN - BARRE_HAUTEUR_ECRAN) - SAISIE_HAUTEUR)

#define ZONE_Y      8
#define ZONE_ECART 12
#define ZONE_HAUTEUR (SAISIE_Y - ZONE_ECART - ZONE_Y)
#define ZONE_LARGEUR (LARGEUR_CONTENU - 2 * MARGE)

/* Trois elements cote a cote dans la rangee de saisie : le champ REMPLIT le
 * reste apres les deux boutons de largeur fixe -- meme raisonnement que
 * ENTETE_LARGEUR dans ecran_reglages_wifi.c. */
#define BOUTON_ENVOYER_LARGEUR  110
#define BOUTON_EFFACER_LARGEUR  110
#define SAISIE_ECART             10
#define CHAMP_LARGEUR (ZONE_LARGEUR - BOUTON_ENVOYER_LARGEUR - SAISIE_ECART - BOUTON_EFFACER_LARGEUR - SAISIE_ECART)

#define CHAMP_X    MARGE
#define ENVOYER_X (CHAMP_X + CHAMP_LARGEUR + SAISIE_ECART)
#define EFFACER_X (ENVOYER_X + BOUTON_ENVOYER_LARGEUR + SAISIE_ECART)

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_PRINCIPAL  0xFFFFFF
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280 /* meme gris de peremption/desactive que le reste de ui/ */
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF
#define COULEUR_ZONE_BORDURE     0x2A3644 /* meme bleu-gris que la bordure de textarea de clavier.c */

/* Meme raisonnement que BOUTON_DESACTIVE_MELANGE dans ecran_fichiers.c. */
#define BOUTON_DESACTIVE_MELANGE 90

_Static_assert(CHAMP_LARGEUR > 0, "la rangee de saisie ne laisse plus de place au champ de commande");
_Static_assert(ZONE_Y + ZONE_HAUTEUR <= SAISIE_Y, "le scrollback chevauche la rangee de saisie");
_Static_assert(SAISIE_Y + SAISIE_HAUTEUR <= HAUTEUR_CONTENU,
               "la rangee de saisie deborde de la hauteur du contenu");
_Static_assert(BARRE_HAUTEUR_ECRAN + SAISIE_Y + SAISIE_HAUTEUR <= BANDEAU_Y_ECRAN,
               "la rangee de saisie chevauche la bande du bandeau de notification de l'habillage");
_Static_assert(EFFACER_X + BOUTON_EFFACER_LARGEUR + MARGE <= LARGEUR_CONTENU,
               "la rangee de saisie deborde de la largeur du contenu");

/* Tampon de mise en forme du scrollback : MODULE-STATIC (pas sur la pile,
 * pas dans le contexte -- ~2,3 Kio) reconstruit uniquement quand
 * `console_log_t.generation` a change (voir ecran_console_mettre_a_jour()),
 * jamais a chaque cycle. `lv_label_set_text()` COPIE ce texte dans son propre
 * tampon interne (jamais de reference conservee au-dela de l'appel) : ecraser
 * ce buffer au prochain rafraichissement est donc sans danger.
 *
 * Dimensionnement : au plus CONSOLE_LIGNES_MAX lignes, chacune d'au plus
 * CONSOLE_LIGNE_MAX-1 caracteres utiles (toujours NUL-terminee, voir le
 * snprintf() de bornage dans console_log_ajouter()) + un separateur '\n'
 * entre deux lignes consecutives (jamais apres la derniere) + le NUL final --
 * le _Static_assert ci-dessous verifie que la marge (CONSOLE_LIGNES_MAX
 * octets) couvre bien les separateurs meme si les constantes de
 * console_log.h changent un jour. */
#define CONSOLE_SCROLLBACK_TEXTE_MAX (CONSOLE_LIGNES_MAX * CONSOLE_LIGNE_MAX + CONSOLE_LIGNES_MAX)
_Static_assert(CONSOLE_SCROLLBACK_TEXTE_MAX >
                   (size_t)(CONSOLE_LIGNES_MAX * (CONSOLE_LIGNE_MAX - 1) + (CONSOLE_LIGNES_MAX - 1)),
               "le tampon de scrollback ne couvre plus le pire cas (lignes pleines + separateurs)");
static char g_scrollback_texte[CONSOLE_SCROLLBACK_TEXTE_MAX];

/* Tampon de la commande echappee pour le JSON envoye -- pire cas : chaque
 * octet de `commande` (CLAVIER_VALEUR_MAX-1 au maximum, voir clavier.h)
 * devient un `\uXXXX` de 6 octets (voir json_util.h), plus le NUL final. */
#define CONSOLE_ECHAPPEE_MAX (6 * (CLAVIER_VALEUR_MAX - 1) + 1)
/* `{"script":"<echappee>"}` autour -- 13 octets de squelette JSON litteral,
 * marge ronde au-dessus (meme discipline que GCODE_ARGS_MAX dans
 * ecran_fichiers.c/POWER_ARGS_MAX dans ecran_power.c). */
#define CONSOLE_ARGS_MAX (CONSOLE_ECHAPPEE_MAX + 16)

/* Tampon de l'echo local (">> <commande>") -- console_log_ajouter() tronque
 * silencieusement au-dela de CONSOLE_LIGNE_MAX de toute facon, ce tampon n'a
 * donc besoin de couvrir que ">> " + la commande la plus longue possible. */
#define CONSOLE_ECHO_MAX (CLAVIER_VALEUR_MAX + 4)

/* Construit `{"script":"<commande echappee>"}` -- construction manuelle (pas
 * cJSON, voir le commentaire de tete du .h) : `json_echapper_chaine()`
 * echappe le CONTENU, les guillemets englobants sont ajoutes ICI par le
 * format litteral, exactement le contrat documente par json_util.h. Rend
 * false SANS rien garantir sur `sortie` si un tampon est trop court -- ne
 * devrait jamais arriver, CONSOLE_ECHAPPEE_MAX/CONSOLE_ARGS_MAX couvrent deja
 * le pire cas d'une commande de CLAVIER_VALEUR_MAX-1 octets. */
static bool construire_arguments_console(const char *commande, char *sortie, size_t taille)
{
    if (commande == NULL || sortie == NULL || taille == 0) {
        return false;
    }
    char echappee[CONSOLE_ECHAPPEE_MAX];
    size_t necessaire = json_echapper_chaine(echappee, sizeof(echappee), commande);
    if (necessaire >= sizeof(echappee)) {
        return false; /* defensif : ne devrait jamais arriver, voir le commentaire ci-dessus */
    }
    int ecrit = snprintf(sortie, taille, "{\"script\":\"%s\"}", echappee);
    if (ecrit < 0 || (size_t)ecrit >= taille) {
        return false; /* defensif : ne devrait jamais arriver, meme raison */
    }
    return true;
}

/* Reconstruit g_scrollback_texte a partir d'un instantane du store -- appelee
 * UNIQUEMENT quand `generation` a change (voir ecran_console_mettre_a_jour()).
 * `log->debut` designe la plus ancienne ligne valide (ring, voir
 * console_log.h) : les `log->nb` lignes sont donc lues dans l'ORDRE
 * chronologique en repartant de `debut`, modulo CONSOLE_LIGNES_MAX. */
static void construire_texte_scrollback(const console_log_t *log)
{
    size_t pos = 0;
    for (uint8_t i = 0; i < log->nb; i++) {
        uint8_t indice = (uint8_t)((log->debut + i) % CONSOLE_LIGNES_MAX);
        const char *ligne = log->lignes[indice]; /* toujours NUL-terminee, voir console_log_ajouter() */
        size_t longueur = strlen(ligne);
        if (i > 0) {
            g_scrollback_texte[pos++] = '\n';
        }
        memcpy(g_scrollback_texte + pos, ligne, longueur);
        pos += longueur;
    }
    g_scrollback_texte[pos] = '\0';
}

/* Affiche le texte courant + auto-scroll en bas -- appelee a la construction
 * ET a chaque generation neuve (voir ecran_console_mettre_a_jour()).
 * `lv_obj_update_layout()` avant le scroll : le label vient de changer de
 * hauteur (LV_LABEL_LONG_WRAP), le conteneur doit recalculer son etendue
 * scrollable avant qu'on puisse lui demander d'aller au bout -- meme piege
 * documente ailleurs dans ce depot pour LV_LABEL_LONG_DOT
 * (ecran_reglages_wifi.c/ecran_power.c). LV_COORD_MAX est CLAMPE par LVGL a
 * la position de scroll maximale reelle -- pas besoin de connaitre la
 * hauteur exacte du contenu ici. */
static void rafraichir_scrollback(ecran_console_ctx_t *ctx, const console_log_t *log)
{
    construire_texte_scrollback(log);
    lv_label_set_text(ctx->zone_label, g_scrollback_texte);
    lv_obj_update_layout(ctx->zone);
    lv_obj_scroll_to_y(ctx->zone, LV_COORD_MAX, LV_ANIM_OFF);
}

/* LV_STATE_DISABLED n'est PAS herite automatiquement par les enfants LVGL :
 * le label enfant d'un bouton garde son style DEFAULT tant qu'il ne recoit
 * pas lui-meme cet etat -- meme constat, meme correctif que
 * bouton_definir_desactive() dans ecran_fichiers.c, repris ici a l'identique
 * (bouton PUIS son unique label enfant, jamais l'un sans l'autre). */
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

/* Recolore/retextualise le champ de saisie selon qu'une commande est en
 * attente ou non, et (des)active "Envoyer" en consequence -- appelee a
 * chaque changement de ctx->commande (validation clavier, envoi). */
static void rafraichir_champ(ecran_console_ctx_t *ctx)
{
    bool vide = (ctx->commande[0] == '\0');
    lv_label_set_text(ctx->champ_label, vide ? "Tap to type a command" : ctx->commande);
    lv_obj_set_style_text_color(
        ctx->champ_label, lv_color_hex(vide ? COULEUR_TEXTE_SECONDAIRE : COULEUR_TEXTE_BOUTON), 0);
    bouton_definir_desactive(ctx->bouton_envoyer, vide);
}

/* Rappel de validation du clavier (voir clavier.h) : pose SEULEMENT la valeur
 * dans le champ, n'envoie RIEN -- l'envoi reel est un geste distinct
 * ("Envoyer", voir bouton_envoyer_cb ci-dessous), pour laisser une chance de
 * relire/corriger avant de toucher la machine. `valeur == NULL` = annule,
 * rien ne change (contrat clavier.h). */
static void rappel_commande(const char *valeur, void *contexte)
{
    ecran_console_ctx_t *ctx = contexte;
    if (ctx == NULL || valeur == NULL) {
        return;
    }
    snprintf(ctx->commande, sizeof(ctx->commande), "%s", valeur);
    rafraichir_champ(ctx);
}

static void champ_cb(lv_event_t *e)
{
    ecran_console_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL) {
        return;
    }
    clavier_ouvrir("Command", ctx->commande, CLAVIER_TEXTE, rappel_commande, ctx);
}

/* "Envoyer" : echo local (toujours, meme si l'envoi reel echoue a se
 * construire -- l'utilisateur voit ce qu'il a tape) puis
 * `ui_commander(BACKEND_ACTION_GCODE, ...)`. Le champ est vide APRES, dans
 * tous les cas (voir le commentaire de tete du .h). */
static void bouton_envoyer_cb(lv_event_t *e)
{
    ecran_console_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL || ctx->commande[0] == '\0') {
        return;
    }

    char echo[CONSOLE_ECHO_MAX];
    snprintf(echo, sizeof(echo), ">> %s", ctx->commande);
    console_log_ajouter(echo);

    char args[CONSOLE_ARGS_MAX];
    if (construire_arguments_console(ctx->commande, args, sizeof(args))) {
        ui_commander(BACKEND_ACTION_GCODE, args);
    }

    ctx->commande[0] = '\0';
    rafraichir_champ(ctx);
}

/* "Effacer" : vide le SCROLLBACK (console_log_effacer(), store dedie), pas le
 * champ de saisie -- deux boutons, deux portees distinctes (voir le
 * commentaire de tete du .h). Le prochain ecran_console_mettre_a_jour() verra
 * `generation` avoir change et redessinera un scrollback vide -- aucun appel
 * direct a rafraichir_scrollback() ici. */
static void bouton_effacer_cb(lv_event_t *e)
{
    (void)e;
    console_log_effacer();
}

/* Bouton pleine-largeur avec un unique label enfant -- meme paire de styles
 * DEFAULT/DISABLED que bouton_creer() dans ecran_fichiers.c, reprise ici pour
 * les trois elements de la rangee de saisie (champ/Envoyer/Effacer). */
static lv_obj_t *bouton_creer(lv_obj_t *parent, const char *texte, lv_coord_t x, lv_coord_t y,
                               lv_coord_t largeur, lv_coord_t hauteur, lv_obj_t **out_label)
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
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_TEXTE_BOUTON), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COULEUR_GRISE), LV_STATE_DISABLED);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(label, largeur - 16);
    lv_obj_update_layout(label); /* meme piege LV_LABEL_LONG_DOT que ecran_fichiers.c/ecran_macros.c */
    lv_label_set_text(label, texte);
    lv_obj_center(label);

    if (out_label != NULL) {
        *out_label = label;
    }
    return bouton;
}

static void ecran_console_construire(lv_obj_t *parent, void *contexte)
{
    ecran_console_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    /* Scrollback : conteneur scrollable a hauteur FIXE, un unique label
     * multi-lignes en enfant (option "label dans un conteneur scrollable" du
     * brief). Une bordure discrete le distingue du fond -- meme couleur que
     * la bordure de textarea de clavier.c. */
    ctx->zone = lv_obj_create(parent);
    lv_obj_remove_style_all(ctx->zone);
    lv_obj_set_pos(ctx->zone, MARGE, ZONE_Y);
    lv_obj_set_size(ctx->zone, ZONE_LARGEUR, ZONE_HAUTEUR);
    lv_obj_set_style_bg_opa(ctx->zone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctx->zone, 1, 0);
    lv_obj_set_style_border_color(ctx->zone, lv_color_hex(COULEUR_ZONE_BORDURE), 0);
    lv_obj_set_style_radius(ctx->zone, 8, 0);
    lv_obj_set_style_pad_all(ctx->zone, 10, 0);
    lv_obj_add_flag(ctx->zone, LV_OBJ_FLAG_SCROLLABLE); /* defensif : deja pose par lv_obj_create() */
    lv_obj_clear_flag(ctx->zone, LV_OBJ_FLAG_CLICKABLE); /* lecture seule : jamais de rappel de tap sur le scrollback */
    lv_obj_set_scroll_dir(ctx->zone, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(ctx->zone, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_style_bg_opa(ctx->zone, LV_OPA_60, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(ctx->zone, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), LV_PART_SCROLLBAR);
    lv_obj_set_style_radius(ctx->zone, 2, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(ctx->zone, 4, LV_PART_SCROLLBAR);

    ctx->zone_label = lv_label_create(ctx->zone);
    /* Police mono absente de ce firmware (LV_FONT_UNSCII_* desactivees, voir
     * simulateur/lv_conf.h) -- repli documente par le brief : montserrat_14. */
    lv_obj_set_style_text_font(ctx->zone_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->zone_label, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_label_set_long_mode(ctx->zone_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(ctx->zone_label, ZONE_LARGEUR - 2 * 10 /* pad_all du conteneur */ - 4 /* largeur barre de defilement */);
    lv_label_set_text(ctx->zone_label, "");

    /* Rangee de saisie : champ cliquable (ouvre le clavier tactile), puis
     * "Envoyer", puis "Effacer". */
    ctx->champ = bouton_creer(parent, "", CHAMP_X, SAISIE_Y, CHAMP_LARGEUR, SAISIE_HAUTEUR, &ctx->champ_label);
    lv_obj_set_style_text_align(ctx->champ_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_align(ctx->champ_label, LV_ALIGN_LEFT_MID, 12, 0); /* remplace le centrage par defaut de bouton_creer() */
    lv_obj_add_event_cb(ctx->champ, champ_cb, LV_EVENT_CLICKED, ctx);

    ctx->bouton_envoyer =
        bouton_creer(parent, "Send", ENVOYER_X, SAISIE_Y, BOUTON_ENVOYER_LARGEUR, SAISIE_HAUTEUR, NULL);
    lv_obj_add_event_cb(ctx->bouton_envoyer, bouton_envoyer_cb, LV_EVENT_CLICKED, ctx);

    ctx->bouton_effacer =
        bouton_creer(parent, "Clear", EFFACER_X, SAISIE_Y, BOUTON_EFFACER_LARGEUR, SAISIE_HAUTEUR, NULL);
    lv_obj_add_event_cb(ctx->bouton_effacer, bouton_effacer_cb, LV_EVENT_CLICKED, ctx);

    ctx->commande[0] = '\0';
    ctx->derniere_generation = 0;
    /* true : force le premier rafraichir_scrollback() meme si le store n'a
     * JAMAIS ete ecrit (generation vaudrait alors deja 0, indistinguable d'un
     * `derniere_generation` initialise a 0 sans ce drapeau) -- meme idiome
     * que ctx->premiere_maj dans ecran_power.c. */
    ctx->premiere_maj = true;

    rafraichir_champ(ctx);
}

static void ecran_console_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    /* Le scrollback ne vit pas dans etat_klipper_t (store dedie, voir
     * console_log.h) et n'a pas de notion de peremption propre : c'est un
     * JOURNAL de ce qui a ete recu/envoye, pas une mesure instantanee -- une
     * ligne deja affichee reste vraie meme si la liaison tombe juste apres,
     * contrairement a une temperature qui, elle, doit se griser (meme
     * raisonnement que l'entete WiFi de ecran_reglages_wifi.c, qui ignore
     * aussi ce champ pour une raison differente mais symetrique). */
    (void)etat;
    (void)donnees_perimees;

    ecran_console_ctx_t *ctx = contexte;
    if (ctx == NULL) {
        return;
    }

    console_log_t log;
    console_log_lire(&log);

    bool neuf = ctx->premiere_maj || log.generation != ctx->derniere_generation;
    if (neuf) {
        ctx->premiere_maj = false;
        ctx->derniere_generation = log.generation;
        rafraichir_scrollback(ctx, &log);
    }
}

const ecran_desc_t ECRAN_CONSOLE = {
    .id = "console",
    .titre = "Console",
    .taille_contexte = sizeof(ecran_console_ctx_t),
    .construire = ecran_console_construire,
    .mettre_a_jour = ecran_console_mettre_a_jour,
    /* Rien a liberer au-dela du contexte : le clavier est un singleton geré
     * entierement par clavier.c (voir clavier.h, "un seul clavier existe
     * jamais") -- s'il est ouvert au moment ou cet ecran est depile, il reste
     * sur lv_layer_top(), au-dessus de tout, et se referme normalement a sa
     * propre validation/annulation, independamment du cycle de vie de cet
     * ecran (meme raisonnement que ecran_reglages_wifi.c, qui utilise aussi
     * clavier_ouvrir() avec detruire = NULL). */
    .detruire = NULL,
};
