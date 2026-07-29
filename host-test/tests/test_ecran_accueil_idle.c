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

#include "ecran_accueil_idle.h"
#include "etat_klipper.h"
#include "petit_test.h"

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
    VERIFIER(ctx->label_controles != NULL);
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
