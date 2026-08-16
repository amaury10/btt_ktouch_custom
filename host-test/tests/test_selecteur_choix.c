/* Tâche 1 (refonte accueil/déplacer) : le sélecteur générique à N boutons
 * mutuellement exclusifs -- voir selecteur_choix.h pour le contrat. Modélisé
 * sur test_selecteur_pas.c (même widget, généralisé) : création,
 * exclusivité mutuelle prouvée par la couleur RÉSOLUE de LV_STATE_CHECKED
 * (même leçon que la revue finale du jalon 2b pour LV_STATE_DISABLED, voir
 * bouton_definir_desactive() dans ecran_macros.c : un simple
 * lv_obj_has_state() ne prouverait rien tant qu'aucun style local n'est
 * vérifié pixel-résolu), et les cas limites propres à `nb`/`defaut` que
 * test_selecteur_pas.c n'avait pas besoin de couvrir (nb fixe à 4 là-bas,
 * ici variable 2..8). */
#include <string.h>

#include "lvgl.h"

#include "petit_test.h"
#include "selecteur_choix.h"

/* Même helper que pomper_transitions_style() dans test_selecteur_pas.c/
 * test_commandes.c (voir leur commentaire complet) : selecteur_choix.c crée
 * ses boutons via lv_button_create(), qui garde le thème par défaut et sa
 * transition de couleur animée -- sans faire avancer l'horloge LVGL,
 * lv_obj_get_style_*_color() rendrait encore la couleur de départ malgré un
 * lv_obj_add_state()/remove_state() déjà appliqué de façon synchrone.
 * Redéfini ici plutôt que partagé : aucune convention commune n'existe pour
 * un utilitaire de test partagé entre fichiers de ce harnais. */
static void pomper_transitions_style(void)
{
    for (int i = 0; i < 5; i++) {
        lv_tick_inc(100);
        lv_timer_handler();
    }
}

static const char *const LIBELLES_VITESSE[3] = { "Lent", "Moyen", "Rapide" };

void suite_selecteur_choix(void)
{
    printf("suite : selecteur_choix\n");

    /* --- création nominale : 3 libellés, défaut = 1 --------------------- */
    selecteur_choix_t s;
    memset(&s, 0, sizeof(s));

    selecteur_choix_creer(&s, lv_screen_active(), LIBELLES_VITESSE, 3, 1);
    VERIFIER(s.racine != NULL);
    VERIFIER(s.nb == 3);
    VERIFIER(selecteur_choix_index(&s) == 1);
    VERIFIER(lv_obj_get_child_count(s.racine) == 3);
    for (int i = 0; i < 3; i++) {
        VERIFIER(s.boutons[i] != NULL);
    }
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(s.boutons[0], 0)), "Lent");
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(s.boutons[1], 0)), "Moyen");
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(s.boutons[2], 0)), "Rapide");

    pomper_transitions_style();
    lv_color_t couleur_active = lv_obj_get_style_bg_color(s.boutons[1], LV_PART_MAIN);
    lv_color_t couleur_normale = lv_obj_get_style_bg_color(s.boutons[0], LV_PART_MAIN);
    /* le bouton par défaut (index 1) est bien déjà marqué actif à la création */
    VERIFIER(!lv_color_eq(couleur_active, couleur_normale));
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(s.boutons[2], LV_PART_MAIN), couleur_normale));

    /* --- clic sur boutons[2] : devient actif, exactement UN seul -------- */
    lv_obj_send_event(s.boutons[2], LV_EVENT_CLICKED, NULL);
    VERIFIER(selecteur_choix_index(&s) == 2);

    pomper_transitions_style();
    /* boutons[2] porte maintenant EXACTEMENT la même couleur "active" que
     * boutons[1] portait avant -- même leçon de round-trip exact que
     * test_selecteur_pas.c/tuile_griser(). */
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(s.boutons[2], LV_PART_MAIN), couleur_active));
    /* l'ancien actif est revenu EXACTEMENT à la couleur normale */
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(s.boutons[1], LV_PART_MAIN), couleur_normale));
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(s.boutons[0], LV_PART_MAIN), couleur_normale));

    /* --- re-clic sur le même bouton : stable, pas de bascule ------------ */
    lv_obj_send_event(s.boutons[2], LV_EVENT_CLICKED, NULL);
    VERIFIER(selecteur_choix_index(&s) == 2);

    lv_obj_delete(s.racine);

    /* --- bornes de nb : hors [2, 8] -> no-op, racine reste NULL --------- */
    selecteur_choix_t s_nb1;
    memset(&s_nb1, 0, sizeof(s_nb1));
    selecteur_choix_creer(&s_nb1, lv_screen_active(), LIBELLES_VITESSE, 1, 0);
    VERIFIER(s_nb1.racine == NULL);

    static const char *const LIBELLES9[9] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };
    selecteur_choix_t s_nb9;
    memset(&s_nb9, 0, sizeof(s_nb9));
    selecteur_choix_creer(&s_nb9, lv_screen_active(), LIBELLES9, 9, 0);
    VERIFIER(s_nb9.racine == NULL);

    /* --- arguments NULL : no-op, jamais de déréférencement --------------- */
    selecteur_choix_t s_sans_parent;
    memset(&s_sans_parent, 0, sizeof(s_sans_parent));
    selecteur_choix_creer(&s_sans_parent, NULL, LIBELLES_VITESSE, 3, 0);
    VERIFIER(s_sans_parent.racine == NULL);

    selecteur_choix_t s_sans_libelles;
    memset(&s_sans_libelles, 0, sizeof(s_sans_libelles));
    selecteur_choix_creer(&s_sans_libelles, lv_screen_active(), NULL, 3, 0);
    VERIFIER(s_sans_libelles.racine == NULL);

    /* `s` NULL lui-même : la seule preuve possible est l'absence de crash
     * (rien à lire en retour) -- l'appel plante tout le harnais sinon. */
    selecteur_choix_creer(NULL, lv_screen_active(), LIBELLES_VITESSE, 3, 0);

    /* --- défaut hors bornes : clampé à nb - 1 ---------------------------- */
    selecteur_choix_t s_clamp;
    memset(&s_clamp, 0, sizeof(s_clamp));
    selecteur_choix_creer(&s_clamp, lv_screen_active(), LIBELLES_VITESSE, 3, 99);
    VERIFIER(s_clamp.racine != NULL);
    VERIFIER(selecteur_choix_index(&s_clamp) == 2);
    lv_obj_delete(s_clamp.racine);

    /* --- selecteur_choix_index(NULL) -> 0, jamais de déréférencement ----- */
    VERIFIER(selecteur_choix_index(NULL) == 0);

    /* --- borne haute valide : nb == 8 ------------------------------------ */
    static const char *const LIBELLES8[8] = { "1", "2", "3", "4", "5", "6", "7", "8" };
    selecteur_choix_t s8;
    memset(&s8, 0, sizeof(s8));
    selecteur_choix_creer(&s8, lv_screen_active(), LIBELLES8, 8, 0);
    VERIFIER(s8.racine != NULL);
    VERIFIER(s8.nb == 8);
    VERIFIER(lv_obj_get_child_count(s8.racine) == 8);
    for (int i = 0; i < 8; i++) {
        VERIFIER(s8.boutons[i] != NULL);
    }
    lv_obj_delete(s8.racine);
}
