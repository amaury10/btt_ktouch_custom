/* Sous-projet "refonte IHM KlipperScreen", tache 4 : l'ecran Accueil-hub
 * reecrit en main_panel -- un resume compact en lecture seule (temperatures,
 * position + outil actif, vitesse/flux, mini-progression d'impression) et
 * une grille de CINQ tuiles de menu (Homing, Temperature, Actions,
 * Configuration, Print), chacune naviguant reellement vers son ecran dedie.
 * REMPLACE le contenu precedent de ce fichier (pool de tuiles de temperature
 * par palier + grille de 6 cases) -- voir l'historique git.
 *
 * Deux parties, meme technique que l'ancien fichier : la premiere construit
 * ECRAN_ACCUEIL_HUB directement (calloc du contexte a la taille du
 * descripteur) pour lire les libelles/couleurs du resume sans passer par la
 * pile de navigation ; la seconde empile reellement ECRAN_ACCUEIL_HUB via
 * navigation_empiler() pour prouver que chacune des cinq tuiles pousse le
 * bon ecran (parcours de l'arbre LVGL -- navigation_empiler() cree toujours
 * le conteneur du nouvel ecran comme DERNIER enfant du conteneur racine, et
 * ecran_accueil_hub_construire() cree toujours zone_menu en DERNIER, apres
 * les quatre lignes du resume).
 *
 * DOIT rester APRES suite_ecran_configuration() (habillage construit, voir
 * habillage_est_construit()) ET suite_commandes() (boucle simulee demarree,
 * voir source_etat_sim_est_demarre()) dans tests/main.c -- meme garde
 * d'ordonnancement que suite_ecran_deplacer(), meme si ce fichier n'envoie
 * en realite aucun gcode (les cinq rappels de clic de la grille de menu ne
 * font que navigation_empiler() -- garde conservee par coherence avec le
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

void suite_ecran_accueil_hub(void)
{
    printf("suite : ecran accueil hub (main_panel)\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_accueil_hub() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    /* ---------------------------------------------------------------------
     * Partie 1 : construction directe -- widgets, texte du resume, grisage.
     * ------------------------------------------------------------------- */
    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_ACCUEIL_HUB.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_accueil_hub_ctx_t *ctx = (ecran_accueil_hub_ctx_t *)brut;
    ECRAN_ACCUEIL_HUB.construire(parent, ctx);

    /* --- tous les widgets sont crees ------------------------------------ */
    VERIFIER(ctx->temperatures != NULL);
    VERIFIER(ctx->position != NULL);
    VERIFIER(ctx->vitesse_flux != NULL);
    VERIFIER(ctx->progression != NULL);
    VERIFIER(ctx->zone_menu != NULL);
    for (int i = 0; i < ECRAN_ACCUEIL_HUB_MENU_NB; i++) {
        VERIFIER(ctx->menu_boutons[i] != NULL);
    }
    /* la mini-progression reste masquee tant que mettre_a_jour() n'a jamais
     * tourne (aucune impression connue) */
    VERIFIER(lv_obj_has_flag(ctx->progression, LV_OBJ_FLAG_HIDDEN));

    /* --- etat : 2 extrudeurs + plateau, position homee sur les trois axes,
     * vitesse/flux non-standards, impression en cours a 42% (brief : "2
     * extruders present, plateau, position homed on X/Y/Z, vitesse_pct=100,
     * flux_pct=95, impression_en_cours=true, progression=0.42"). ---------- */
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
    etat.position[0] = 10.0f;
    etat.position[1] = 20.0f;
    etat.position[2] = 5.0f;
    etat.axes_references = 0x7u; /* X, Y et Z references */
    etat.vitesse_pct = 100;
    etat.flux_pct = 95;
    etat.impression_en_cours = true;
    etat.progression = 0.42f;

    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat, false, ctx);

    VERIFIER_TEXTE(lv_label_get_text(ctx->temperatures), "T0 205.0/210.0  T1 45.0/0.0  Bed 59.5/60.0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->position), "X:10.0 Y:20.0 Z:5.0  T0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->vitesse_flux), "Speed: 100%  Flow: 95%");
    VERIFIER_TEXTE(lv_label_get_text(ctx->progression), "Printing: 42%");
    /* impression en cours -- la mini-progression redevient visible */
    VERIFIER(!lv_obj_has_flag(ctx->progression, LV_OBJ_FLAG_HIDDEN));

    /* --- impression terminee : la mini-progression redisparait, re-evaluee
     * a CHAQUE appel (jamais un etat fige depuis le premier passage). ----- */
    etat_klipper_t etat_repos = etat;
    etat_repos.impression_en_cours = false;
    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat_repos, false, ctx);
    VERIFIER(lv_obj_has_flag(ctx->progression, LV_OBJ_FLAG_HIDDEN));

    /* --- perime : grise (les quatre lignes du resume), puis redevient
     * normal -- style RESOLU, meme lecon que tuile_griser()/l'ancien contenu
     * de ce fichier (round-trip reversible). ------------------------------ */
    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat, true, ctx);
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->temperatures, 0), lv_color_hex(0x6B7280)));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->position, 0), lv_color_hex(0x6B7280)));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->vitesse_flux, 0), lv_color_hex(0x6B7280)));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->progression, 0), lv_color_hex(0x6B7280)));

    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->temperatures, 0), lv_color_hex(0x6B7280)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->position, 0), lv_color_hex(0x6B7280)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->vitesse_flux, 0), lv_color_hex(0x6B7280)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->progression, 0), lv_color_hex(0x6B7280)));

    /* --- les cinq tuiles de menu portent les bons libelles --------------- */
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(ctx->menu_boutons[ECRAN_ACCUEIL_HUB_MENU_HOMING], 0)),
                   "Homing");
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(ctx->menu_boutons[ECRAN_ACCUEIL_HUB_MENU_TEMPERATURE], 0)),
                   "Temperature");
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(ctx->menu_boutons[ECRAN_ACCUEIL_HUB_MENU_ACTIONS], 0)),
                   "Actions");
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(ctx->menu_boutons[ECRAN_ACCUEIL_HUB_MENU_CONFIGURATION], 0)),
                   "Configuration");
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(ctx->menu_boutons[ECRAN_ACCUEIL_HUB_MENU_PRINT], 0)), "Print");

    lv_obj_delete(parent);
    free(brut);

    /* ---------------------------------------------------------------------
     * Partie 2 : navigation reelle -- empile ECRAN_ACCUEIL_HUB, clique
     * chacune des cinq tuiles, verifie la pile a chaque fois. Voir le
     * commentaire de tete de ce fichier pour pourquoi une construction
     * separee est necessaire ici. ----------------------------------------- */
    navigation_init(lv_screen_active()); /* pile vide, meme technique defensive que les autres suites */
    VERIFIER(navigation_empiler(&ECRAN_ACCUEIL_HUB) == ESP_OK);
    VERIFIER(navigation_profondeur() == 1);
    VERIFIER_TEXTE(navigation_id_courant(), "accueil_hub");

    navigation_mettre_a_jour(&etat, false);

    lv_obj_t *ecran = lv_screen_active();
    lv_obj_t *conteneur = lv_obj_get_child(ecran, lv_obj_get_child_count(ecran) - 1);
    VERIFIER(conteneur != NULL);
    /* 4 lignes de resume + zone_menu = 5 enfants directs. */
    VERIFIER(lv_obj_get_child_count(conteneur) == 5);
    lv_obj_t *zone_menu = lv_obj_get_child(conteneur, lv_obj_get_child_count(conteneur) - 1);
    VERIFIER(zone_menu != NULL);
    VERIFIER(lv_obj_get_child_count(zone_menu) == ECRAN_ACCUEIL_HUB_MENU_NB);

    /* Table des cinq destinations attendues (task-4-brief.md, verbatim) --
     * indice de la tuile, id de l'ecran cible qu'elle doit empiler. */
    const struct {
        uint8_t     indice;
        const char *id_attendu;
    } CIBLES[ECRAN_ACCUEIL_HUB_MENU_NB] = {
        { ECRAN_ACCUEIL_HUB_MENU_HOMING,        "homing" },
        { ECRAN_ACCUEIL_HUB_MENU_TEMPERATURE,   "temperatures" },
        { ECRAN_ACCUEIL_HUB_MENU_ACTIONS,       "actions" },
        { ECRAN_ACCUEIL_HUB_MENU_CONFIGURATION, "menu_reglages" },
        { ECRAN_ACCUEIL_HUB_MENU_PRINT,         "fichiers" },
    };

    for (size_t i = 0; i < ECRAN_ACCUEIL_HUB_MENU_NB; i++) {
        lv_obj_t *bouton = lv_obj_get_child(zone_menu, CIBLES[i].indice);
        VERIFIER(bouton != NULL);
        lv_obj_send_event(bouton, LV_EVENT_CLICKED, NULL);
        VERIFIER(navigation_profondeur() == 2);
        VERIFIER_TEXTE(navigation_id_courant(), CIBLES[i].id_attendu);

        navigation_depiler();
        VERIFIER(navigation_profondeur() == 1);
    }
}
