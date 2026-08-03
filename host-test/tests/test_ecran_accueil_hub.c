/* Sous-projet "graphes de temperature", tache 3 : l'ecran Accueil-hub
 * REECRIT en deux colonnes (lignes de chauffants + graphe a gauche, cinq
 * tuiles de menu a droite) plus une ligne de statut pleine largeur SOUS les
 * deux colonnes (position + outil actif + vitesse/flux + progression, tache
 * de suivi refinement 2) -- voir ecran_accueil_hub.h/.c pour le contrat et la
 * mise en page.
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
 * les lignes de chauffants, la ligne de statut et le chart).
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

#include "backend.h"
#include "ecran_accueil_hub.h"
#include "etat_klipper.h"
#include "habillage.h"
#include "klipper_temp_historique.h"
#include "navigation.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

/* Retrouve le dernier enfant de lv_layer_top() -- meme helper que
 * dernier_enfant_calque_superieur() dans test_ecran_temperatures.c/
 * test_clavier.c : le clavier (clavier.h) se construit toujours sur cette
 * couche, par-dessus l'ecran actif entier. Copie locale plutot que partagee,
 * meme choix que le reste de ce harnais. */
static lv_obj_t *dernier_enfant_calque_superieur(void)
{
    lv_obj_t *calque = lv_layer_top();
    uint32_t n = lv_obj_get_child_count(calque);
    if (n == 0) {
        return NULL;
    }
    return lv_obj_get_child(calque, n - 1);
}

/* Retrouve le premier descendant DIRECT de `parent` dont la classe LVGL est
 * `classe` -- copie locale du meme helper que test_ecran_temperatures.c. */
static lv_obj_t *enfant_de_classe(lv_obj_t *parent, const lv_obj_class_t *classe)
{
    if (parent == NULL) {
        return NULL;
    }
    uint32_t n = lv_obj_get_child_count(parent);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *enfant = lv_obj_get_child(parent, i);
        if (lv_obj_check_type(enfant, classe)) {
            return enfant;
        }
    }
    return NULL;
}

/* Retrouve le `lv_label_t` enfant d'UN bouton de ligne de chauffant (NOM ou
 * VALEUR) -- copie locale de chauffant_bouton_label() (ecran_accueil_hub.c,
 * prive a ce fichier) : depuis la tache de suivi "chauffants en boutons",
 * `ctx->chauffant_nom[i]`/`ctx->chauffant_valeur[i]` sont des BOUTONS, le
 * texte/la couleur relus par ce test vivent sur leur UNIQUE label enfant,
 * jamais sur le bouton lui-meme -- meme convention que
 * `lv_obj_get_child(ctx->menu_boutons[...], 0)` deja utilise plus bas pour
 * les tuiles de menu. */
static lv_obj_t *chauffant_bouton_label(lv_obj_t *bouton)
{
    return lv_obj_get_child(bouton, 0);
}

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
    VERIFIER(ctx->zone_chauffants != NULL);
    /* Conteneur SCROLLABLE (spec de ce refinement : "with a visible
     * scrollbar") -- verifie ici le flag SEUL, jamais un pixel (voir le
     * commentaire de tete du fichier) ; le defilement vertical proprement dit
     * (LV_DIR_VER) et le mode de barre (LV_SCROLLBAR_MODE_AUTO) sont du
     * ressort de construire()/ecran_accueil_hub.c, non re-verifies ici. ---- */
    VERIFIER(lv_obj_has_flag(ctx->zone_chauffants, LV_OBJ_FLAG_SCROLLABLE));
    for (int i = 0; i < ECRAN_ACCUEIL_HUB_HEATER_LIGNES; i++) {
        VERIFIER(ctx->chauffant_nom[i] != NULL);
        VERIFIER(ctx->chauffant_valeur[i] != NULL);
        /* Chaque bouton NOM/VALEUR est cliquable (lv_button_create() le pose
         * par defaut) et porte un UNIQUE label enfant (chauffant_bouton_label(),
         * jamais NULL) -- verifie pour TOUT le pool, pas seulement les lignes
         * presentes dans l'etat de test ci-dessous. */
        VERIFIER(lv_obj_has_flag(ctx->chauffant_nom[i], LV_OBJ_FLAG_CLICKABLE));
        VERIFIER(lv_obj_has_flag(ctx->chauffant_valeur[i], LV_OBJ_FLAG_CLICKABLE));
        VERIFIER(chauffant_bouton_label(ctx->chauffant_nom[i]) != NULL);
        VERIFIER(chauffant_bouton_label(ctx->chauffant_valeur[i]) != NULL);
    }
    VERIFIER(ctx->statut != NULL);
    VERIFIER(ctx->chart != NULL);
    VERIFIER(ctx->zone_menu != NULL);
    for (int i = 0; i < ECRAN_ACCUEIL_HUB_MENU_NB; i++) {
        VERIFIER(ctx->menu_boutons[i] != NULL);
    }
    /* la ligne de statut demarre vide (mettre_a_jour() n'a jamais tourne) --
     * tache de suivi (refinement 2) : contrairement a l'ancienne ligne
     * `progression`, ce widget n'a plus de flag LV_OBJ_FLAG_HIDDEN a suivre,
     * le segment de progression est simplement absent du texte. */
    VERIFIER_TEXTE(lv_label_get_text(ctx->statut), "");

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

    VERIFIER_TEXTE(lv_label_get_text(chauffant_bouton_label(ctx->chauffant_nom[0])), "T0");
    VERIFIER_TEXTE(lv_label_get_text(chauffant_bouton_label(ctx->chauffant_valeur[0])), "205.0/210.0");
    VERIFIER_TEXTE(lv_label_get_text(chauffant_bouton_label(ctx->chauffant_nom[1])), "T1");
    VERIFIER_TEXTE(lv_label_get_text(chauffant_bouton_label(ctx->chauffant_valeur[1])), "45.0/0.0");
    VERIFIER_TEXTE(lv_label_get_text(chauffant_bouton_label(ctx->chauffant_nom[2])), "Bed");
    VERIFIER_TEXTE(lv_label_get_text(chauffant_bouton_label(ctx->chauffant_valeur[2])), "59.5/60.0");
    /* 3 chauffants presents dans `etat` (T0, T1, Bed) -- lignes 0..2
     * demasquees et CONTIGUES (voir la repositionnement contigu de
     * mettre_a_jour(), commentaire dans le .c), lignes 3..HEATER_LIGNES-1
     * (jusqu'a 9) restees masquees -- c'est PRECISEMENT ce qui garantit
     * l'absence de trou de defilement dans zone_chauffants (voir le
     * commentaire de tete du .h). */
    for (int i = 0; i < 3; i++) {
        VERIFIER(!lv_obj_has_flag(ctx->chauffant_nom[i], LV_OBJ_FLAG_HIDDEN));
        VERIFIER(!lv_obj_has_flag(ctx->chauffant_valeur[i], LV_OBJ_FLAG_HIDDEN));
    }
    for (int i = 3; i < ECRAN_ACCUEIL_HUB_HEATER_LIGNES; i++) {
        VERIFIER(lv_obj_has_flag(ctx->chauffant_nom[i], LV_OBJ_FLAG_HIDDEN));
        VERIFIER(lv_obj_has_flag(ctx->chauffant_valeur[i], LV_OBJ_FLAG_HIDDEN));
    }
    /* --- ligne de statut consolidee (tache de suivi, refinement 2) :
     * position + outil actif + vitesse/flux + progression sur UNE seule
     * ligne pleine largeur, sous les deux colonnes -- remplace les trois
     * VERIFIER_TEXTE separes de l'ancien resume (ctx->position/
     * ->vitesse_flux/->progression, desormais fusionnes dans ctx->statut).
     * Impression en cours (etat de test : 42%) -- le segment "Printing: 42%"
     * est present. */
    VERIFIER_TEXTE(lv_label_get_text(ctx->statut), "X:10.0 Y:20.0 Z:5.0  T0  Speed: 100%  Flow: 95%  Printing: 42%");

    /* ---------------------------------------------------------------------
     * Tache 4 (task-4-brief.md) : taper le label VALEUR d'une ligne de
     * chauffant ouvre le clavier numerique pour editer sa consigne -- modele
     * direct de suite_ecran_temperatures() (test_ecran_temperatures.c) pour
     * la partie clavier. Le label NOM, lui, est cliquable pour un raccourci
     * DIFFERENT (tache 5, plus bas dans cette suite) -- les deux flags sont
     * donc VRAIS ici, un seul geste (clavier vs toggle courbe) distinguant
     * les deux rappels.
     * ------------------------------------------------------------------- */
    VERIFIER(lv_obj_has_flag(ctx->chauffant_valeur[0], LV_OBJ_FLAG_CLICKABLE));
    VERIFIER(lv_obj_has_flag(ctx->chauffant_nom[0], LV_OBJ_FLAG_CLICKABLE));

    char action[32];
    char arguments[192];

    /* --- (a) tap sur la valeur de T0 : clavier numerique sur lv_layer_top(),
     * preremplit (placeholder) avec la consigne courante (210). ------------ */
    VERIFIER(dernier_enfant_calque_superieur() == NULL);
    lv_obj_send_event(ctx->chauffant_valeur[0], LV_EVENT_CLICKED, NULL);
    lv_obj_t *racine_clavier = dernier_enfant_calque_superieur();
    VERIFIER(racine_clavier != NULL);
    lv_obj_t *kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    lv_obj_t *ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);
    VERIFIER(lv_keyboard_get_mode(kb) == LV_KEYBOARD_MODE_NUMBER);
    VERIFIER_TEXTE(lv_textarea_get_placeholder_text(ta), "210");
    VERIFIER_TEXTE(lv_textarea_get_text(ta), "");

    /* --- (b) valider "200" : BACKEND_ACTION_GCODE, script
     * SET_HEATER_TEMPERATURE HEATER=extruder TARGET=200. -------------------- */
    lv_textarea_set_text(ta, "200");
    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=200") != NULL);
    lv_timer_handler(); /* acheve la fermeture asynchrone du clavier */
    VERIFIER(dernier_enfant_calque_superieur() == NULL);
    source_etat_sim_cycle(); /* draine avant la suite */

    /* --- (c) tap sur la valeur du plateau (ligne 2, "Bed") : preremplit avec
     * 60, valider "70" -> HEATER=heater_bed. -------------------------------- */
    lv_obj_send_event(ctx->chauffant_valeur[2], LV_EVENT_CLICKED, NULL);
    racine_clavier = dernier_enfant_calque_superieur();
    VERIFIER(racine_clavier != NULL);
    kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);
    VERIFIER_TEXTE(lv_textarea_get_placeholder_text(ta), "60");
    lv_textarea_set_text(ta, "70");
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=70") != NULL);
    lv_timer_handler();
    source_etat_sim_cycle();

    /* --- (d) hors bornes (999) sur T0 : AUCUN gcode envoye (taille de file
     * inchangee) -- pas de troncature/clamp silencieux. ---------------------- */
    lv_obj_send_event(ctx->chauffant_valeur[0], LV_EVENT_CLICKED, NULL);
    racine_clavier = dernier_enfant_calque_superieur();
    kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);
    lv_textarea_set_text(ta, "999");
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant);
    lv_timer_handler();
    VERIFIER(dernier_enfant_calque_superieur() == NULL);

    /* ---------------------------------------------------------------------
     * Tache 5 (task-5-brief.md) : taper le bouton NOM d'une ligne de
     * chauffant bascule l'affichage de sa courbe sur le graphe et recolore
     * son label -- verifie via l'etat ctx->serie_visible[] maintenu PLUS la
     * couleur resolue du label enfant (option explicitement offerte par le
     * brief : l'API publique LVGL n'expose pas le champ prive `hidden` de
     * lv_chart_series_t, lv_chart_hide_series() ne rend rien). serie_visible[0]
     * correspond a T0 (mapping FIXE de klipper_temp_historique.h, ligne 0 de
     * cet etat de test). `couleur_serie_t0` (0xEF4444) est une copie LOCALE
     * de COULEURS_SERIE[0] (ecran_accueil_hub.c, prive/static -- jamais
     * exportee) -- meme convention que 0x6B7280 (COULEUR_GRISE) deja
     * duplique en dur dans ce fichier juste en dessous : ce test verifie une
     * COULEUR CONCRETE, pas seulement "differente du gris" (spec de ce
     * refinement : "the NAME text color equals COULEURS_SERIE[s] when
     * fresh+visible"). ------------------------------------------------- */
    const uint32_t couleur_serie_t0 = 0xEF4444;
    VERIFIER(ctx->serie_visible[0] == true);
    lv_color_t couleur_nom_avant = lv_obj_get_style_text_color(chauffant_bouton_label(ctx->chauffant_nom[0]), 0);
    VERIFIER(!lv_color_eq(couleur_nom_avant, lv_color_hex(0x6B7280)));
    VERIFIER(lv_color_eq(couleur_nom_avant, lv_color_hex(couleur_serie_t0)));

    /* --- (a) premier clic : masque la courbe, grise le nom, IMMEDIATEMENT
     * (sans mettre_a_jour() intermediaire). --------------------------------- */
    lv_obj_send_event(ctx->chauffant_nom[0], LV_EVENT_CLICKED, NULL);
    VERIFIER(ctx->serie_visible[0] == false);
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(chauffant_bouton_label(ctx->chauffant_nom[0]), 0),
                          lv_color_hex(0x6B7280)));

    /* --- (b) reclic : reaffiche la courbe, redonne au nom la couleur de SA
     * courbe -- round-trip. -------------------------------------------------- */
    lv_obj_send_event(ctx->chauffant_nom[0], LV_EVENT_CLICKED, NULL);
    VERIFIER(ctx->serie_visible[0] == true);
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(chauffant_bouton_label(ctx->chauffant_nom[0]), 0),
                          lv_color_hex(couleur_serie_t0)));

    /* --- (c) reconciliation avec le grisage C3 : masquer la courbe puis
     * appeler mettre_a_jour() avec des donnees FRAICHES ne doit PAS
     * recolorer le nom -- le toggle utilisateur reste prioritaire sur des
     * donnees fraiches (voir le commentaire de mettre_a_jour()/
     * chauffant_nom_cb() dans le .c pour la regle complete). La VALEUR,
     * elle, N'EST PAS grisee (spec tache 5 : seul le NOM reagit au toggle de
     * courbe). --------------------------------------------------------- */
    lv_obj_send_event(ctx->chauffant_nom[0], LV_EVENT_CLICKED, NULL); /* masque de nouveau */
    VERIFIER(ctx->serie_visible[0] == false);
    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat, false, ctx);
    VERIFIER(ctx->serie_visible[0] == false);
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(chauffant_bouton_label(ctx->chauffant_nom[0]), 0),
                          lv_color_hex(0x6B7280)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(chauffant_bouton_label(ctx->chauffant_valeur[0]), 0),
                           lv_color_hex(0x6B7280)));

    /* --- (d) l'autre sens de la reconciliation : donnees perimees grise le
     * nom MEME quand la courbe est visible, et redevient la couleur de SA
     * courbe des que les donnees redeviennent fraiches ET que la courbe est
     * restee visible. ----------------------------------------------------- */
    lv_obj_send_event(ctx->chauffant_nom[0], LV_EVENT_CLICKED, NULL); /* re-affiche la courbe */
    VERIFIER(ctx->serie_visible[0] == true);
    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat, true, ctx);
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(chauffant_bouton_label(ctx->chauffant_nom[0]), 0),
                          lv_color_hex(0x6B7280)));
    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat, false, ctx);
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(chauffant_bouton_label(ctx->chauffant_nom[0]), 0),
                          lv_color_hex(couleur_serie_t0)));

    /* --- 1 seul extrudeur present : la ligne T1 disparait (masquee, jamais
     * juste videe), Bed glisse en ligne 1. -------------------------------- */
    etat_klipper_t etat_mono = etat;
    etat_mono.nb_extrudeurs = 1;
    etat_mono.extrudeurs[1].presente = false;
    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat_mono, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(chauffant_bouton_label(ctx->chauffant_nom[0])), "T0");
    VERIFIER_TEXTE(lv_label_get_text(chauffant_bouton_label(ctx->chauffant_nom[1])), "Bed");
    VERIFIER(!lv_obj_has_flag(ctx->chauffant_nom[1], LV_OBJ_FLAG_HIDDEN));
    VERIFIER(lv_obj_has_flag(ctx->chauffant_nom[2], LV_OBJ_FLAG_HIDDEN));
    VERIFIER(lv_obj_has_flag(ctx->chauffant_valeur[2], LV_OBJ_FLAG_HIDDEN));
    /* retour a l'etat complet pour la suite */
    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_obj_has_flag(ctx->chauffant_nom[2], LV_OBJ_FLAG_HIDDEN));

    /* --- impression terminee : le segment de progression disparait du texte
     * de la ligne de statut, re-evalue a CHAQUE appel (jamais un etat fige
     * depuis le premier passage) -- tache de suivi (refinement 2) : plus de
     * flag LV_OBJ_FLAG_HIDDEN a verifier ici, le texte entier ne contient
     * simplement plus "Printing". ------------------------------------------ */
    etat_klipper_t etat_repos = etat;
    etat_repos.impression_en_cours = false;
    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat_repos, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->statut), "X:10.0 Y:20.0 Z:5.0  T0  Speed: 100%  Flow: 95%");

    /* --- perime : grise (chauffants + resume), puis redevient normal --
     * style RESOLU, meme lecon que tuile_griser()/l'ancien contenu de ce
     * fichier (round-trip reversible). La grille de menu, elle, N'EST JAMAIS
     * grisee (spec) : le libelle de la premiere tuile reste blanc dans les
     * DEUX cas. ------------------------------------------------------- */
    lv_obj_t *label_tuile_homing = lv_obj_get_child(ctx->menu_boutons[ECRAN_ACCUEIL_HUB_MENU_HOMING], 0);
    lv_color_t couleur_tuile_avant = lv_obj_get_style_text_color(label_tuile_homing, 0);

    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat, true, ctx);
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(chauffant_bouton_label(ctx->chauffant_nom[0]), 0),
                          lv_color_hex(0x6B7280)));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(chauffant_bouton_label(ctx->chauffant_valeur[0]), 0),
                          lv_color_hex(0x6B7280)));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->statut, 0), lv_color_hex(0x6B7280)));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(label_tuile_homing, 0), couleur_tuile_avant));

    ECRAN_ACCUEIL_HUB.mettre_a_jour(&etat, false, ctx);
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(chauffant_bouton_label(ctx->chauffant_nom[0]), 0),
                          lv_color_hex(couleur_serie_t0)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(chauffant_bouton_label(ctx->chauffant_valeur[0]), 0),
                           lv_color_hex(0x6B7280)));
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->statut, 0), lv_color_hex(0x6B7280)));
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
    /* 1 zone_chauffants (le pool entier de lignes-boutons vit DEDANS, voir
     * plus bas) + 1 ligne de statut (position/outil/vitesse-flux/progression
     * consolides, tache de suivi refinement 2 -- remplace les TROIS lignes de
     * l'ancien resume) + 1 chart + 1 zone_menu = 4 -- depuis la tache de
     * suivi "chauffants en boutons scrollables", les lignes de chauffants ne
     * sont PLUS des enfants directs de `conteneur` (elles l'etaient quand
     * nom/valeur etaient des lv_label_t nus). */
    VERIFIER(lv_obj_get_child_count(conteneur) == 4u);
    lv_obj_t *zone_chauffants = lv_obj_get_child(conteneur, 0);
    VERIFIER(zone_chauffants != NULL);
    /* (ECRAN_ACCUEIL_HUB_HEATER_LIGNES * 2) boutons nom+valeur, pool entier
     * (present ou non -- masque, jamais retire, voir le commentaire de tete
     * du .h). */
    VERIFIER(lv_obj_get_child_count(zone_chauffants) == (uint32_t)(ECRAN_ACCUEIL_HUB_HEATER_LIGNES * 2));
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
