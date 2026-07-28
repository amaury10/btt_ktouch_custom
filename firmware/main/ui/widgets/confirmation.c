/* Implémentation : voir confirmation.h pour le contrat.
 *
 * État module : `g_confirmation`, même justification que `g_clavier` dans
 * clavier.c (singleton, jamais alloué par ce fichier — voir aussi le
 * commentaire de tête de confirmation.h sur le refus du second appel). */
#include "confirmation.h"

#include "journal.h"
#include "lvgl.h"

static const char *TAG = "confirmation";

/* Même rouge que habillage_couleur_liaison(LIAISON_HORS_LIGNE) (voir
 * habillage.c) : un seul rouge "alerte" dans toute l'interface, pas une
 * seconde teinte qui laisserait croire à un sens différent. */
#define COULEUR_ROUGE_DESTRUCTIF 0xE74C3C

static struct {
    bool      ouvert;
    lv_obj_t *mbox;
    confirmation_rappel_t rappel;
    void     *contexte;
} g_confirmation;

/* Commun aux deux boutons : ferme le dialogue puis invoque le rappel, dans
 * cet ordre (voir le commentaire de evenement_clavier() dans clavier.c pour
 * la même règle appliquée au clavier — fermeture programmée et état remis à
 * zéro AVANT l'appel, jamais après). */
static void fermer_et_rappeler(bool confirme)
{
    /* Garde de réentrance : un double clic sur le même bouton avant que la
     * destruction asynchrone ci-dessous n'ait tourné ne doit pas appeler le
     * rappel une seconde fois (même propriété que clavier.c, voir son
     * commentaire pour le scénario précis qui la rend nécessaire). */
    if (!g_confirmation.ouvert) {
        return;
    }

    lv_obj_t *mbox                = g_confirmation.mbox;
    confirmation_rappel_t rappel  = g_confirmation.rappel;
    void     *contexte            = g_confirmation.contexte;

    g_confirmation.ouvert   = false;
    g_confirmation.mbox     = NULL;
    g_confirmation.rappel   = NULL;
    g_confirmation.contexte = NULL;

    /* lv_msgbox_close_async(), jamais lv_msgbox_close() : cette fonction est
     * appelée depuis l'événement LV_EVENT_CLICKED du bouton qu'on est en
     * train de faire disparaître (avec son fond de modale, voir
     * lv_msgbox_create() : le bouton est un descendant du fond détruit ici).
     * lv_msgbox_close_async() délègue à lv_obj_delete_async(), la même
     * garantie de destruction différée jusqu'au prochain lv_timer_handler()
     * que clavier.c applique explicitement — LVGL fournit ici directement la
     * variante sûre. */
    lv_msgbox_close_async(mbox);

    if (rappel != NULL) {
        rappel(confirme, contexte);
    }
}

static void bouton_annuler_cb(lv_event_t *e)
{
    (void)e;
    fermer_et_rappeler(false);
}

static void bouton_action_cb(lv_event_t *e)
{
    (void)e;
    fermer_et_rappeler(true);
}

void confirmation_ouvrir(const char *titre, const char *message,
                          const char *libelle_action, bool destructif,
                          confirmation_rappel_t rappel, void *contexte)
{
    if (rappel == NULL) {
        JOURNAL_ERREUR(TAG, "confirmation_ouvrir : rappel NULL, refuse");
        return;
    }
    if (g_confirmation.ouvert) {
        JOURNAL_ALERTE(TAG, "confirmation_ouvrir appele alors qu'un dialogue est deja ouvert ; ignore");
        return;
    }

    /* lv_msgbox_create(NULL) : LVGL crée elle-même un fond plein cadre sur
     * lv_layer_top() (le même calque supérieur qu'utilise clavier.c, voir
     * son commentaire dans clavier_ouvrir()) et centre la boîte dessus — pas
     * besoin de reproduire ici la mise en page que clavier.c fait à la
     * main pour son propre plein écran. */
    g_confirmation.mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(g_confirmation.mbox, titre != NULL ? titre : "");
    lv_msgbox_add_text(g_confirmation.mbox, message != NULL ? message : "");

    /* Bouton d'annulation ajouté EN PREMIER : test_clavier.c (suite
     * confirmation) suppose cet ordre pour retrouver les deux boutons dans
     * le pied du dialogue — documenté ici parce que c'est le seul endroit
     * qui le garantit. */
    lv_obj_t *bouton_annuler = lv_msgbox_add_footer_button(g_confirmation.mbox, "Cancel");
    lv_obj_add_event_cb(bouton_annuler, bouton_annuler_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *bouton_action =
        lv_msgbox_add_footer_button(g_confirmation.mbox, libelle_action != NULL ? libelle_action : "");
    lv_obj_add_event_cb(bouton_action, bouton_action_cb, LV_EVENT_CLICKED, NULL);

    if (destructif) {
        lv_obj_set_style_bg_color(bouton_action, lv_color_hex(COULEUR_ROUGE_DESTRUCTIF), 0);
        /* "N'est pas le bouton par défaut" (brief) : LV_STATE_FOCUS_KEY est
         * le seul état que le thème par défaut de LVGL utilise pour mettre
         * un bouton en avant (voir lv_theme_default.c, contour coloré sur ce
         * seul état) — on le pose sur l'annulation, jamais sur l'action
         * destructive, pour qu'aucune mise en avant ne penche vers elle. */
        lv_obj_add_state(bouton_annuler, LV_STATE_FOCUS_KEY);
    }

    g_confirmation.rappel   = rappel;
    g_confirmation.contexte = contexte;
    g_confirmation.ouvert   = true;
}
