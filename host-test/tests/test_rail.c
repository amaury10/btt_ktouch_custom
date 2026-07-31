/* Tâche 3 (refonte accueil/déplacer) : le rail persistant d'accès rapide --
 * voir rail.h pour le contrat. Modélisé sur test_selecteur_pas.c/
 * test_selecteur_choix.c (même idiome de test : création, exclusivité/
 * surlignage prouvé par le style RÉSOLU, pas juste lv_obj_has_state() -- même
 * leçon que la revue finale du jalon 2b), avec la différence propre à ce
 * widget : un clic ne bascule PAS lui-même un état "actif" (le rail ne fait
 * ni navigation ni gcode, voir le commentaire de tête de rail.h) -- il ne
 * fait que dispatcher vers `sur_action`, exactement comme le fait
 * bouton_pas_cb() pour selecteur_pas.c mais SANS la partie
 * add_state/remove_state. C'est `rail_marquer_actif()`, appelée par
 * l'intégration (tâche future), qui décide seule ce qui est visuellement
 * actif -- donc testée séparément d'un clic. */
#include <string.h>

#include "lvgl.h"

#include "petit_test.h"
#include "rail.h"

/* Même helper que pomper_transitions_style() dans test_selecteur_pas.c/
 * test_selecteur_choix.c (voir leur commentaire complet) : sans faire
 * avancer l'horloge LVGL, lv_obj_get_style_*_color()/_width() rendrait
 * encore la valeur de départ malgré un lv_obj_add_state()/remove_state()
 * déjà appliqué de façon synchrone. Redéfini ici plutôt que partagé : même
 * choix que les deux fichiers ci-dessus (aucune convention commune n'existe
 * pour un utilitaire de test partagé entre fichiers de ce harnais). */
static void pomper_transitions_style(void)
{
    for (int i = 0; i < 5; i++) {
        lv_tick_inc(100);
        lv_timer_handler();
    }
}

/* Callback de trace (forme imposée par le brief de la tâche) : enregistre la
 * dernière action reçue, le nombre total d'appels, et le `ctx` reçu -- ce
 * dernier prouve que rail_creer() transmet bien le pointeur fourni par
 * l'appelant plutôt qu'un pointeur interne au widget (même préoccupation que
 * `s` transmis à bouton_pas_cb() via lv_event_get_user_data()). */
static rail_action_t g_dernier;
static int g_appels;
static void *g_dernier_ctx;
static void trace(rail_action_t a, void *ctx)
{
    g_dernier = a;
    g_appels++;
    g_dernier_ctx = ctx;
}

void suite_rail(void)
{
    printf("suite : rail\n");

    /* --- création nominale ------------------------------------------------ */
    rail_t r;
    memset(&r, 0, sizeof(r));
    int contexte_bidon = 42;

    g_appels = 0;
    g_dernier_ctx = NULL;
    rail_creer(&r, lv_screen_active(), trace, &contexte_bidon);
    VERIFIER(r.racine != NULL);
    VERIFIER(lv_obj_get_child_count(r.racine) == RAIL_NB);
    for (int i = 0; i < RAIL_NB; i++) {
        VERIFIER(r.boutons[i] != NULL);
    }
    VERIFIER(r.sur_action == trace);
    VERIFIER(r.ctx == &contexte_bidon);

    /* --- clic sur STOP : dispatche exactement une fois, avec le bon ctx --- */
    lv_obj_send_event(r.boutons[RAIL_STOP], LV_EVENT_CLICKED, NULL);
    VERIFIER(g_appels == 1);
    VERIFIER(g_dernier == RAIL_STOP);
    VERIFIER(g_dernier_ctx == &contexte_bidon);

    /* --- clic sur ACCUEIL : dispatche l'action correspondante ------------- */
    lv_obj_send_event(r.boutons[RAIL_ACCUEIL], LV_EVENT_CLICKED, NULL);
    VERIFIER(g_appels == 2);
    VERIFIER(g_dernier == RAIL_ACCUEIL);

    /* --- clic sur HOME puis MACROS : chaque bouton dispatche SA propre
     * action (pas juste "toujours le dernier index clique precedemment") -- */
    lv_obj_send_event(r.boutons[RAIL_HOME], LV_EVENT_CLICKED, NULL);
    VERIFIER(g_appels == 3);
    VERIFIER(g_dernier == RAIL_HOME);

    lv_obj_send_event(r.boutons[RAIL_MACROS], LV_EVENT_CLICKED, NULL);
    VERIFIER(g_appels == 4);
    VERIFIER(g_dernier == RAIL_MACROS);

    /* --- re-clic sur le meme bouton : redispatche (pas d'exclusivite
     * mutuelle ici, contrairement a selecteur_pas/selecteur_choix -- le rail
     * ne memorise aucune "selection", voir le commentaire de tete) -------- */
    lv_obj_send_event(r.boutons[RAIL_MACROS], LV_EVENT_CLICKED, NULL);
    VERIFIER(g_appels == 5);
    VERIFIER(g_dernier == RAIL_MACROS);

    /* --- STOP visuellement distinct des trois autres des la creation,
     * INDEPENDAMMENT de tout appel a rail_marquer_actif() -- couleur RESOLUE,
     * meme lecon que test_selecteur_pas.c/test_selecteur_choix.c. --------- */
    pomper_transitions_style();
    lv_color_t couleur_stop = lv_obj_get_style_bg_color(r.boutons[RAIL_STOP], LV_PART_MAIN);
    lv_color_t couleur_nav = lv_obj_get_style_bg_color(r.boutons[RAIL_ACCUEIL], LV_PART_MAIN);
    VERIFIER(!lv_color_eq(couleur_stop, couleur_nav));
    VERIFIER(lv_color_eq(couleur_stop, lv_color_hex(0xE5484D)));
    /* les trois boutons de navigation partagent la meme couleur de base */
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(r.boutons[RAIL_HOME], LV_PART_MAIN), couleur_nav));
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(r.boutons[RAIL_MACROS], LV_PART_MAIN), couleur_nav));

    /* --- rail_marquer_actif() : surligne UN bouton, aucun autre ---------- */
    rail_marquer_actif(&r, RAIL_MACROS);
    pomper_transitions_style();
    VERIFIER(lv_obj_get_style_border_width(r.boutons[RAIL_MACROS], LV_PART_MAIN) > 0);
    VERIFIER(lv_obj_get_style_border_width(r.boutons[RAIL_ACCUEIL], LV_PART_MAIN) == 0);
    VERIFIER(lv_obj_get_style_border_width(r.boutons[RAIL_HOME], LV_PART_MAIN) == 0);
    VERIFIER(lv_obj_get_style_border_width(r.boutons[RAIL_STOP], LV_PART_MAIN) == 0);
    /* le surlignage ne change PAS la couleur de fond propre a STOP (rouge) --
     * verifie ici que marquer_actif(RAIL_STOP) ne "banalise" pas son rouge en
     * le remplacant par la meme couleur "actif" que les boutons de
     * navigation. */
    rail_marquer_actif(&r, RAIL_STOP);
    pomper_transitions_style();
    VERIFIER(lv_obj_get_style_border_width(r.boutons[RAIL_STOP], LV_PART_MAIN) > 0);
    VERIFIER(lv_obj_get_style_border_width(r.boutons[RAIL_MACROS], LV_PART_MAIN) == 0);
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(r.boutons[RAIL_STOP], LV_PART_MAIN),
                          lv_color_hex(0xE5484D)));

    /* --- RAIL_NB : n'importe -- aucun bouton actif ------------------------ */
    rail_marquer_actif(&r, RAIL_NB);
    pomper_transitions_style();
    for (int i = 0; i < RAIL_NB; i++) {
        VERIFIER(lv_obj_get_style_border_width(r.boutons[i], LV_PART_MAIN) == 0);
    }

    lv_obj_delete(r.racine);

    /* --- arguments NULL : no-op, jamais de dereferencement ---------------- */
    rail_t r_sans_parent;
    memset(&r_sans_parent, 0, sizeof(r_sans_parent));
    rail_creer(&r_sans_parent, NULL, trace, NULL);
    VERIFIER(r_sans_parent.racine == NULL);

    /* `r` NULL lui-meme : la seule preuve possible est l'absence de crash. */
    rail_creer(NULL, lv_screen_active(), trace, NULL);

    /* --- sur_action NULL a la creation : autorise, un clic ne plante pas et
     * n'invoque jamais trace_jamais_appelee() -- garde symetrique a celle de
     * bouton_pas_cb() sur `s`/`cible`. -------------------------------------- */
    rail_t r_sans_cb;
    memset(&r_sans_cb, 0, sizeof(r_sans_cb));
    rail_creer(&r_sans_cb, lv_screen_active(), NULL, NULL);
    VERIFIER(r_sans_cb.racine != NULL);
    VERIFIER((lv_obj_send_event(r_sans_cb.boutons[RAIL_STOP], LV_EVENT_CLICKED, NULL), true));
    lv_obj_delete(r_sans_cb.racine);

    /* --- rail_marquer_actif(NULL, ...) : no-op, jamais de dereferencement - */
    VERIFIER((rail_marquer_actif(NULL, RAIL_ACCUEIL), true));
}
