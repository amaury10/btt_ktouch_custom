/* Implémentation : voir ecran_macros.h pour le contrat.
 *
 * Mise en page (800x436, sous la barre d'état construite par habillage.c) :
 * une ligne d'avertissement de troncature tout en haut (masquée sauf
 * `macros_tronquees`), une COLONNE UNIQUE de boutons pleine largeur (5
 * macros par page, refonte jalon 3b -- grille 4x4 remplacée par une liste 1D
 * : chaque nom profite de toute la largeur du contenu, bien moins de
 * troncature qu'a 184px de large), une ligne de pagination en bas
 * ("< Page X/Y >"). Toutes les constantes de position sont dérivées les unes
 * des autres (voir les _Static_assert plus bas), même discipline que
 * ecran_accueil.c.
 *
 * BOUTON_HAUTEUR (52) et ECRAN_MACROS_PAGE_TAILLE (5, voir ecran_macros.h)
 * sont choisis ENSEMBLE, pas indépendamment : la hauteur de contenu
 * disponible entre GRILLE_Y et la limite absolue imposée par le bandeau de
 * notification (voir le commentaire de PAGINATION_Y plus bas) ne laisse la
 * place que pour 5 boutons de 52px (confortable, largement au-dessus du
 * minimum tactile de 44px) -- 6 boutons de 52px déborderait ; et 6 boutons
 * même au minimum strict de 44px NE PASSERAIENT PAS non plus sans rogner
 * AUSSI l'écart entre boutons (le calcul absolu dépasse la limite du bandeau
 * de quelques px), un confort sacrifié pour une seule ligne -- non retenu. */
#include "ecran_macros.h"

#include <stdio.h>
#include <string.h>

#include "backend.h"
#include "habillage.h"
#include "source_etat.h"

#define LARGEUR_CONTENU 800
#define HAUTEUR_CONTENU 436

#define MARGE 20

#define AVERTISSEMENT_Y       8
#define AVERTISSEMENT_HAUTEUR 22

#define GRILLE_Y            36
/* Colonne unique pleine largeur (refonte jalon 3b) : BOUTON_LARGEUR n'est
 * plus un littéral indépendant, il REMPLIT exactement la largeur de contenu
 * disponible entre les deux marges -- exactement la même dérivation que
 * BOUTON_LARGEUR dans ecran_fichiers.c (colonne unique, pas de division par
 * un nombre de colonnes puisqu'il n'y en a plus qu'une). */
#define BOUTON_LARGEUR (LARGEUR_CONTENU - 2 * MARGE)
#define BOUTON_HAUTEUR      52
#define BOUTON_ECART_Y       6
#define GRILLE_BAS (GRILLE_Y + ECRAN_MACROS_PAGE_TAILLE * BOUTON_HAUTEUR + \
                    (ECRAN_MACROS_PAGE_TAILLE - 1) * BOUTON_ECART_Y)

#define PAGINATION_HAUTEUR 44
/* PAS "HAUTEUR_CONTENU - PAGINATION_HAUTEUR - MARGE" (bug reel, trouve a la
 * capture -- voir le commentaire de BOUTON_ENREGISTRER_Y dans
 * ecran_configuration.c pour l'incident jumeau qui a fait ecrire ce
 * commentaire-la en premier) : cette derivation ignore le bandeau de
 * notification de l'habillage (60 px, superpose en ABSOLU depuis le bas de
 * l'ECRAN ENTIER -- BANDEAU_HAUTEUR_ECRAN/BANDEAU_Y_ECRAN plus bas, pas de
 * la zone de contenu). Constatee directement (capture macros-troncature.png,
 * cette tache) : "Page 1/3" et les boutons < > tombaient dans la bande du
 * bandeau, a moitie recouverts des le premier "host connected". Positionnee
 * ICI juste sous la grille (avec une marge modeste), jamais pres du bas du
 * contenu -- le _Static_assert plus bas verifie desormais la MEME chose que
 * BOUTON_ENREGISTRER_Y en coordonnees ABSOLUES d'ecran. */
#define PAGINATION_Y (GRILLE_BAS + 8)
#define BOUTON_PAGE_LARGEUR 60

/* Bande couverte par le bandeau de notification, en coordonnees ABSOLUES
 * d'ecran -- memes trois valeurs que habillage.c (BARRE_HAUTEUR=44,
 * BANDEAU_HAUTEUR=60, HAUTEUR_ECRAN=480), DOIVENT rester synchronisees avec
 * lui (aucune des deux n'incluant l'autre), meme convention que
 * ecran_configuration.c. */
#define BARRE_HAUTEUR_ECRAN   44
#define HAUTEUR_ECRAN_TOTALE 480
#define BANDEAU_HAUTEUR_ECRAN 60
#define BANDEAU_Y_ECRAN (HAUTEUR_ECRAN_TOTALE - BANDEAU_HAUTEUR_ECRAN)

#define COULEUR_FOND             0x10161D
#define COULEUR_TEXTE_SECONDAIRE 0xC9D1D9
#define COULEUR_GRISE            0x6B7280 /* meme gris de peremption que le reste de ui/ */
#define COULEUR_BOUTON           0x2A3644
#define COULEUR_TEXTE_BOUTON     0xFFFFFF
#define COULEUR_AVERTISSEMENT    0xF1C40F /* meme ambre que la pastille "unstable", habillage.c */

/* Meme raisonnement que BOUTON_DESACTIVE_MELANGE dans ecran_accueil.c (voir
 * son commentaire complet la-bas, revue tache 6, fix round 1, jalon 2b) : un
 * style local DEDIE a LV_STATE_DISABLED, jamais un simple lv_obj_add_state()
 * seul, est ce qui rend le grisage honnete -- LV_STATE_DISABLED seul est
 * pixel-identique a l'etat arme tant qu'aucun style local n'est enregistre
 * pour cet etat precis. */
#define BOUTON_DESACTIVE_MELANGE 90

/* Colonne unique : BOUTON_LARGEUR REMPLIT deja exactement l'espace entre les
 * deux marges (voir sa definition ci-dessus) -- cet assert reste en place
 * (tautologique aujourd'hui) pour attraper toute regression future qui
 * redonnerait a BOUTON_LARGEUR une valeur independante. */
_Static_assert(MARGE + BOUTON_LARGEUR + MARGE <= LARGEUR_CONTENU,
               "le bouton pleine largeur deborde de la largeur du contenu");
/* Fix round 1 (revue tache 6, L1) : GRILLE_Y (36) etait un litteral NU, pas
 * derive de AVERTISSEMENT_Y + AVERTISSEMENT_HAUTEUR, malgre ce que promet le
 * commentaire de tete du fichier ("toutes les constantes de position sont
 * derivees les unes des autres, voir les _Static_assert"). Meme classe de
 * bug que la superposition pagination/bandeau ci-dessous, juste jamais
 * observee sur une capture : la ligne d'avertissement (8..30) et GRILLE_Y
 * (36) ne se chevauchent PAS aujourd'hui, mais rien ne l'aurait signale si
 * AVERTISSEMENT_HAUTEUR avait grandi. */
_Static_assert(AVERTISSEMENT_Y + AVERTISSEMENT_HAUTEUR <= GRILLE_Y,
               "la ligne d'avertissement de troncature chevauche la premiere rangee de la grille");
_Static_assert(GRILLE_BAS <= PAGINATION_Y,
               "la colonne de macros chevauche la ligne de pagination");
_Static_assert(PAGINATION_Y + PAGINATION_HAUTEUR <= HAUTEUR_CONTENU,
               "la ligne de pagination deborde de la hauteur du contenu");
/* Le garde-fou qui manquait (bug reel constate a la capture, voir le
 * commentaire de PAGINATION_Y ci-dessus) : le bas de la ligne de pagination
 * (en coordonnees ABSOLUES d'ecran) doit rester strictement au-dessus du
 * haut du bandeau de notification -- sans quoi "host connected" (ou
 * n'importe quelle notification) recouvre "Page X/Y" et les boutons < >
 * exactement comme la capture l'a montre. */
_Static_assert(BARRE_HAUTEUR_ECRAN + PAGINATION_Y + PAGINATION_HAUTEUR <= BANDEAU_Y_ECRAN,
               "la ligne de pagination chevauche la bande du bandeau de notification de l'habillage");

bool ecran_macros_construire_arguments(const char *nom, char *sortie, size_t taille)
{
    if (nom == NULL || sortie == NULL || taille == 0) {
        return false;
    }
    int ecrit = snprintf(sortie, taille, "{\"nom\":\"%s\"}", nom);
    if (ecrit < 0 || (size_t)ecrit >= taille) {
        return false;
    }
    return true;
}

/* Meme paire de styles locaux (DEFAULT + DISABLED, bouton ET label) que
 * bouton_creer()/bouton_definir_desactive() dans ecran_accueil.c -- voir son
 * commentaire de tete pour le recit complet de la lecon (revue finale jalon
 * 2b, boutons "grises" pixel-identiques a l'etat arme). Parametree en
 * position/taille (contrairement a la version accueil, fixe) : ce fichier
 * en a besoin pour la grille ET pour les deux boutons de pagination, qui
 * n'ont pas la meme taille. */
/* Fix défaut 4 (revue live jalon 3a) : `alignement_texte` distingue les
 * boutons de la grille (LV_TEXT_ALIGN_LEFT -- un nom de macro tronqué par
 * LV_LABEL_LONG_DOT doit se lire depuis la gauche, "..." venant naturellement
 * MANGER la fin, pas le début) des deux boutons de pagination
 * (LV_TEXT_ALIGN_CENTER -- un simple "<"/">" perdu dans une étiquette bien
 * plus large que son seul glyphe, voir BOUTON_PAGE_LARGEUR - 12 = 48 px pour
 * un caractère, restait planté à GAUCHE de cette étiquette au lieu du centre
 * du bouton, capture macros-pagination-avant-fix.png). lv_obj_center(label)
 * plus bas ne centre que la BOÎTE de l'étiquette dans le bouton -- pas le
 * texte À L'INTÉRIEUR de cette boîte quand elle est plus large que son
 * contenu, ce qui est exactement le cas ici : sans ce style, le texte reste
 * aligné à gauche (LV_TEXT_ALIGN_AUTO, la valeur par défaut de LVGL) dans une
 * boîte pourtant centrée. */
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
    /* Points de suspension plutot que deborder sur un nom de macro long
     * (jusqu'a KLIPPER_MACRO_NOM_MAX-1 caracteres) -- meme technique que le
     * nom de fichier de ecran_accueil.c. Largeur explicite = celle du
     * bouton moins un peu de marge interne, requise par LV_LABEL_LONG_DOT. */
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(label, alignement_texte, 0);
    lv_obj_set_width(label, largeur - 12);
    /* Sans cet appel, le PREMIER lv_label_set_text() ci-dessous verrait une
     * largeur de contenu pas encore resolue (proche de 0) et tronquerait en
     * "..." meme un texte court -- LV_LABEL_LONG_DOT calcule sa troncature
     * depuis lv_obj_get_content_width(), pas depuis la taille qu'on vient de
     * poser (meme piege, meme correctif que ecran_configuration.c, voir son
     * commentaire sur ecran_configuration_rafraichir_label()). Les appels
     * ULTERIEURS de lv_label_set_text() (afficher_page(), a chaque
     * changement de page) n'en ont pas besoin : la taille de `label` ne
     * change plus jamais apres cette construction. */
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

/* min(1, ceil(nb_filtrees / ECRAN_MACROS_PAGE_TAILLE)) : au moins une page,
 * meme quand la liste est vide (une page vide, qui affiche "No macros" --
 * jamais zero page, qui rendrait `ctx->page` invalide sans qu'il y ait de
 * borne haute a comparer). */
static uint8_t nb_pages(uint8_t nb_filtrees)
{
    if (nb_filtrees == 0) {
        return 1;
    }
    return (uint8_t)((nb_filtrees + ECRAN_MACROS_PAGE_TAILLE - 1) / ECRAN_MACROS_PAGE_TAILLE);
}

/* Recalcule entierement ce que la page courante doit montrer : les boutons
 * (texte + visibilite), l'etat vide, l'avertissement de troncature, et la
 * ligne de pagination -- appelee par mettre_a_jour() ET par les rappels
 * Precedent/Suivant (qui ne recoivent jamais de nouvel `etat`, seulement un
 * changement de ctx->page sur les memes donnees deja filtrees). Le grisage
 * de peremption est SYSTEMATIQUE a chaque appel (meme lecon que
 * tuile_griser()/bouton_definir_desactive() dans ecran_accueil.c) : un appel
 * avec donnees_perimees=false doit toujours rendre exactement l'etat actif,
 * y compris apres plusieurs allers-retours. */
static void afficher_page(ecran_macros_ctx_t *ctx)
{
    uint8_t pages = nb_pages(ctx->nb_filtrees);
    if (ctx->page >= pages) {
        ctx->page = (uint8_t)(pages - 1);
    }

    bool vide = (ctx->nb_filtrees == 0);
    if (vide) {
        lv_obj_clear_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_text_color(ctx->vide, lv_color_hex(ctx->donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_SECONDAIRE), 0);

    for (uint8_t emplacement = 0; emplacement < ECRAN_MACROS_PAGE_TAILLE; emplacement++) {
        uint16_t indice = (uint16_t)ctx->page * ECRAN_MACROS_PAGE_TAILLE + emplacement;
        if (!vide && indice < ctx->nb_filtrees) {
            lv_label_set_text(ctx->labels[emplacement], ctx->macros_filtrees[indice]);
            lv_obj_clear_flag(ctx->boutons[emplacement], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(ctx->boutons[emplacement], LV_OBJ_FLAG_HIDDEN);
        }
        /* Grisage systematique, meme sur un bouton cache : un aller-retour
         * de page ne doit jamais laisser un bouton "reactif" en memoire
         * pendant que les donnees sont perimees. */
        bouton_definir_desactive(ctx->boutons[emplacement], ctx->donnees_perimees);
    }

    if (ctx->macros_tronquees) {
        lv_label_set_text(ctx->avertissement,
                           "Some macros are not shown (list truncated)");
        lv_obj_clear_flag(ctx->avertissement, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ctx->avertissement, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_text_color(ctx->avertissement,
                                 lv_color_hex(ctx->donnees_perimees ? COULEUR_GRISE : COULEUR_AVERTISSEMENT), 0);

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
    lv_obj_set_style_text_color(ctx->page_label,
                                 lv_color_hex(ctx->donnees_perimees ? COULEUR_GRISE : COULEUR_TEXTE_SECONDAIRE), 0);
    /* Bornes de pagination ET grisage combines dans le meme appel -- un
     * bouton Precedent/Suivant desactive par une borne (premiere/derniere
     * page) reste desactive meme quand donnees_perimees redevient faux ;
     * l'inverse (donnees fraiches, borne franchissable) doit rester
     * cliquable. */
    bouton_definir_desactive(ctx->bouton_precedent, ctx->donnees_perimees || ctx->page == 0);
    bouton_definir_desactive(ctx->bouton_suivant,
                              ctx->donnees_perimees || (uint8_t)(ctx->page + 1) >= pages);
}

/* Tache 6 : envoie BACKEND_ACTION_MACRO via ui_commander() (voir
 * ui/source_etat.h) pour la macro affichee sur CE bouton au moment du clic
 * -- pas un nom mis en cache ailleurs, `ctx->macros_filtrees` et `ctx->page`
 * sont relus ICI, exactement au moment du clic, pour que le nom transmis
 * soit toujours celui reellement affiche.
 *
 * Deux issues, deux libelles distincts et delibirement PAS le meme mot
 * ("sent" contre "failed"), trouvaille B de la revue des taches 4/5 :
 * - ESP_OK ⇒ "Macro sent: <nom>" (info) -- la commande est ACCEPTEE
 *   dans la file, rien de plus. Ce N'EST PAS une confirmation que Klipper a
 *   reellement execute la macro avec succes : sur un vrai Klipper, la
 *   reponse JSON-RPC corrolee de printer.gcode.script rend {"result":"ok"}
 *   AUSSI pour une macro totalement inconnue (voir moonraker_rpc.h, pres de
 *   rpc_lire_reponse et RPC_MSG_AUTRE) -- l'echec reel n'arrive que de facon
 *   asynchrone via notify_gcode_response, un canal que ce seam n'interprete
 *   pas encore (voir le rapport de tache 6). Fix round 1 (revue tache 6, N1) :
 *   "launched" (lancee) suggerait que l'EXECUTION avait commence, ce qui est
 *   faux pour un nom que Klipper va rejeter -- "sent" (envoyee) ne pretend
 *   que ce que cet ecran sait reellement : la commande a quitte la file
 *   d'attente locale, rien de plus.
 * - erreur (file pleine, boucle pas demarree) ⇒ "Command failed: <nom>",
 *   rouge -- EXACTEMENT le meme seam synchrone que executer_commande() dans
 *   ecran_accueil.c (tache 9), avec le nom de la macro comme libelle
 *   (contrairement aux quatre actions communes, celui-ci EST connu ici).
 *   L'echec ASYNCHRONE (commande acceptee, mais qui echoue plus tard a
 *   l'execution reelle -- exactement ce que backend_factice.c demontre avec
 *   sa sentinelle MACRO_ECHEC, scenario 11) remonte par le seam GENERIQUE
 *   existant (habillage_pomper() -> ui_commande_echec() -> "Command failed:
 *   macro", SANS le nom -- ce canal ne porte que le nom de l'ACTION,
 *   "macro", jamais ses arguments, voir ui_commande_echec() dans
 *   source_etat.h) : cet ecran ne le duplique pas, la banniere generique
 *   REMPLACE simplement celle-ci quelques cycles plus tard (habillage_notifier()
 *   remplace, jamais n'empile, voir habillage.h). */
static void bouton_macro_cb(lv_event_t *e)
{
    ecran_macros_emplacement_t *info = lv_event_get_user_data(e);
    if (info == NULL || info->ctx == NULL) {
        return;
    }
    ecran_macros_ctx_t *ctx = info->ctx;
    if (ctx->donnees_perimees) {
        /* Garde defensive : LV_STATE_DISABLED bloque deja un appui tactile
         * reel (lv_indev.c), mais host-test envoie l'evenement directement
         * via lv_obj_send_event(), qui ne passe jamais par cette
         * verification -- meme raisonnement que ecran_accueil.c. */
        return;
    }
    uint16_t indice = (uint16_t)ctx->page * ECRAN_MACROS_PAGE_TAILLE + info->emplacement;
    if (indice >= ctx->nb_filtrees) {
        return; /* bouton cache/vide sur cette page ; ne devrait jamais arriver via un vrai doigt */
    }
    const char *nom = ctx->macros_filtrees[indice];

    char arguments[ECRAN_MACROS_ARGUMENTS_MAX];
    if (!ecran_macros_construire_arguments(nom, arguments, sizeof(arguments))) {
        return; /* ne devrait jamais arriver : nom deja borne a KLIPPER_MACRO_NOM_MAX */
    }

    esp_err_t erreur = ui_commander(BACKEND_ACTION_MACRO, arguments);
    char texte[64];
    if (erreur != ESP_OK) {
        snprintf(texte, sizeof(texte), "Command failed: %s", nom);
        habillage_notifier(texte, true);
    } else {
        snprintf(texte, sizeof(texte), "Macro sent: %s", nom);
        habillage_notifier(texte, false);
    }
}

static void bouton_precedent_cb(lv_event_t *e)
{
    ecran_macros_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL || ctx->donnees_perimees || ctx->page == 0) {
        return;
    }
    ctx->page--;
    afficher_page(ctx);
}

static void bouton_suivant_cb(lv_event_t *e)
{
    ecran_macros_ctx_t *ctx = lv_event_get_user_data(e);
    if (ctx == NULL || ctx->donnees_perimees) {
        return;
    }
    if ((uint8_t)(ctx->page + 1) >= nb_pages(ctx->nb_filtrees)) {
        return;
    }
    ctx->page++;
    afficher_page(ctx);
}

static void ecran_macros_construire(lv_obj_t *parent, void *contexte)
{
    ecran_macros_ctx_t *ctx = contexte;
    if (parent == NULL || ctx == NULL) {
        return;
    }

    lv_obj_set_style_bg_color(parent, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    ctx->avertissement = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->avertissement, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(ctx->avertissement, lv_color_hex(COULEUR_AVERTISSEMENT), 0);
    lv_label_set_text(ctx->avertissement, "");
    lv_obj_set_pos(ctx->avertissement, MARGE, AVERTISSEMENT_Y);
    lv_obj_set_size(ctx->avertissement, LARGEUR_CONTENU - 2 * MARGE, AVERTISSEMENT_HAUTEUR);
    lv_obj_add_flag(ctx->avertissement, LV_OBJ_FLAG_HIDDEN);

    /* "No macros" (brief : jamais un ecran muet) -- centre dans la zone que
     * la grille occuperait sinon, masque des qu'au moins une macro visible
     * existe (voir afficher_page()). */
    ctx->vide = lv_label_create(parent);
    lv_obj_set_style_text_font(ctx->vide, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(ctx->vide, lv_color_hex(COULEUR_TEXTE_SECONDAIRE), 0);
    lv_obj_set_style_text_align(ctx->vide, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(ctx->vide, "No macros");
    lv_obj_set_pos(ctx->vide, MARGE, GRILLE_Y);
    lv_obj_set_size(ctx->vide, LARGEUR_CONTENU - 2 * MARGE, PAGINATION_Y - GRILLE_Y);
    lv_obj_add_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN);

    for (uint8_t emplacement = 0; emplacement < ECRAN_MACROS_PAGE_TAILLE; emplacement++) {
        /* Colonne unique (refonte jalon 3b) : x fixe a MARGE, seul y avance
         * d'une ligne a l'autre -- plus de division/modulo par un nombre de
         * colonnes, il n'y en a plus qu'une. */
        lv_coord_t x = MARGE;
        lv_coord_t y = GRILLE_Y + emplacement * (BOUTON_HAUTEUR + BOUTON_ECART_Y);

        ctx->boutons[emplacement] =
            bouton_creer(parent, "", x, y, BOUTON_LARGEUR, BOUTON_HAUTEUR, &lv_font_montserrat_14,
                         LV_TEXT_ALIGN_LEFT);
        ctx->labels[emplacement] = lv_obj_get_child(ctx->boutons[emplacement], 0);
        lv_obj_add_flag(ctx->boutons[emplacement], LV_OBJ_FLAG_HIDDEN);

        ctx->emplacements[emplacement].ctx = ctx;
        ctx->emplacements[emplacement].emplacement = emplacement;
        lv_obj_add_event_cb(ctx->boutons[emplacement], bouton_macro_cb, LV_EVENT_CLICKED,
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

    ctx->nb_filtrees = 0;
    ctx->macros_tronquees = false;
    ctx->donnees_perimees = false;
    ctx->page = 0;
}

static void ecran_macros_mettre_a_jour(const void *etat, bool donnees_perimees, void *contexte)
{
    ecran_macros_ctx_t *ctx = contexte;
    const etat_klipper_t *e = etat;
    if (ctx == NULL || e == NULL) {
        return;
    }

    /* Filtre les macros `_prefixees` -- "c'est un choix d'affichage, pas de
     * protocole" (moonraker_rpc.h) : l'etat les porte toutes, cet ecran-ci
     * decide lesquelles montrer. Copie DEFENSIVEMENT bornee AVANT de lire
     * le premier caractere (comme le nom de fichier de ecran_accueil.c) :
     * `e->macros[i]` peut occuper la totalite de KLIPPER_MACRO_NOM_MAX sans
     * octet nul (etat_klipper_t est un POD a champs fixes) -- une lecture
     * directe via strchr()/e->macros[i][0] est sure ICI (un seul octet), mais
     * la COPIE qui suit (snprintf) ne l'est que parce qu'elle est elle-meme
     * bornee a KLIPPER_MACRO_NOM_MAX en sortie comme en entree implicite
     * (snprintf lit `nom_borne`, deja NUL-termine par la copie explicite
     * ci-dessous, jamais `e->macros[i]` directement). */
    uint8_t nb_macros_source = e->nb_macros;
    if (nb_macros_source > KLIPPER_MACROS_MAX) {
        nb_macros_source = KLIPPER_MACROS_MAX; /* meme borne defensive que web_nb_macros_serialisables() */
    }
    ctx->nb_filtrees = 0;
    for (uint8_t i = 0; i < nb_macros_source; i++) {
        char nom_borne[KLIPPER_MACRO_NOM_MAX + 1];
        memcpy(nom_borne, e->macros[i], KLIPPER_MACRO_NOM_MAX);
        nom_borne[KLIPPER_MACRO_NOM_MAX] = '\0';
        if (nom_borne[0] == '\0' || nom_borne[0] == '_') {
            continue; /* macro cachee (convention Klipper) ou emplacement vide */
        }
        /* Copie bornee manuelle, PAS snprintf : `nom_borne` peut faire
         * jusqu'a KLIPPER_MACRO_NOM_MAX octets de long (le pire cas du test
         * de torture memoire, voir test_ecran_macros.c), ce que gcc ne peut
         * pas prouver borne a la compilation -- snprintf(...,
         * KLIPPER_MACRO_NOM_MAX, "%s", nom_borne) declenche alors
         * -Werror=format-truncation la ou aucune troncature reelle
         * n'inquiete personne ici (destination deja dimensionnee
         * EXPRES a KLIPPER_MACRO_NOM_MAX). */
        size_t longueur = strlen(nom_borne);
        if (longueur >= KLIPPER_MACRO_NOM_MAX) {
            longueur = KLIPPER_MACRO_NOM_MAX - 1;
        }
        memcpy(ctx->macros_filtrees[ctx->nb_filtrees], nom_borne, longueur);
        ctx->macros_filtrees[ctx->nb_filtrees][longueur] = '\0';
        ctx->nb_filtrees++;
    }
    ctx->macros_tronquees = e->macros_tronquees;
    ctx->donnees_perimees = donnees_perimees;

    afficher_page(ctx);
}

const ecran_desc_t ECRAN_MACROS = {
    .id = "macros",
    .titre = "Macros",
    .taille_contexte = sizeof(ecran_macros_ctx_t),
    .construire = ecran_macros_construire,
    .mettre_a_jour = ecran_macros_mettre_a_jour,
    .detruire = NULL,
};
