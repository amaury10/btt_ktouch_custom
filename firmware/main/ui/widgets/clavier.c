/* Implémentation : voir clavier.h pour le contrat.
 *
 * État module : `g_clavier` ci-dessous porte tout l'état d'un clavier
 * éventuellement ouvert — jamais alloué (malloc/calloc) par ce fichier, pour
 * la même raison que tuile_t/progression_t ne s'allouent pas elles-mêmes
 * (voir tuile.h) : un clavier est un singleton par contrat (« un seul
 * clavier à la fois », voir clavier_ouvrir() dans clavier.h), donc une
 * variable statique de fichier le représente entièrement sans qu'aucun
 * appelant n'ait jamais à porter ni libérer sa mémoire. C'est le même choix
 * que g_barre et consorts dans habillage.c. */
#include "clavier.h"

#include <stdio.h>
#include <string.h>

#include "journal.h"
#include "lvgl.h"

static const char *TAG = "clavier";

#define LARGEUR_ECRAN 800
#define HAUTEUR_ECRAN 480

#define COULEUR_FOND            0x1B2430
#define COULEUR_TEXTE_PRINCIPAL 0xFFFFFF

static struct {
    bool       ouvert;
    lv_obj_t  *racine;    /* conteneur plein écran, créé sur lv_layer_top() */
    lv_obj_t  *textarea;
    lv_obj_t  *clavier;   /* lv_keyboard */
    clavier_rappel_t rappel;
    void      *contexte;
    /* Valeur copiée hors de la textarea AVANT sa destruction (voir
     * evenement_clavier()) : le rappel reçoit un pointeur vers CE tampon,
     * jamais vers lv_textarea_get_text(), qui pointerait dans un objet LVGL
     * sur le point d'être détruit. Vit au-delà du rappel (variable statique),
     * mais son contenu est écrasé par le clavier suivant — un appelant qui a
     * besoin de la valeur plus tard doit la copier depuis le rappel, jamais
     * en garder le pointeur (voir clavier_rappel_t dans clavier.h). */
    char       valeur[CLAVIER_VALEUR_MAX];
} g_clavier;

static void evenement_clavier(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_READY && code != LV_EVENT_CANCEL) {
        return;
    }

    /* Garde de réentrance : LV_EVENT_READY/CANCEL peuvent, selon le chemin
     * qui les déclenche, être délivrés une seconde fois au même objet avant
     * que la destruction asynchrone programmée plus bas n'ait réellement
     * tourné (voir lv_keyboard_def_event_cb dans LVGL, qui renvoie le même
     * événement à la textarea après l'avoir envoyé au clavier). Sans cette
     * garde, un second passage relirait un `g_clavier` déjà remis à zéro et
     * appellerait le rappel une seconde fois — exactement le défaut que la
     * propriété 1 du brief interdit ("jamais deux"). */
    if (!g_clavier.ouvert) {
        return;
    }

    const char *resultat = NULL;
    if (code == LV_EVENT_READY) {
        /* Copié dans le tampon statique AVANT tout appel à
         * lv_obj_delete_async() ci-dessous : c'est la seule lecture de la
         * textarea de tout ce fichier, et elle a lieu pendant que l'objet
         * est encore parfaitement vivant. */
        const char *texte = lv_textarea_get_text(g_clavier.textarea);
        strncpy(g_clavier.valeur, texte != NULL ? texte : "", sizeof(g_clavier.valeur) - 1);
        g_clavier.valeur[sizeof(g_clavier.valeur) - 1] = '\0';
        resultat = g_clavier.valeur;
    }

    lv_obj_t *ancienne_racine = g_clavier.racine;
    clavier_rappel_t rappel   = g_clavier.rappel;
    void     *contexte        = g_clavier.contexte;

    /* Le clavier se referme AVANT l'appel du rappel (contrat de
     * clavier_ouvrir()) : l'état est remis à zéro et la destruction
     * programmée ICI, pas après rappel(...). C'est ce qui permet à un
     * rappel d'ouvrir un nouveau clavier (clavier_ouvrir() le verra fermé,
     * donc acceptera) ou d'empiler un écran sans jamais toucher aux objets
     * LVGL de ce clavier, déjà programmés à la destruction. */
    g_clavier.ouvert   = false;
    g_clavier.racine   = NULL;
    g_clavier.textarea = NULL;
    g_clavier.clavier  = NULL;
    g_clavier.rappel   = NULL;
    g_clavier.contexte = NULL;

    /* lv_obj_delete_async() : jamais lv_obj_delete() ici. Cet événement est
     * en train d'être distribué PAR l'objet qu'on détruirait (le clavier
     * lui-même, via lv_event_send() qui parcourt encore sa liste de
     * rappels) — le détruire de façon synchrone pendant sa propre
     * distribution d'événement est le usage-after-free classique de ce
     * genre de widget (voir le commentaire de tête du brief). La version
     * asynchrone programme la destruction réelle au prochain passage de
     * lv_timer_handler(), une fois cette distribution d'événement terminée. */
    lv_obj_delete_async(ancienne_racine);

    if (rappel != NULL) {
        rappel(resultat, contexte);
    }
}

void clavier_ouvrir(const char *titre, const char *valeur_initiale,
                     clavier_mode_t mode, clavier_rappel_t rappel, void *contexte)
{
    if (rappel == NULL) {
        JOURNAL_ERREUR(TAG, "clavier_ouvrir : rappel NULL, refuse");
        return;
    }
    if (g_clavier.ouvert) {
        JOURNAL_ALERTE(TAG, "clavier_ouvrir appele alors qu'un clavier est deja ouvert ; ignore");
        return;
    }

    /* lv_layer_top() plutôt qu'un enfant de lv_screen_active() : le clavier
     * doit passer par-dessus TOUT, y compris la barre d'état de l'habillage
     * (construite en premier enfant de l'écran actif, voir habillage.c) et
     * son bandeau de notifications (construit en dernier enfant, donc déjà
     * au sommet du z-order de l'écran actif). La couche supérieure de LVGL
     * est garantie au-dessus de l'écran actif entier, quel que soit l'ordre
     * de construction de ce dernier — pas de dépendance fragile sur le fait
     * que ce fichier soit construit après l'habillage. */
    g_clavier.racine = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(g_clavier.racine);
    lv_obj_set_size(g_clavier.racine, LARGEUR_ECRAN, HAUTEUR_ECRAN);
    lv_obj_set_pos(g_clavier.racine, 0, 0);
    lv_obj_set_style_bg_color(g_clavier.racine, lv_color_hex(COULEUR_FOND), 0);
    lv_obj_set_style_bg_opa(g_clavier.racine, LV_OPA_COVER, 0);
    lv_obj_clear_flag(g_clavier.racine, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(g_clavier.racine, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(g_clavier.racine, 16, 0);
    lv_obj_set_style_pad_row(g_clavier.racine, 12, 0);

    lv_obj_t *titre_label = lv_label_create(g_clavier.racine);
    /* montserrat_24 n'est pas compilée (voir simulateur/lv_conf.h,
     * LV_FONT_MONTSERRAT_24 vaut 0) : la 20, déjà utilisée par
     * habillage.c/tuile.c, est la plus grande disponible ici. */
    lv_obj_set_style_text_font(titre_label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(titre_label, lv_color_hex(COULEUR_TEXTE_PRINCIPAL), 0);
    lv_label_set_text(titre_label, titre != NULL ? titre : "");

    g_clavier.textarea = lv_textarea_create(g_clavier.racine);
    lv_obj_set_width(g_clavier.textarea, LV_PCT(100));
    lv_obj_set_height(g_clavier.textarea, 60);
    lv_textarea_set_one_line(g_clavier.textarea, true);
    /* -1 : la place du NUL, jamais comptée dans une longueur LVGL — même
     * borne que sizeof(g_clavier.valeur) - 1 ci-dessus. */
    lv_textarea_set_max_length(g_clavier.textarea, CLAVIER_VALEUR_MAX - 1);
    /* Tronqué nous-mêmes avant de le poser dans la textarea plutôt que de
     * compter sur l'application de LV_TEXTAREA_MAX_LENGTH par
     * lv_textarea_set_text() (non garantie ici, voir sa documentation qui ne
     * la promet que pour la saisie caractère par caractère) : le tampon
     * g_clavier.valeur, qui portera la valeur rendue au rappel en cas de
     * validation, est l'unique source de vérité sur la taille maximale. */
    char tampon_initial[CLAVIER_VALEUR_MAX];
    snprintf(tampon_initial, sizeof(tampon_initial), "%s", valeur_initiale != NULL ? valeur_initiale : "");
    lv_textarea_set_text(g_clavier.textarea, tampon_initial);

    g_clavier.clavier = lv_keyboard_create(g_clavier.racine);
    lv_obj_set_width(g_clavier.clavier, LV_PCT(100));
    /* flex_grow plutôt qu'une hauteur fixe : occupe tout ce qui reste sous
     * le titre et la textarea dans les 480 px de hauteur de `g_clavier.racine`
     * (une hauteur fixe, donc flex_grow a une base sur laquelle grandir) —
     * touches larges sur les 800x480 avec des doigts, exigence du brief. */
    lv_obj_set_flex_grow(g_clavier.clavier, 1);
    lv_keyboard_set_textarea(g_clavier.clavier, g_clavier.textarea);
    lv_keyboard_set_mode(g_clavier.clavier, mode == CLAVIER_NUMERIQUE
                                                 ? LV_KEYBOARD_MODE_NUMBER
                                                 : LV_KEYBOARD_MODE_TEXT_LOWER);

    /* LV_EVENT_READY (touche OK) et LV_EVENT_CANCEL (touche clavier/fermer)
     * sont les deux seules sorties, voir clavier.h : lv_keyboard fournit déjà
     * les deux, sur les deux modes utilisés ici (voir les cartes par défaut
     * dans lv_keyboard.c), il ne manque qu'un rappel qui les transforme en
     * valeur rendue. */
    lv_obj_add_event_cb(g_clavier.clavier, evenement_clavier, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(g_clavier.clavier, evenement_clavier, LV_EVENT_CANCEL, NULL);

    g_clavier.rappel   = rappel;
    g_clavier.contexte = contexte;
    g_clavier.ouvert   = true;
}
