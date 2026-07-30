/* Suite de la tâche 3 (jalon 3b) : l'écran d'accueil idle (voir
 * ecran_accueil_idle.h pour le contrat, et task-3-brief.md pour les
 * scénarios exigés). Construction directe (calloc du contexte à la taille
 * du descripteur, puis ECRAN_ACCUEIL_IDLE.construire()) plutôt que
 * navigation_empiler() -- même choix que test_ecran_accueil.c, pour la même
 * raison (tester uniquement le contrat de cet écran, pas celui de la pile
 * de navigation). */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "backend.h"
#include "ecran_accueil_idle.h"
#include "etat_klipper.h"
#include "habillage.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

static size_t compter_cellules_visibles(const ecran_accueil_idle_ctx_t *ctx)
{
    size_t total = 0;
    for (size_t i = 0; i < ECRAN_ACCUEIL_IDLE_CELLULES_MAX; i++) {
        if (!lv_obj_has_flag(ctx->cellules[i].racine, LV_OBJ_FLAG_HIDDEN)) {
            total++;
        }
    }
    return total;
}

void suite_ecran_accueil_idle(void)
{
    printf("suite : ecran accueil idle\n");

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_ACCUEIL_IDLE.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_accueil_idle_ctx_t *ctx = (ecran_accueil_idle_ctx_t *)brut;

    ECRAN_ACCUEIL_IDLE.construire(parent, ctx);
    /* tous les widgets sont crees */
    for (size_t i = 0; i < ECRAN_ACCUEIL_IDLE_CELLULES_MAX; i++) {
        VERIFIER(ctx->cellules[i].racine != NULL);
        VERIFIER(ctx->cellules[i].nom != NULL);
        VERIFIER(ctx->cellules[i].valeur != NULL);
        VERIFIER(ctx->cellules[i].consigne != NULL);
    }
    VERIFIER(ctx->position != NULL);
    VERIFIER(ctx->outil_actif_nom != NULL);
    VERIFIER(ctx->zone_controles != NULL);
    /* Tache 4 : pad de jog (six boutons) + selecteur de pas, remplacent le
     * placeholder "Controls" de la tache 3. */
    for (int i = 0; i < ECRAN_ACCUEIL_IDLE_JOG_NB; i++) {
        VERIFIER(ctx->jog_boutons[i] != NULL);
    }
    VERIFIER(ctx->selecteur_pas.racine != NULL);
    for (int i = 0; i < 4; i++) {
        VERIFIER(ctx->selecteur_pas.boutons[i] != NULL);
    }
    VERIFIER(ctx->selecteur_pas.index_actif == 1); /* 1 mm par defaut */
    VERIFIER(ctx->bouton_macros != NULL);
    VERIFIER(ctx->label_macros != NULL);
    /* aucune cellule visible tant que mettre_a_jour() n'a jamais tourne */
    VERIFIER(compter_cellules_visibles(ctx) == 0);

    etat_klipper_t etat;

    /* --- palier MONO : 1 extrudeur present + plateau = 2 cellules ------- */
    memset(&etat, 0, sizeof(etat));
    etat.nb_extrudeurs = 1;
    etat.extrudeurs[0].presente = true;
    etat.extrudeurs[0].actuelle = 205.0f;
    etat.extrudeurs[0].consigne = 210.0f;
    etat.plateau.presente = true;
    etat.plateau.actuelle = 60.0f;
    etat.plateau.consigne = 60.0f;
    etat.outil_actif = 0;
    VERIFIER((ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, false, ctx), true));
    VERIFIER(compter_cellules_visibles(ctx) == 2);
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[0].nom), "T0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[0].valeur), "205.0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[0].consigne), "210.0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[1].nom), "Bed");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[1].valeur), "60.0");
    /* outil actif (T0) marque, le plateau ne l'est jamais */
    VERIFIER(lv_obj_get_style_border_width(ctx->cellules[0].racine, 0) > 0);
    VERIFIER(lv_obj_get_style_border_width(ctx->cellules[1].racine, 0) == 0);
    /* MONO affiche la consigne (contrairement a COMPACT plus bas) */
    VERIFIER(!lv_obj_has_flag(ctx->cellules[0].consigne, LV_OBJ_FLAG_HIDDEN));

    /* --- palier MOYEN (U1, 4 tetes) : 4 extrudeurs + plateau = 5 cellules */
    memset(&etat, 0, sizeof(etat));
    etat.nb_extrudeurs = 4;
    for (uint8_t i = 0; i < 4; i++) {
        etat.extrudeurs[i].presente = true;
        etat.extrudeurs[i].actuelle = 24.0f;
        etat.extrudeurs[i].consigne = 0.0f;
    }
    etat.extrudeurs[2].actuelle = 205.0f;
    etat.extrudeurs[2].consigne = 210.0f;
    etat.outil_actif = 2;
    etat.plateau.presente = true;
    etat.plateau.actuelle = 60.0f;
    etat.plateau.consigne = 60.0f;
    VERIFIER((ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, false, ctx), true));
    VERIFIER(compter_cellules_visibles(ctx) == 5);
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[2].nom), "T2");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[2].valeur), "205.0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[4].nom), "Bed");
    /* seule la cellule 2 (T2 == outil_actif) porte la bordure */
    VERIFIER(lv_obj_get_style_border_width(ctx->cellules[0].racine, 0) == 0);
    VERIFIER(lv_obj_get_style_border_width(ctx->cellules[1].racine, 0) == 0);
    VERIFIER(lv_obj_get_style_border_width(ctx->cellules[2].racine, 0) > 0);
    VERIFIER(lv_obj_get_style_border_width(ctx->cellules[3].racine, 0) == 0);
    VERIFIER(lv_obj_get_style_border_width(ctx->cellules[4].racine, 0) == 0);
    /* une cellule au-dela de `total` (9 - 5 = 4 restantes) reste masquee */
    VERIFIER(lv_obj_has_flag(ctx->cellules[5].racine, LV_OBJ_FLAG_HIDDEN));

    /* --- palier COMPACT (8 tetes) : 8 extrudeurs + plateau = 9 cellules,
     * consigne masquee (spec : "au palier COMPACT ... pas de consigne
     * inline"). */
    memset(&etat, 0, sizeof(etat));
    etat.nb_extrudeurs = 8;
    for (uint8_t i = 0; i < 8; i++) {
        etat.extrudeurs[i].presente = true;
        etat.extrudeurs[i].actuelle = 24.0f;
        etat.extrudeurs[i].consigne = 0.0f;
    }
    etat.outil_actif = 5;
    etat.plateau.presente = true;
    etat.plateau.actuelle = 60.0f;
    VERIFIER((ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, false, ctx), true));
    VERIFIER(compter_cellules_visibles(ctx) == 9);
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[7].nom), "T7");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[8].nom), "Bed");
    VERIFIER(lv_obj_has_flag(ctx->cellules[0].consigne, LV_OBJ_FLAG_HIDDEN));
    VERIFIER(lv_obj_has_flag(ctx->cellules[8].consigne, LV_OBJ_FLAG_HIDDEN));
    VERIFIER(lv_obj_get_style_border_width(ctx->cellules[5].racine, 0) > 0);
    VERIFIER(lv_obj_get_style_border_width(ctx->cellules[8].racine, 0) == 0);

    /* --- temperature aberrante : "--" (formateur inchange) -------------- */
    etat.extrudeurs[0].actuelle = 999.0f;
    etat.plateau.actuelle = -999.0f;
    VERIFIER((ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, false, ctx), true));
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[0].valeur), "--");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[8].valeur), "--");

    /* --- position : axe non reference -> "--", axe reference -> valeur -- */
    memset(&etat, 0, sizeof(etat));
    etat.nb_extrudeurs = 1;
    etat.extrudeurs[0].presente = true;
    etat.plateau.presente = true;
    etat.position[0] = 123.4f;
    etat.position[1] = 56.7f;
    etat.position[2] = 1.0f;
    etat.axes_references = 0; /* aucun axe reference */
    VERIFIER((ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, false, ctx), true));
    VERIFIER_TEXTE(lv_label_get_text(ctx->position), "X:-- Y:-- Z:--");

    etat.axes_references = 0x1u | 0x2u | 0x4u; /* X, Y, Z references */
    VERIFIER((ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, false, ctx), true));
    VERIFIER_TEXTE(lv_label_get_text(ctx->position), "X:123.4 Y:56.7 Z:1.0");

    /* axe partiellement reference : seul Y l'est */
    etat.axes_references = 0x2u;
    VERIFIER((ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, false, ctx), true));
    VERIFIER_TEXTE(lv_label_get_text(ctx->position), "X:-- Y:56.7 Z:--");

    /* --- outil actif nomme, y compris sans extrudeur du tout ------------ */
    etat.axes_references = 0x1u | 0x2u | 0x4u;
    etat.outil_actif = 0;
    VERIFIER((ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, false, ctx), true));
    VERIFIER_TEXTE(lv_label_get_text(ctx->outil_actif_nom), "Active: T0");

    etat.nb_extrudeurs = 0;
    etat.extrudeurs[0].presente = false;
    VERIFIER((ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, false, ctx), true));
    VERIFIER_TEXTE(lv_label_get_text(ctx->outil_actif_nom), "Active: --");
    /* seul le plateau reste visible */
    VERIFIER(compter_cellules_visibles(ctx) == 1);

    /* --- perime : grise, puis redevient normal -- style RESOLU, meme
     * lecon que tuile_griser()/ecran_accueil.c (round-trip reversible).
     * Le plateau (seule cellule visible ici) est explicitement inclus,
     * meme raison que M-bed dans test_ecran_accueil.c : rien avant ce test
     * ne prouverait qu'une cellule "plateau seul" grise correctement. */
    VERIFIER((ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, true, ctx), true));
    lv_color_t gris_valeur = lv_obj_get_style_text_color(ctx->cellules[0].valeur, 0);
    VERIFIER(lv_color_eq(gris_valeur, lv_color_hex(0x6B7280)));
    lv_color_t gris_position = lv_obj_get_style_text_color(ctx->position, 0);
    VERIFIER(lv_color_eq(gris_position, lv_color_hex(0x6B7280)));

    VERIFIER((ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, false, ctx), true));
    lv_color_t normal_valeur = lv_obj_get_style_text_color(ctx->cellules[0].valeur, 0);
    VERIFIER(!lv_color_eq(normal_valeur, lv_color_hex(0x6B7280)));
    lv_color_t normal_position = lv_obj_get_style_text_color(ctx->position, 0);
    VERIFIER(!lv_color_eq(normal_position, lv_color_hex(0x6B7280)));

    lv_obj_delete(parent);
    free(brut);
}

/* ------------------------------------------------------------------------
 * Tache 4 (jalon 3b) : trace du seam pad de jog -> ui_commander(), et
 * grisage par axe. DOIT rester APRES suite_commandes() dans tests/main.c --
 * reutilise la boucle simulee deja demarree par elle (singleton
 * process-wide, voir le commentaire de tete de source_etat_sim.h), meme
 * garde d'ordonnancement que groupe_trace_seam() dans test_ecran_macros.c.
 * ------------------------------------------------------------------------ */

/* Meme helper que pomper_transitions_style() dans test_commandes.c/
 * test_ecran_macros.c (voir leur commentaire complet) : les boutons de jog
 * et du selecteur de pas sont crees via lv_button_create(), qui garde le
 * theme par defaut et sa transition de couleur animee. Redefini ici plutot
 * que partage, meme choix que les autres fichiers de ce harnais. */
static void pomper_transitions_style(void)
{
    for (int i = 0; i < 5; i++) {
        lv_tick_inc(100);
        lv_timer_handler();
    }
}

void suite_ecran_accueil_idle_jog(void)
{
    printf("suite : ecran accueil idle (trace du seam -> pad de jog)\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_accueil_idle_jog() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_ACCUEIL_IDLE.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_accueil_idle_ctx_t *ctx = (ecran_accueil_idle_ctx_t *)brut;
    ECRAN_ACCUEIL_IDLE.construire(parent, ctx);

    /* X et Y references, Z ne l'est pas (brief tache 4, step 2). */
    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    etat.nb_extrudeurs = 1;
    etat.extrudeurs[0].presente = true;
    etat.axes_references = 0x1u | 0x2u; /* X, Y -- pas Z */
    ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, false, ctx);

    /* --- Z desactive (RESOLU), X/Y actifs -- couleur RESOLUE, pas
     * seulement LV_STATE_DISABLED (meme lecon que partout ailleurs dans ce
     * harnais depuis la revue finale du jalon 2b). */
    VERIFIER(lv_obj_has_state(ctx->jog_boutons[ECRAN_ACCUEIL_IDLE_JOG_Z_POS], LV_STATE_DISABLED));
    VERIFIER(lv_obj_has_state(ctx->jog_boutons[ECRAN_ACCUEIL_IDLE_JOG_Z_NEG], LV_STATE_DISABLED));
    VERIFIER(!lv_obj_has_state(ctx->jog_boutons[ECRAN_ACCUEIL_IDLE_JOG_X_POS], LV_STATE_DISABLED));
    VERIFIER(!lv_obj_has_state(ctx->jog_boutons[ECRAN_ACCUEIL_IDLE_JOG_Y_POS], LV_STATE_DISABLED));
    pomper_transitions_style();
    lv_color_t fond_actif = lv_obj_get_style_bg_color(ctx->jog_boutons[ECRAN_ACCUEIL_IDLE_JOG_X_POS], LV_PART_MAIN);
    lv_color_t fond_desactive_z =
        lv_obj_get_style_bg_color(ctx->jog_boutons[ECRAN_ACCUEIL_IDLE_JOG_Z_POS], LV_PART_MAIN);
    VERIFIER(!lv_color_eq(fond_actif, fond_desactive_z));

    /* Un clic sur Z+ (desactive) n'envoie RIEN -- garde defensive de
     * jog_bouton_cb(), lv_obj_send_event() ne passe jamais par la
     * verification tactile de LV_STATE_DISABLED (lv_indev.c). */
    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->jog_boutons[ECRAN_ACCUEIL_IDLE_JOG_Z_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant);

    /* --- Clic sur X+ (pas par defaut = 1 mm) : BACKEND_ACTION_GCODE, script
     * contenant "G1 X1 F3000" (XY = 3000 mm/min, brief tache 4). Trace du
     * seam via source_etat_sim_derniere_commande() (tache 4, nouvel accesseur
     * de test -- voir son commentaire dans source_etat_sim.h). */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->jog_boutons[ECRAN_ACCUEIL_IDLE_JOG_X_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    char action[32];
    char arguments[192];
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "G1 X1 F3000") != NULL);
    /* le script est bien enveloppe dans un objet JSON valide, cle "script" */
    VERIFIER(strstr(arguments, "\"script\"") != NULL);
    source_etat_sim_cycle(); /* draine avant la suite */

    /* --- donnees_perimees=true : TOUS les boutons de jog se desactivent,
     * y compris X/Y qui etaient actifs juste au-dessus (spec tache 4 §7 :
     * "donnees_perimees OU axe non reference"). */
    ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, true, ctx);
    for (int i = 0; i < ECRAN_ACCUEIL_IDLE_JOG_NB; i++) {
        VERIFIER(lv_obj_has_state(ctx->jog_boutons[i], LV_STATE_DISABLED));
    }
    pomper_transitions_style();
    VERIFIER(!lv_color_eq(
        lv_obj_get_style_bg_color(ctx->jog_boutons[ECRAN_ACCUEIL_IDLE_JOG_X_POS], LV_PART_MAIN), fond_actif));

    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->jog_boutons[ECRAN_ACCUEIL_IDLE_JOG_X_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant); /* rien envoye, donnees perimees */

    /* --- redevient frais : X actif de nouveau, couleur RESOLUE exactement
     * celle de depart (round-trip complet, meme lecon que tuile_griser()). */
    ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_obj_has_state(ctx->jog_boutons[ECRAN_ACCUEIL_IDLE_JOG_X_POS], LV_STATE_DISABLED));
    pomper_transitions_style();
    VERIFIER(lv_color_eq(
        lv_obj_get_style_bg_color(ctx->jog_boutons[ECRAN_ACCUEIL_IDLE_JOG_X_POS], LV_PART_MAIN), fond_actif));

    /* --- le pas choisi change la distance : selecteur a 10 mm -> "X10" --- */
    lv_obj_send_event(ctx->selecteur_pas.boutons[2], LV_EVENT_CLICKED, NULL); /* index 2 = 10 mm */
    VERIFIER(ctx->selecteur_pas.index_actif == 2);
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->jog_boutons[ECRAN_ACCUEIL_IDLE_JOG_X_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "G1 X10 F3000") != NULL);
    source_etat_sim_cycle(); /* draine avant la suite */

    /* --- un axe negatif construit bien un signe negatif (X-, pas au pas
     * courant, 10 mm) -- "G1 X-10 F3000", pas juste "X10" avec un moins
     * ailleurs dans la chaine. */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->jog_boutons[ECRAN_ACCUEIL_IDLE_JOG_X_NEG], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "G1 X-10 F3000") != NULL);
    source_etat_sim_cycle();

    /* --- Z, quand reference, jog a la vitesse Z (600 mm/min) -- pas 3000. */
    etat.axes_references = 0x1u | 0x2u | 0x4u; /* X, Y, Z tous references */
    ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_obj_has_state(ctx->jog_boutons[ECRAN_ACCUEIL_IDLE_JOG_Z_POS], LV_STATE_DISABLED));
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->jog_boutons[ECRAN_ACCUEIL_IDLE_JOG_Z_POS], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "G1 Z10 F600") != NULL);
    source_etat_sim_cycle();

    lv_obj_delete(parent);
    free(brut);
}
