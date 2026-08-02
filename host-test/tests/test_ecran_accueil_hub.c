/* Tache 5 (refonte accueil/deplacer) : l'ecran Accueil-hub -- tuiles de
 * temperature multi-tete (meme geometrie par palier que l'ancien
 * ecran_accueil_idle.c, supprime en tache 7) + grille de 6 cases de
 * menu, dont une seule (Deplacer) navigue reellement.
 *
 * Deux parties : la premiere construit ECRAN_ACCUEIL_HUB directement (calloc
 * du contexte a la taille du descripteur), meme choix que
 * suite_ecran_deplacer() pour la meme raison
 * (tester uniquement le contrat de cet ecran, pas celui de la pile de
 * navigation) -- c'est la partie qui a besoin d'un acces direct a `ctx` pour
 * lire les libelles/couleurs des tuiles. La seconde empile reellement
 * ECRAN_ACCUEIL_HUB via navigation_empiler() (meme technique que l'ancienne
 * suite_ecran_accueil_idle_macros(), supprimee en tache 7 avec le reste de
 * test_ecran_accueil_idle.c) : prouver un changement REEL de profondeur de
 * pile exige un vrai sommet de pile pour naviguer depuis, et retrouver le
 * bouton "Deplacer" par parcours de l'arbre LVGL (navigation_empiler() cree
 * toujours le conteneur du nouvel ecran comme DERNIER enfant du conteneur
 * racine -- voir navigation.c -- et ecran_accueil_hub_construire() cree
 * toujours zone_menu en DERNIER, apres les 9 cellules de temperature, avec
 * le bouton Deplacer comme PREMIER enfant de zone_menu -- voir son ordre de
 * construction).
 *
 * DOIT rester APRES suite_ecran_configuration() (habillage construit, voir
 * habillage_est_construit()) ET suite_commandes() (boucle simulee demarree,
 * voir source_etat_sim_est_demarre()) dans tests/main.c -- meme garde
 * d'ordonnancement que suite_ecran_deplacer(), meme si ce fichier n'envoie
 * en realite aucun gcode (le seul rappel de clic reel, menu_deplacer_cb(),
 * ne fait que navigation_empiler() -- garde conservee par coherence avec le
 * reste de ce depot plutot que par necessite stricte). */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "ecran_accueil_hub.h"
#include "etat_klipper.h"
#include "habillage.h"
#include "navigation.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

static size_t compter_cellules_visibles(const ecran_accueil_hub_ctx_t *ctx)
{
    size_t total = 0;
    for (size_t i = 0; i < ECRAN_ACCUEIL_HUB_CELLULES_MAX; i++) {
        if (!lv_obj_has_flag(ctx->cellules[i].racine, LV_OBJ_FLAG_HIDDEN)) {
            total++;
        }
    }
    return total;
}

void suite_ecran_accueil_hub(void)
{
    printf("suite : ecran accueil hub\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_accueil_hub() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    /* ---------------------------------------------------------------------
     * Partie 1 : construction directe -- widgets, valeurs des tuiles,
     * grisage. --------------------------------------------------------- */
    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_ACCUEIL_HUB.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_accueil_hub_ctx_t *ctx = (ecran_accueil_hub_ctx_t *)brut;
    ECRAN_ACCUEIL_HUB.construire(parent, ctx);

    /* --- tous les widgets sont crees ------------------------------------ */
    for (size_t i = 0; i < ECRAN_ACCUEIL_HUB_CELLULES_MAX; i++) {
        VERIFIER(ctx->cellules[i].racine != NULL);
        VERIFIER(ctx->cellules[i].nom != NULL);
        VERIFIER(ctx->cellules[i].valeur != NULL);
        VERIFIER(ctx->cellules[i].consigne != NULL);
    }
    VERIFIER(ctx->zone_menu != NULL);
    for (int i = 0; i < ECRAN_ACCUEIL_HUB_MENU_NB; i++) {
        VERIFIER(ctx->menu_boutons[i] != NULL);
    }
    /* aucune cellule visible tant que mettre_a_jour() n'a jamais tourne */
    VERIFIER(compter_cellules_visibles(ctx) == 0);

    /* --- 2 extrudeurs + plateau (palier MOYEN, brief step 1) : 3 tuiles -- */
    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    etat.nb_extrudeurs = 2;
    etat.extrudeurs[0].presente = true;
    etat.extrudeurs[0].actuelle = 205.0f;
    etat.extrudeurs[0].consigne = 210.0f;
    etat.extrudeurs[1].presente = true;
    etat.extrudeurs[1].actuelle = 45.0f;
    etat.extrudeurs[1].consigne = 0.0f;
    etat.plateau.presente = true;
    etat.plateau.actuelle = 59.5f;
    etat.plateau.consigne = 60.0f;
    etat.outil_actif = 0;
    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat, false, ctx);

    VERIFIER(compter_cellules_visibles(ctx) == 3);
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[0].nom), "T0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[0].valeur), "205.0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[0].consigne), "210.0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[1].nom), "T1");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[1].valeur), "45.0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[1].consigne), "0.0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[2].nom), "Bed");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[2].valeur), "59.5");
    VERIFIER_TEXTE(lv_label_get_text(ctx->cellules[2].consigne), "60.0");
    /* outil actif (T0) marque, T1 et le plateau ne le sont jamais */
    VERIFIER(lv_obj_get_style_border_width(ctx->cellules[0].racine, 0) > 0);
    VERIFIER(lv_obj_get_style_border_width(ctx->cellules[1].racine, 0) == 0);
    VERIFIER(lv_obj_get_style_border_width(ctx->cellules[2].racine, 0) == 0);
    /* palier MOYEN affiche la consigne (contrairement a COMPACT) */
    VERIFIER(!lv_obj_has_flag(ctx->cellules[0].consigne, LV_OBJ_FLAG_HIDDEN));

    /* --- 6 cases de menu, libelles attendus (voir MENU_DEFS, ecran_accueil_hub.c) */
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(ctx->menu_boutons[ECRAN_ACCUEIL_HUB_MENU_DEPLACER], 0)),
                   "Deplacer");
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(ctx->menu_boutons[ECRAN_ACCUEIL_HUB_MENU_TEMPERATURES], 0)),
                   "Temperatures");
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(ctx->menu_boutons[ECRAN_ACCUEIL_HUB_MENU_EXTRUDER], 0)),
                   "Extruder");
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(ctx->menu_boutons[ECRAN_ACCUEIL_HUB_MENU_VENTILATEURS], 0)),
                   "Ventilateurs");
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(ctx->menu_boutons[ECRAN_ACCUEIL_HUB_MENU_IMPRIMER], 0)),
                   "Imprimer");
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(ctx->menu_boutons[ECRAN_ACCUEIL_HUB_MENU_REGLAGES], 0)),
                   "Reglages");

    /* --- perime : grise (nom/valeur/consigne des 3 tuiles visibles), puis
     * redevient normal -- style RESOLU, meme lecon que
     * tuile_griser()/l'ancien ecran_accueil_idle.c (round-trip reversible). */
    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat, true, ctx);
    lv_color_t gris_valeur = lv_obj_get_style_text_color(ctx->cellules[2].valeur, 0);
    VERIFIER(lv_color_eq(gris_valeur, lv_color_hex(0x6B7280)));
    lv_color_t gris_nom = lv_obj_get_style_text_color(ctx->cellules[2].nom, 0);
    VERIFIER(lv_color_eq(gris_nom, lv_color_hex(0x6B7280)));

    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->cellules[2].valeur, 0), lv_color_hex(0x6B7280)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->cellules[2].nom, 0), lv_color_hex(0x6B7280)));

    lv_obj_delete(parent);
    free(brut);

    /* ---------------------------------------------------------------------
     * Partie 2 : navigation reelle -- empile ECRAN_ACCUEIL_HUB, clique
     * "Deplacer", verifie la pile. Voir le commentaire de tete de ce fichier
     * pour pourquoi une construction separee est necessaire ici. --------- */
    navigation_init(lv_screen_active()); /* pile vide, meme technique defensive que les autres suites */
    VERIFIER(navigation_empiler(&ECRAN_ACCUEIL_HUB) == ESP_OK);
    VERIFIER(navigation_profondeur() == 1);
    VERIFIER_TEXTE(navigation_id_courant(), "accueil_hub");

    navigation_mettre_a_jour(&etat, false);

    lv_obj_t *ecran = lv_screen_active();
    lv_obj_t *conteneur = lv_obj_get_child(ecran, lv_obj_get_child_count(ecran) - 1);
    VERIFIER(conteneur != NULL);
    /* 9 cellules de temperature + zone_menu = 10 enfants directs. */
    VERIFIER(lv_obj_get_child_count(conteneur) == ECRAN_ACCUEIL_HUB_CELLULES_MAX + 1);
    lv_obj_t *zone_menu = lv_obj_get_child(conteneur, lv_obj_get_child_count(conteneur) - 1);
    VERIFIER(zone_menu != NULL);
    VERIFIER(lv_obj_get_child_count(zone_menu) == ECRAN_ACCUEIL_HUB_MENU_NB);
    lv_obj_t *bouton_deplacer = lv_obj_get_child(zone_menu, ECRAN_ACCUEIL_HUB_MENU_DEPLACER);
    VERIFIER(bouton_deplacer != NULL);
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(bouton_deplacer, 0)), "Deplacer");

    lv_obj_send_event(bouton_deplacer, LV_EVENT_CLICKED, NULL);
    VERIFIER(navigation_profondeur() == 2);
    VERIFIER_TEXTE(navigation_id_courant(), "deplacer");

    navigation_depiler();
    VERIFIER(navigation_profondeur() == 1);

    /* ---------------------------------------------------------------------
     * Sous-projet "panneaux KlipperScreen", tache 8 : la case de menu
     * "Reglages" navigue desormais vers ECRAN_MENU_REGLAGES (id
     * "menu_reglages", le sous-menu Configuration -- PAS ECRAN_CONFIGURATION,
     * voir ecran_menu_reglages.h) -- plus aucune case no-op dans cette
     * grille. Meme technique de parcours/clic que "Deplacer" ci-dessus ;
     * verification exhaustive du contenu du sous-menu (les 12 cases) dans
     * test_ecran_menu_reglages.c, pas ici. --------------------------------- */
    lv_obj_t *bouton_reglages = lv_obj_get_child(zone_menu, ECRAN_ACCUEIL_HUB_MENU_REGLAGES);
    VERIFIER(bouton_reglages != NULL);
    lv_obj_send_event(bouton_reglages, LV_EVENT_CLICKED, NULL);
    VERIFIER(navigation_profondeur() == 2);
    VERIFIER_TEXTE(navigation_id_courant(), "menu_reglages");

    navigation_depiler();
    VERIFIER(navigation_profondeur() == 1);

    /* ---------------------------------------------------------------------
     * Sous-projet 2, tache 2 : la case de menu "Temperatures" ET une tuile
     * de temperature naviguent desormais vers ECRAN_TEMPERATURES -- meme
     * technique de parcours/clic que "Deplacer" ci-dessus. ---------------- */
    lv_obj_t *bouton_temperatures = lv_obj_get_child(zone_menu, ECRAN_ACCUEIL_HUB_MENU_TEMPERATURES);
    VERIFIER(bouton_temperatures != NULL);
    lv_obj_send_event(bouton_temperatures, LV_EVENT_CLICKED, NULL);
    VERIFIER(navigation_profondeur() == 2);
    VERIFIER_TEXTE(navigation_id_courant(), "temperatures");

    navigation_depiler();
    VERIFIER(navigation_profondeur() == 1);

    /* Tuile de temperature (premiere cellule) : cree AVANT zone_menu dans
     * ecran_accueil_hub_construire() (voir la boucle en tete de fonction),
     * donc premier enfant de `conteneur`. */
    lv_obj_t *tuile_0 = lv_obj_get_child(conteneur, 0);
    VERIFIER(tuile_0 != NULL);
    lv_obj_send_event(tuile_0, LV_EVENT_CLICKED, NULL);
    VERIFIER(navigation_profondeur() == 2);
    VERIFIER_TEXTE(navigation_id_courant(), "temperatures");

    navigation_depiler();
    VERIFIER(navigation_profondeur() == 1);

    /* ---------------------------------------------------------------------
     * Sous-projet 3 (decoupage KlipperScreen), tache 2 : la case de menu
     * "Extruder" navigue desormais vers ECRAN_EXTRUDER -- meme technique de
     * parcours/clic que "Deplacer"/"Temperatures" ci-dessus. --------------- */
    lv_obj_t *bouton_extruder = lv_obj_get_child(zone_menu, ECRAN_ACCUEIL_HUB_MENU_EXTRUDER);
    VERIFIER(bouton_extruder != NULL);
    lv_obj_send_event(bouton_extruder, LV_EVENT_CLICKED, NULL);
    VERIFIER(navigation_profondeur() == 2);
    VERIFIER_TEXTE(navigation_id_courant(), "extruder");

    navigation_depiler();
    VERIFIER(navigation_profondeur() == 1);

    /* ---------------------------------------------------------------------
     * Sous-projet 4 (decoupage KlipperScreen), tache 2 : la case de menu
     * "Ventilateurs" navigue desormais vers ECRAN_VENTILATEURS -- meme
     * technique de parcours/clic que "Deplacer"/"Temperatures"/"Extruder"
     * ci-dessus. --------------------------------------------------------- */
    lv_obj_t *bouton_ventilateurs = lv_obj_get_child(zone_menu, ECRAN_ACCUEIL_HUB_MENU_VENTILATEURS);
    VERIFIER(bouton_ventilateurs != NULL);
    lv_obj_send_event(bouton_ventilateurs, LV_EVENT_CLICKED, NULL);
    VERIFIER(navigation_profondeur() == 2);
    VERIFIER_TEXTE(navigation_id_courant(), "ventilateurs");

    navigation_depiler();
    VERIFIER(navigation_profondeur() == 1);

    /* ---------------------------------------------------------------------
     * Sous-projet 6 (browser de fichiers), tache 3 : la case de menu
     * "Imprimer" navigue desormais vers ECRAN_FICHIERS (id "fichiers", le
     * navigateur de fichiers) -- recablee depuis ECRAN_ACCUEIL (sous-projet 5,
     * tache 1) -- meme technique de parcours/clic que
     * "Deplacer"/"Temperatures"/"Extruder"/"Ventilateurs" ci-dessus. -------- */
    lv_obj_t *bouton_imprimer = lv_obj_get_child(zone_menu, ECRAN_ACCUEIL_HUB_MENU_IMPRIMER);
    VERIFIER(bouton_imprimer != NULL);
    lv_obj_send_event(bouton_imprimer, LV_EVENT_CLICKED, NULL);
    VERIFIER(navigation_profondeur() == 2);
    VERIFIER_TEXTE(navigation_id_courant(), "fichiers");

    navigation_depiler();
    VERIFIER(navigation_profondeur() == 1);
}
