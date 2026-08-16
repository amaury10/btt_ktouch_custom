/* Tâche 4 (jalon 3b) : le sélecteur de pas -- voir selecteur_pas.h pour le
 * contrat. Création, valeur par défaut (1 mm), et exclusivité mutuelle
 * (un seul bouton actif à la fois, prouvé par la couleur RÉSOLUE de
 * LV_STATE_CHECKED -- même leçon que la revue finale du jalon 2b pour
 * LV_STATE_DISABLED, voir bouton_definir_desactive() dans ecran_macros.c :
 * un simple lv_obj_has_state() ne prouverait rien tant qu'aucun style local
 * n'est vérifié pixel-résolu). */
#include <string.h>

#include "lvgl.h"

#include "petit_test.h"
#include "selecteur_pas.h"

/* Même helper que pomper_transitions_style() dans test_commandes.c (voir son
 * commentaire complet là-bas) : selecteur_pas.c crée ses boutons via
 * lv_button_create(), qui garde le thème par défaut et sa transition de
 * couleur animée -- sans faire avancer l'horloge LVGL, lv_obj_get_style_*_color()
 * rendrait encore la couleur de départ malgré un lv_obj_add_state()/
 * remove_state() déjà appliqué de façon synchrone. Redéfini ici plutôt que
 * partagé : aucune convention commune n'existe pour un utilitaire de test
 * partagé entre fichiers de ce harnais (même choix que dernier_msgbox() dans
 * test_commandes.c). */
static void pomper_transitions_style(void)
{
    for (int i = 0; i < 5; i++) {
        lv_tick_inc(100);
        lv_timer_handler();
    }
}

void suite_selecteur_pas(void)
{
    printf("suite : selecteur_pas\n");

    /* Contexte alloué par l'écran, jamais par malloc (contrat du .h) : ici
     * une simple variable automatique, même technique que suite_tuile()/
     * suite_progression() dans test_widgets.c. */
    selecteur_pas_t s;
    memset(&s, 0, sizeof(s));

    selecteur_pas_creer(&s, lv_screen_active());
    VERIFIER(s.racine != NULL);
    for (int i = 0; i < 4; i++) {
        VERIFIER(s.boutons[i] != NULL);
    }
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(s.boutons[0], 0)), "0.1");
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(s.boutons[1], 0)), "1");
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(s.boutons[2], 0)), "10");
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(s.boutons[3], 0)), "100");

    /* --- defaut : 1 mm (index 1), un pas raisonnable ------------------- */
    VERIFIER(s.index_actif == 1);
    VERIFIER_FLOAT(selecteur_pas_valeur(&s), 1.0f, 0.0001f);

    pomper_transitions_style();
    lv_color_t couleur_active = lv_obj_get_style_bg_color(s.boutons[1], LV_PART_MAIN);
    lv_color_t couleur_normale = lv_obj_get_style_bg_color(s.boutons[0], LV_PART_MAIN);
    /* le bouton par defaut (1 mm) est bien deja marque actif a la creation */
    VERIFIER(!lv_color_eq(couleur_active, couleur_normale));
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(s.boutons[2], LV_PART_MAIN), couleur_normale));
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(s.boutons[3], LV_PART_MAIN), couleur_normale));

    /* --- clic sur boutons[2] (10 mm) : devient actif, exactement UN seul -- */
    lv_obj_send_event(s.boutons[2], LV_EVENT_CLICKED, NULL);
    VERIFIER(s.index_actif == 2);
    VERIFIER_FLOAT(selecteur_pas_valeur(&s), 10.0f, 0.0001f);

    pomper_transitions_style();
    /* boutons[2] porte maintenant EXACTEMENT la meme couleur "active" que
     * boutons[1] portait avant -- pas seulement "differente du normal", la
     * meme lecon de round-trip exact que tuile_griser(). */
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(s.boutons[2], LV_PART_MAIN), couleur_active));
    /* boutons[1] (l'ancien actif) est revenu EXACTEMENT a la couleur normale */
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(s.boutons[1], LV_PART_MAIN), couleur_normale));
    /* les deux jamais-actifs restent normaux */
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(s.boutons[0], LV_PART_MAIN), couleur_normale));
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(s.boutons[3], LV_PART_MAIN), couleur_normale));

    /* --- re-clic sur le meme bouton : stable, pas de bascule ----------- */
    lv_obj_send_event(s.boutons[2], LV_EVENT_CLICKED, NULL);
    VERIFIER(s.index_actif == 2);

    /* --- clic sur boutons[0] (0.1 mm) ----------------------------------- */
    lv_obj_send_event(s.boutons[0], LV_EVENT_CLICKED, NULL);
    VERIFIER(s.index_actif == 0);
    VERIFIER_FLOAT(selecteur_pas_valeur(&s), 0.1f, 0.0001f);
    pomper_transitions_style();
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(s.boutons[0], LV_PART_MAIN), couleur_active));
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(s.boutons[2], LV_PART_MAIN), couleur_normale));

    /* `s` NULL : selecteur_pas_valeur() rend le defaut documente (1 mm),
     * jamais un dereferencement. */
    VERIFIER_FLOAT(selecteur_pas_valeur(NULL), 1.0f, 0.0001f);

    lv_obj_delete(s.racine);
}
