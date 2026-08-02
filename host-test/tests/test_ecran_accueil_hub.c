/* Sous-projet "graphes de temperature", tache 3 : l'ecran Accueil-hub
 * REECRIT en deux colonnes (lignes de chauffants + resume + graphe a gauche,
 * cinq tuiles de menu a droite) -- voir ecran_accueil_hub.h/.c pour le
 * contrat et la mise en page.
 *
 * Deux parties, meme technique que l'ancien fichier (tache 4/5 de la refonte
 * IHM KlipperScreen) : la premiere construit ECRAN_ACCUEIL_HUB directement
 * (calloc du contexte a la taille du descripteur) pour lire les libelles/
 * couleurs/pointeurs de serie sans passer par la pile de navigation ; la
 * seconde empile reellement ECRAN_ACCUEIL_HUB via navigation_empiler() pour
 * prouver que chacune des cinq tuiles pousse le bon ecran (parcours de
 * l'arbre LVGL -- navigation_empiler() cree toujours le conteneur du nouvel
 * ecran comme DERNIER enfant du conteneur racine, et
 * ecran_accueil_hub_construire() cree toujours zone_menu en DERNIER, apres
 * les lignes de chauffants et les trois lignes de resume).
 *
 * DOIT rester APRES suite_ecran_configuration() (habillage construit, voir
 * habillage_est_construit()) ET suite_commandes() (boucle simulee demarree,
 * voir source_etat_sim_est_demarre()) dans tests/main.c -- meme garde
 * d'ordonnancement que suite_ecran_deplacer(). DOIT AUSSI rester juste APRES
 * suite_klipper_temp_historique() (voir son commentaire de tete dans
 * test_klipper_temp_historique.c) : klipper_temp_historique.h est un store
 * statique process-wide SANS remise a zero dans son contrat public -- cette
 * suite pousse elle-meme quelques points connus PAR-DESSUS l'etat (regime
 * permanent, tampon plein) que suite_klipper_temp_historique() laisse
 * derriere elle, plutot que d'exiger une remise a zero inexistante. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "ecran_accueil_hub.h"
#include "etat_klipper.h"
#include "habillage.h"
#include "klipper_temp_historique.h"
#include "navigation.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

void suite_ecran_accueil_hub(void)
{
    printf("suite : ecran accueil hub (main_panel, deux colonnes + graphe)\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_accueil_hub() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    /* --- etat connu : 2 extrudeurs presents + plateau, position homee sur
     * les trois axes, vitesse/flux non-standards, impression en cours a 42%
     * (memes valeurs que l'ancien test tache 4/5, brief : "2 extruders
     * present, plateau, position homed on X/Y/Z, vitesse_pct=100,
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

    /* --- seed du store d'historique (tache 1) : "quelques" pousser() d'un
     * etat connu, task-3-brief.md step 1 -- klipper_temp_historique_pousser()
     * ne lit que extrudeurs[]/plateau de `etat` (voir klipper_temp_historique.h),
     * donc reutiliser directement `etat` ci-dessus est fidele. Trois pousser
     * pour avoir un peu d'historique avant construire() (backfill), avec
     * derniere_gen capturee APRES par ecran_accueil_hub_construire(). ------ */
    uint32_t generation_avant_construire = klipper_temp_historique_generation();
    for (int k = 0; k < 3; k++) {
        klipper_temp_historique_pousser(&etat);
    }
    VERIFIER(klipper_temp_historique_generation() == generation_avant_construire + 3);

    /* ---------------------------------------------------------------------
     * Partie 1 : construction directe -- widgets, graphe, texte du resume,
     * grisage.
     * ------------------------------------------------------------------- */
    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_ACCUEIL_HUB.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_accueil_hub_ctx_t *ctx = (ecran_accueil_hub_ctx_t *)brut;
    ECRAN_ACCUEIL_HUB.construire(parent, ctx);

    /* --- tous les widgets sont crees ------------------------------------ */
    for (int i = 0; i < ECRAN_ACCUEIL_HUB_HEATER_LIGNES; i++) {
        VERIFIER(ctx->chauffant_nom[i] != NULL);
        VERIFIER(ctx->chauffant_valeur[i] != NULL);
    }
    VERIFIER(ctx->position != NULL);
    VERIFIER(ctx->vitesse_flux != NULL);
    VERIFIER(ctx->progression != NULL);
    VERIFIER(ctx->chart != NULL);
    VERIFIER(ctx->zone_menu != NULL);
    for (int i = 0; i < ECRAN_ACCUEIL_HUB_MENU_NB; i++) {
        VERIFIER(ctx->menu_boutons[i] != NULL);
    }
    /* la mini-progression reste masquee tant que mettre_a_jour() n'a jamais
     * tourne (aucune impression connue) */
    VERIFIER(lv_obj_has_flag(ctx->progression, LV_OBJ_FLAG_HIDDEN));

    /* --- le graphe a une serie PAR CHAUFFANT PRESENT (extrudeurs 0 et 1,
     * plateau = indice KLIPPER_EXTRUDEURS_MAX) -- aucune pour les indices
     * jamais mis a `presente` (2..7, voir suite_klipper_temp_historique()).
     * derniere_gen capturee APRES le backfill : doit deja valoir la
     * generation courante du store. ---------------------------------------- */
    VERIFIER(ctx->serie[0] != NULL);
    VERIFIER(ctx->serie[1] != NULL);
    for (int i = 2; i < KLIPPER_EXTRUDEURS_MAX; i++) {
        VERIFIER(ctx->serie[i] == NULL);
    }
    VERIFIER(ctx->serie[KLIPPER_EXTRUDEURS_MAX] != NULL); /* plateau */
    VERIFIER(ctx->derniere_gen == klipper_temp_historique_generation());

    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat, false, ctx);

    VERIFIER_TEXTE(lv_label_get_text(ctx->chauffant_nom[0]), "T0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->chauffant_valeur[0]), "205.0/210.0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->chauffant_nom[1]), "T1");
    VERIFIER_TEXTE(lv_label_get_text(ctx->chauffant_valeur[1]), "45.0/0.0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->chauffant_nom[2]), "Bed");
    VERIFIER_TEXTE(lv_label_get_text(ctx->chauffant_valeur[2]), "59.5/60.0");
    for (int i = 0; i < ECRAN_ACCUEIL_HUB_HEATER_LIGNES; i++) {
        VERIFIER(!lv_obj_has_flag(ctx->chauffant_nom[i], LV_OBJ_FLAG_HIDDEN));
        VERIFIER(!lv_obj_has_flag(ctx->chauffant_valeur[i], LV_OBJ_FLAG_HIDDEN));
    }
    VERIFIER_TEXTE(lv_label_get_text(ctx->position), "X:10.0 Y:20.0 Z:5.0  T0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->vitesse_flux), "Speed: 100%  Flow: 95%");
    VERIFIER_TEXTE(lv_label_get_text(ctx->progression), "Printing: 42%");
    /* impression en cours -- la mini-progression redevient visible */
    VERIFIER(!lv_obj_has_flag(ctx->progression, LV_OBJ_FLAG_HIDDEN));

    /* --- 1 seul extrudeur present : la ligne T1 disparait (masquee, jamais
     * juste videe), Bed glisse en ligne 1. -------------------------------- */
    etat_klipper_t etat_mono = etat;
    etat_mono.nb_extrudeurs = 1;
    etat_mono.extrudeurs[1].presente = false;
    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat_mono, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->chauffant_nom[0]), "T0");
    VERIFIER_TEXTE(lv_label_get_text(ctx->chauffant_nom[1]), "Bed");
    VERIFIER(!lv_obj_has_flag(ctx->chauffant_nom[1], LV_OBJ_FLAG_HIDDEN));
    VERIFIER(lv_obj_has_flag(ctx->chauffant_nom[2], LV_OBJ_FLAG_HIDDEN));
    VERIFIER(lv_obj_has_flag(ctx->chauffant_valeur[2], LV_OBJ_FLAG_HIDDEN));
    /* retour a l'etat complet pour la suite */
    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_obj_has_flag(ctx->chauffant_nom[2], LV_OBJ_FLAG_HIDDEN));

    /* --- impression terminee : la mini-progression redisparait, re-evaluee
     * a CHAQUE appel (jamais un etat fige depuis le premier passage). ----- */
    etat_klipper_t etat_repos = etat;
    etat_repos.impression_en_cours = false;
    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat_repos, false, ctx);
    VERIFIER(lv_obj_has_flag(ctx->progression, LV_OBJ_FLAG_HIDDEN));

    /* --- perime : grise (chauffants + resume), puis redevient normal --
     * style RESOLU, meme lecon que tuile_griser()/l'ancien contenu de ce
     * fichier (round-trip reversible). La grille de menu, elle, N'EST JAMAIS
     * grisee (spec) : le libelle de la premiere tuile reste blanc dans les
     * DEUX cas. ------------------------------------------------------- */
    lv_obj_t *label_tuile_homing = lv_obj_get_child(ctx->menu_boutons[ECRAN_ACCUEIL_HUB_MENU_HOMING], 0);
    lv_color_t couleur_tuile_avant = lv_obj_get_style_text_color(label_tuile_homing, 0);

    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat, true, ctx);
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->chauffant_nom[0], 0), lv_color_hex(0x6B7280)));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->chauffant_valeur[0], 0), lv_color_hex(0x6B7280)));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->position, 0), lv_color_hex(0x6B7280)));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->vitesse_flux, 0), lv_color_hex(0x6B7280)));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->progression, 0), lv_color_hex(0x6B7280)));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(label_tuile_homing, 0), couleur_tuile_avant));

    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->chauffant_nom[0], 0), lv_color_hex(0x6B7280)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->chauffant_valeur[0], 0), lv_color_hex(0x6B7280)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->position, 0), lv_color_hex(0x6B7280)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->vitesse_flux, 0), lv_color_hex(0x6B7280)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->progression, 0), lv_color_hex(0x6B7280)));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(label_tuile_homing, 0), couleur_tuile_avant));

    /* ---------------------------------------------------------------------
     * Garde-fou de rafraichissement du graphe (task-3-brief.md, coeur de la
     * tache) : mettre_a_jour() SANS nouvelle generation ne doit AJOUTER
     * AUCUN point -- verifie en clonant tout le tableau Y brut de la serie 0
     * (lv_chart_get_y_array(), KLIPPER_HISTO_POINTS entiers) avant/apres,
     * PLUS l'egalite de ctx->derniere_gen. Avec une nouvelle generation
     * (un pousser() de plus), le tableau DOIT changer. --------------------
     * ------------------------------------------------------------------- */
    int32_t *tableau_y = lv_chart_get_y_array(ctx->chart, ctx->serie[0]);
    VERIFIER(tableau_y != NULL);
    int32_t instantane_avant[KLIPPER_HISTO_POINTS];
    memcpy(instantane_avant, tableau_y, sizeof(instantane_avant));
    uint32_t gen_avant = ctx->derniere_gen;

    /* Aucun nouveau pousser() : generation() inchangee -> le graphe ne doit
     * pas bouger, quel que soit le nombre d'appels a mettre_a_jour(). ----- */
    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat, false, ctx);
    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat, false, ctx);
    VERIFIER(ctx->derniere_gen == gen_avant);
    VERIFIER(memcmp(instantane_avant, tableau_y, sizeof(instantane_avant)) == 0);

    /* Un nouveau pousser() : generation() avance -> le prochain
     * mettre_a_jour() DOIT ajouter le nouveau point et memoriser la
     * nouvelle generation. -------------------------------------------------*/
    etat_klipper_t etat_plus_chaud = etat;
    etat_plus_chaud.extrudeurs[0].actuelle = 206.0f;
    klipper_temp_historique_pousser(&etat_plus_chaud);
    uint32_t gen_apres_pousser = klipper_temp_historique_generation();
    VERIFIER(gen_apres_pousser == gen_avant + 1);

    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat, false, ctx);
    VERIFIER(ctx->derniere_gen == gen_apres_pousser);
    VERIFIER(memcmp(instantane_avant, tableau_y, sizeof(instantane_avant)) != 0);

    /* ---------------------------------------------------------------------
     * Regression : un chauffant qui devient PRESENT seulement APRES
     * construire() doit quand meme recevoir une serie -- c'est exactement le
     * cas du BOOT REEL (app_main.c empile ECRAN_ACCUEIL_HUB avant que le
     * minuteur d'echantillonnage n'ait jamais pousse un point, voir le
     * commentaire de chart_ajouter_serie() dans ecran_accueil_hub.c) :
     * decouvert en capturant le simulateur pendant cette tache (graphe
     * reste plat sans ce rattrapage). L'indice 3 (T3) n'a jamais ete
     * present dans aucun pousser() jusqu'ici (seuls 0, 1 et le plateau le
     * sont) -- verifie donc bien le chemin de creation TARDIVE de la serie,
     * pas un doublon du cas nominal deja couvert plus haut. ---------------
     * ------------------------------------------------------------------- */
    VERIFIER(ctx->serie[3] == NULL);
    etat_klipper_t etat_t3 = etat;
    etat_t3.nb_extrudeurs = 4;
    etat_t3.extrudeurs[3].presente = true;
    etat_t3.extrudeurs[3].actuelle = 77.0f;
    etat_t3.extrudeurs[3].consigne = 80.0f;
    klipper_temp_historique_pousser(&etat_t3);
    VERIFIER(klipper_temp_historique_serie_presente(3) == true);

    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat, false, ctx);
    VERIFIER(ctx->serie[3] != NULL);
    int16_t dernier_t3 = -1;
    VERIFIER(klipper_temp_historique_dernier(3, &dernier_t3));
    VERIFIER(dernier_t3 == 77);

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
    /* (ECRAN_ACCUEIL_HUB_HEATER_LIGNES * 2) paires nom+valeur + 3 lignes de
     * resume + 1 chart + 1 zone_menu. */
    VERIFIER(lv_obj_get_child_count(conteneur) == (uint32_t)(ECRAN_ACCUEIL_HUB_HEATER_LIGNES * 2 + 3 + 1 + 1));
    lv_obj_t *zone_menu = lv_obj_get_child(conteneur, lv_obj_get_child_count(conteneur) - 1);
    VERIFIER(zone_menu != NULL);
    VERIFIER(lv_obj_get_child_count(zone_menu) == ECRAN_ACCUEIL_HUB_MENU_NB);

    /* Table des cinq destinations attendues (task-4-brief.md de la refonte
     * IHM KlipperScreen, verbatim, inchangee par cette tache) -- indice de
     * la tuile, id de l'ecran cible qu'elle doit empiler. */
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
