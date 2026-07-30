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
#include "navigation.h"
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
 * Tache 7 (jalon 3b) : bouton Macros -- visible ssi nb_macros > 0, un clic
 * empile ECRAN_MACROS (profondeur 1 -> 2). Contrairement aux autres suites
 * de ce fichier, celle-ci empile ECRAN_ACCUEIL_IDLE via navigation_empiler()
 * plutot que de le construire directement (comme suite_ecran_accueil_idle()
 * en tete de ce fichier) : prouver un changement REEL de profondeur exige un
 * vrai sommet de pile pour naviguer depuis. Sans accesseur public vers le
 * ctx d'un ecran empile (navigation.h n'en expose aucun, deliberement --
 * voir son commentaire de tete), le bouton est retrouve par parcours de
 * l'arbre LVGL depuis lv_screen_active() : meme idiome que
 * dernier_enfant_calque_superieur()/dernier_msgbox() plus haut dans ce
 * fichier, applique ici a l'ecran plutot qu'au calque superieur --
 * navigation_empiler() cree toujours le conteneur du nouvel ecran comme
 * DERNIER enfant du conteneur racine (voir navigation.c), et
 * ecran_accueil_idle_construire() cree toujours bouton_macros en DERNIER
 * (apres les cellules, la ligne de position, zone_controles et
 * zone_preregalges -- voir son ordre de construction).
 * ------------------------------------------------------------------------ */
void suite_ecran_accueil_idle_macros(void)
{
    printf("suite : ecran accueil idle (macros)\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_accueil_idle_macros() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    navigation_init(lv_screen_active()); /* pile vide, meme technique defensive que _temp() ci-dessus */
    VERIFIER(navigation_empiler(&ECRAN_ACCUEIL_IDLE) == ESP_OK);
    VERIFIER(navigation_profondeur() == 1);

    lv_obj_t *ecran = lv_screen_active();
    lv_obj_t *conteneur = lv_obj_get_child(ecran, lv_obj_get_child_count(ecran) - 1);
    VERIFIER(conteneur != NULL);
    /* 9 cellules + position + outil actif + zone_controles + zone_preregalges
     * + bouton_macros = 14 enfants directs, jamais un 15e (ce serait un
     * bouton "Imprimer" non documente -- ecran_accueil_idle_ctx_t, voir
     * ecran_accueil_idle.h, ne declare structurellement AUCUN champ de ce
     * genre : aucun code ne peut en lire un qui n'existe pas). */
    VERIFIER(lv_obj_get_child_count(conteneur) == 14);
    lv_obj_t *bouton_macros = lv_obj_get_child(conteneur, lv_obj_get_child_count(conteneur) - 1);
    VERIFIER(bouton_macros != NULL);
    lv_obj_t *label_macros = lv_obj_get_child(bouton_macros, 0);
    VERIFIER(label_macros != NULL);
    VERIFIER_TEXTE(lv_label_get_text(label_macros), "Macros");

    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    etat.nb_extrudeurs = 1;
    etat.extrudeurs[0].presente = true;

    /* --- nb_macros == 0 : cache -- deja le cas juste apres construire()
     * (voir "Visibilite initiale" dans ecran_accueil_idle.c), mais
     * mettre_a_jour() doit le CONFIRMER explicitement, jamais herite par
     * hasard de l'etat de construction (meme discipline systematique que le
     * reste de cette fonction). ------------------------------------------ */
    etat.nb_macros = 0;
    navigation_mettre_a_jour(&etat, false);
    VERIFIER(lv_obj_has_flag(bouton_macros, LV_OBJ_FLAG_HIDDEN));

    /* Un clic sur un bouton cache n'empile rien -- garde defensive de
     * bouton_macros_cb() sur LV_OBJ_FLAG_HIDDEN, meme raisonnement que
     * jog_bouton_cb()/home_bouton_cb() pour LV_STATE_DISABLED : un vrai
     * doigt ne peut pas atteindre un objet cache, mais lv_obj_send_event(),
     * le seam de test, si. */
    lv_obj_send_event(bouton_macros, LV_EVENT_CLICKED, NULL);
    VERIFIER(navigation_profondeur() == 1);

    /* --- nb_macros > 0 : visible, un clic empile ECRAN_MACROS ----------- */
    etat.nb_macros = 3;
    navigation_mettre_a_jour(&etat, false);
    VERIFIER(!lv_obj_has_flag(bouton_macros, LV_OBJ_FLAG_HIDDEN));

    lv_obj_send_event(bouton_macros, LV_EVENT_CLICKED, NULL);
    VERIFIER(navigation_profondeur() == 2);
    VERIFIER_TEXTE(navigation_id_courant(), "macros");

    navigation_depiler();
    VERIFIER(navigation_profondeur() == 1);

    /* --- choix documente (ecran_accueil_idle.c, commentaire "Rangee
     * Macros") : ce bouton n'est JAMAIS desactive sur donnees_perimees,
     * contrairement au pad de jog/homing -- naviguer vers la liste de
     * macros reste sans danger meme avec un etat perime. Preuve
     * comportementale : le clic empile toujours ECRAN_MACROS ici. --------- */
    navigation_mettre_a_jour(&etat, true);
    VERIFIER(!lv_obj_has_flag(bouton_macros, LV_OBJ_FLAG_HIDDEN));
    lv_obj_send_event(bouton_macros, LV_EVENT_CLICKED, NULL);
    VERIFIER(navigation_profondeur() == 2);
    navigation_depiler();
    VERIFIER(navigation_profondeur() == 1);
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
    /* Fix round 1 (revue code, defaut Minor n3) : epingle DIRECTEMENT
     * l'echappement JSON du script de jog plutot que de ne l'inferer
     * qu'indirectement d'un match de sous-chaine -- klipper_gcode_jog()
     * produit de VRAIS octets 0x0A (SAVE_GCODE_STATE...\nG91\n...) ; le
     * contrat BACKEND_ACTION_GCODE (core/backend.h) exige du JSON valide, ou
     * un octet 0x0A brut a l'interieur d'une chaine JSON violerait RFC 8259
     * (voir le commentaire de construire_arguments_gcode() dans
     * ecran_accueil_idle.c). Aucun octet 0x0A cru dans les arguments
     * envoyes... */
    VERIFIER(strchr(arguments, '\n') == NULL);
    /* ...ET la forme echappee (backslash-n litteral, deux caracteres) est
     * bien presente -- les deux ensemble prouvent l'echappement REEL, pas
     * une absence accidentelle de retour a la ligne dans un script qui
     * n'en aurait jamais eu. */
    VERIFIER(strstr(arguments, "\\n") != NULL);
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

/* ------------------------------------------------------------------------
 * Tache 5 (jalon 3b) : boutons de homing (Home All/X/Y/Z) -- confirmation
 * conditionnelle (spec §7) et grisage RESOLU sur donnees_perimees uniquement
 * (jamais sur "axe non reference", contrairement au pad de jog). Meme garde
 * d'ordonnancement que suite_ecran_accueil_idle_jog() (reutilise la boucle
 * simulee demarree par suite_commandes()) -- DOIT rester apres elle dans
 * tests/main.c. Pilote le dialogue de confirmation via lv_obj_send_event()
 * sur les boutons du pied, meme technique que test_clavier.c/
 * test_commandes.c (voir dernier_msgbox() ci-dessous, copie locale du meme
 * helper -- redefini ici plutot que partage, meme choix que le reste de ce
 * harnais, voir pomper_transitions_style() plus haut dans ce fichier).
 * ------------------------------------------------------------------------ */

static lv_obj_t *dernier_enfant_calque_superieur(void)
{
    lv_obj_t *calque = lv_layer_top();
    uint32_t n = lv_obj_get_child_count(calque);
    if (n == 0) {
        return NULL;
    }
    return lv_obj_get_child(calque, n - 1);
}

static lv_obj_t *dernier_msgbox(void)
{
    lv_obj_t *fond = dernier_enfant_calque_superieur();
    if (fond == NULL) {
        return NULL;
    }
    return lv_obj_get_child(fond, 0);
}

/* Tache 6 : retrouve le premier descendant DIRECT de `parent` dont la classe
 * LVGL est `classe` -- copie locale du meme helper que test_clavier.c (voir
 * son commentaire complet), redefinie ici plutot que partagee, meme choix
 * que le reste de ce harnais (pomper_transitions_style()/dernier_msgbox()
 * ci-dessus). Utilisee pour retrouver le lv_keyboard/la lv_textarea du
 * clavier numerique ouvert par une cellule de temperature -- clavier.h
 * n'expose aucun accesseur, le test les retrouve par parcours de l'arbre
 * LVGL depuis lv_layer_top(), exactement comme test_clavier.c. */
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

void suite_ecran_accueil_idle_home(void)
{
    printf("suite : ecran accueil idle (homing + confirmation)\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_accueil_idle_home() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_ACCUEIL_IDLE.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_accueil_idle_ctx_t *ctx = (ecran_accueil_idle_ctx_t *)brut;
    ECRAN_ACCUEIL_IDLE.construire(parent, ctx);
    for (int i = 0; i < ECRAN_ACCUEIL_IDLE_HOME_NB; i++) {
        VERIFIER(ctx->home_boutons[i] != NULL);
    }

    etat_klipper_t etat;
    char action[32];
    char arguments[192];

    /* --- (a) machine non referencee (axes_references=0) : "Home X" envoie
     * DIRECTEMENT, aucun dialogue -- rien ne bouge d'une position "connue"
     * puisqu'aucune position n'est connue (spec §7). */
    memset(&etat, 0, sizeof(etat));
    etat.nb_extrudeurs = 1;
    etat.extrudeurs[0].presente = true;
    etat.axes_references = 0;
    ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, false, ctx);

    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->home_boutons[ECRAN_ACCUEIL_IDLE_HOME_X], LV_EVENT_CLICKED, NULL);
    VERIFIER(dernier_enfant_calque_superieur() == NULL); /* pas de confirmation */
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "G28 X") != NULL);
    source_etat_sim_cycle(); /* draine avant la suite */

    /* --- (b) X deja reference : "Home X" ouvre la confirmation, AUCUN gcode
     * encore envoye -- decliner d'abord (rien ne part), puis reessayer et
     * confirmer (G28 X part exactement une fois). */
    etat.axes_references = 0x1u; /* X reference */
    ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, false, ctx);

    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->home_boutons[ECRAN_ACCUEIL_IDLE_HOME_X], LV_EVENT_CLICKED, NULL);
    lv_obj_t *mbox = dernier_msgbox();
    VERIFIER(mbox != NULL);
    VERIFIER_TEXTE(lv_label_get_text(lv_msgbox_get_title(mbox)), "Home X?");
    VERIFIER(source_etat_sim_file_taille() == avant); /* rien envoye tant que non confirme */

    lv_obj_t *pied = lv_msgbox_get_footer(mbox);
    lv_obj_t *bouton_decliner = lv_obj_get_child(pied, 0);
    lv_obj_t *bouton_action = lv_obj_get_child(pied, 1);
    VERIFIER(bouton_decliner != NULL);
    VERIFIER(bouton_action != NULL);
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(bouton_decliner, 0)), "Cancel");
    VERIFIER_TEXTE(lv_label_get_text(lv_obj_get_child(bouton_action, 0)), "Home");
    /* destructif=true (brief) : bouton d'action rouge, jamais l'etat "par
     * defaut" -- meme verification que section_ecran_accueil_boutons() dans
     * test_commandes.c pour "Cancel print?"/"Emergency stop?". */
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(bouton_action, 0), lv_color_hex(0xE74C3C)));
    VERIFIER(lv_obj_has_state(bouton_decliner, LV_STATE_FOCUS_KEY));
    VERIFIER(!lv_obj_has_state(bouton_action, LV_STATE_FOCUS_KEY));

    lv_obj_send_event(bouton_decliner, LV_EVENT_CLICKED, NULL);
    lv_timer_handler(); /* acheve la fermeture asynchrone du dialogue */
    VERIFIER(source_etat_sim_file_taille() == avant); /* toujours rien envoye */
    VERIFIER(dernier_enfant_calque_superieur() == NULL);

    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->home_boutons[ECRAN_ACCUEIL_IDLE_HOME_X], LV_EVENT_CLICKED, NULL);
    mbox = dernier_msgbox();
    VERIFIER(mbox != NULL);
    pied = lv_msgbox_get_footer(mbox);
    bouton_action = lv_obj_get_child(pied, 1);
    lv_obj_send_event(bouton_action, LV_EVENT_CLICKED, NULL);
    /* confirmation.c invoque le rappel de facon SYNCHRONE (fermer_et_rappeler(),
     * voir son commentaire) : la commande est deja en file ici. */
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "G28 X") != NULL);
    lv_timer_handler();
    source_etat_sim_cycle(); /* draine avant la suite */

    /* --- (c) donnees_perimees=true : les QUATRE boutons de homing se
     * desactivent, style RESOLU (couleur reellement changee, pas seulement
     * LV_STATE_DISABLED -- meme lecon que le pad de jog/tuile_griser()) --
     * meme si X reste "reference" (contrairement au pad de jog, l'etat de
     * reference ne joue AUCUN role dans le grisage du homing, spec §7). */
    pomper_transitions_style();
    lv_color_t fond_actif =
        lv_obj_get_style_bg_color(ctx->home_boutons[ECRAN_ACCUEIL_IDLE_HOME_X], LV_PART_MAIN);

    ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, true, ctx);
    for (int i = 0; i < ECRAN_ACCUEIL_IDLE_HOME_NB; i++) {
        VERIFIER(lv_obj_has_state(ctx->home_boutons[i], LV_STATE_DISABLED));
    }
    pomper_transitions_style();
    VERIFIER(!lv_color_eq(
        lv_obj_get_style_bg_color(ctx->home_boutons[ECRAN_ACCUEIL_IDLE_HOME_X], LV_PART_MAIN), fond_actif));

    /* Un clic sur un bouton desactive n'envoie rien et n'ouvre aucun
     * dialogue -- garde defensive de home_bouton_cb(), lv_obj_send_event()
     * ne passe jamais par la verification tactile de LV_STATE_DISABLED. */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->home_boutons[ECRAN_ACCUEIL_IDLE_HOME_X], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant);
    VERIFIER(dernier_enfant_calque_superieur() == NULL);

    /* redevient frais : le bouton reprend sa couleur RESOLUE de depart. */
    ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_obj_has_state(ctx->home_boutons[ECRAN_ACCUEIL_IDLE_HOME_X], LV_STATE_DISABLED));
    pomper_transitions_style();
    VERIFIER(lv_color_eq(
        lv_obj_get_style_bg_color(ctx->home_boutons[ECRAN_ACCUEIL_IDLE_HOME_X], LV_PART_MAIN), fond_actif));

    /* --- (d) "Home All" avec Y (seul) reference : confirmation quand meme
     * ("au moins un axe" du brief) ; confirmer envoie "G28" PLEIN (le masque
     * 0x7 passe a klipper_gcode_home() s'effondre en "tout", jamais
     * "G28 Y" seul -- klipper_gcode_home() est deja teste pour ce cas,
     * cette assertion ne fait que prouver que ecran_accueil_idle.c lui donne
     * bien 0x7 pour "All", pas juste le masque des axes references). */
    memset(&etat, 0, sizeof(etat));
    etat.nb_extrudeurs = 1;
    etat.extrudeurs[0].presente = true;
    etat.axes_references = 0x2u; /* Y seul reference */
    ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, false, ctx);

    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->home_boutons[ECRAN_ACCUEIL_IDLE_HOME_ALL], LV_EVENT_CLICKED, NULL);
    mbox = dernier_msgbox();
    VERIFIER(mbox != NULL);
    VERIFIER_TEXTE(lv_label_get_text(lv_msgbox_get_title(mbox)), "Home All?");
    VERIFIER(source_etat_sim_file_taille() == avant);

    pied = lv_msgbox_get_footer(mbox);
    bouton_action = lv_obj_get_child(pied, 1);
    lv_obj_send_event(bouton_action, LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "\"script\":\"G28\"") != NULL); /* plein, pas "G28 Y" */
    lv_timer_handler();
    source_etat_sim_cycle(); /* draine avant la suite */

    /* --- (e) Fix revue tache 5 : un SECOND appui pendant qu'un dialogue est
     * deja ouvert ne retargette PAS le G28 en attente. confirmation.c refuse
     * silencieusement la seconde ouverture (singleton) ; sans la garde
     * confirmation_est_ouverte() dans home_bouton_cb(), home_masque_en_attente
     * passerait quand meme au nouvel axe, et confirmer le dialogue AFFICHE
     * "Home X?" enverrait le G28 du mauvais axe. Atteignable via
     * lv_obj_send_event() (le fond modal bloque un vrai doigt, pas ce seam). */
    memset(&etat, 0, sizeof(etat));
    etat.nb_extrudeurs = 1;
    etat.extrudeurs[0].presente = true;
    etat.axes_references = 0x1u | 0x2u; /* X et Y references */
    ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, false, ctx);

    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->home_boutons[ECRAN_ACCUEIL_IDLE_HOME_X], LV_EVENT_CLICKED, NULL);
    mbox = dernier_msgbox();
    VERIFIER(mbox != NULL);
    VERIFIER_TEXTE(lv_label_get_text(lv_msgbox_get_title(mbox)), "Home X?");

    /* second appui (Home Y) alors que "Home X?" est toujours ouvert */
    lv_obj_send_event(ctx->home_boutons[ECRAN_ACCUEIL_IDLE_HOME_Y], LV_EVENT_CLICKED, NULL);
    VERIFIER_TEXTE(lv_label_get_text(lv_msgbox_get_title(dernier_msgbox())), "Home X?"); /* toujours le premier */
    VERIFIER(source_etat_sim_file_taille() == avant); /* rien envoye */

    pied = lv_msgbox_get_footer(mbox);
    bouton_action = lv_obj_get_child(pied, 1);
    lv_obj_send_event(bouton_action, LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "G28 X") != NULL); /* le bon axe... */
    VERIFIER(strstr(arguments, "G28 Y") == NULL); /* ...jamais celui du second appui */
    lv_timer_handler();
    source_etat_sim_cycle();

    lv_obj_delete(parent);
    free(brut);
}

/* ------------------------------------------------------------------------
 * Tache 6 (jalon 3b) : consignes de temperature manuelles (clavier
 * numerique ouvert sur une cellule) + prereglages (PLA/PETG/ABS/Off). Meme
 * garde d'ordonnancement que suite_ecran_accueil_idle_jog()/_home()
 * ci-dessus (reutilise l'habillage construit par suite_ecran_configuration()
 * ET la boucle simulee demarree par suite_commandes()) -- DOIT rester apres
 * elles dans tests/main.c.
 *
 * navigation_init(lv_screen_active()) au tout debut : meme technique
 * defensive que groupe_file_pleine()/section_echec_asynchrone()
 * (test_ecran_macros.c/test_commandes.c respectivement) avant de retrouver
 * le bandeau de l'habillage comme DERNIER enfant de lv_screen_active() --
 * detruit tout ecran qu'une suite precedente aurait laisse empile sans le
 * depiler, ce qui decalerait sinon le bandeau d'un cran et casserait
 * silencieusement cette hypothese. Les suites _jog()/_home() ci-dessus n'en
 * avaient pas besoin (elles ne lisent jamais le bandeau) ; celle-ci est la
 * premiere de ce fichier a le faire.
 * ------------------------------------------------------------------------ */

void suite_ecran_accueil_idle_temp(void)
{
    printf("suite : ecran accueil idle (temperatures manuelles + prereglages)\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_accueil_idle_temp() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    navigation_init(lv_screen_active());
    lv_obj_t *ecran = lv_screen_active();
    lv_obj_t *bandeau = lv_obj_get_child(ecran, lv_obj_get_child_count(ecran) - 1);
    VERIFIER(bandeau != NULL);
    lv_obj_t *bandeau_texte = lv_obj_get_child(bandeau, 0);
    VERIFIER(bandeau_texte != NULL);

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_ACCUEIL_IDLE.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_accueil_idle_ctx_t *ctx = (ecran_accueil_idle_ctx_t *)brut;
    ECRAN_ACCUEIL_IDLE.construire(parent, ctx);

    /* Mono-extrudeur + plateau : cellules[0] = T0 (buse), cellules[1] = Bed
     * -- meme mapping que suite_ecran_accueil_idle() (palier MONO). */
    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    etat.nb_extrudeurs = 1;
    etat.extrudeurs[0].presente = true;
    etat.extrudeurs[0].actuelle = 205.0f;
    etat.extrudeurs[0].consigne = 200.0f;
    etat.plateau.presente = true;
    etat.plateau.actuelle = 55.0f;
    etat.plateau.consigne = 55.0f;
    etat.outil_actif = 0;
    ECRAN_ACCUEIL_IDLE.mettre_a_jour(&etat, false, ctx);

    char action[32];
    char arguments[192];

    /* --- (a) tap sur la cellule buse : un clavier numerique apparait sur
     * lv_layer_top(), prerempli avec la consigne courante (200). --------- */
    VERIFIER(dernier_enfant_calque_superieur() == NULL);
    lv_obj_send_event(ctx->cellules[0].racine, LV_EVENT_CLICKED, NULL);
    lv_obj_t *racine_clavier = dernier_enfant_calque_superieur();
    VERIFIER(racine_clavier != NULL);
    lv_obj_t *kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    lv_obj_t *ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);
    VERIFIER(lv_keyboard_get_mode(kb) == LV_KEYBOARD_MODE_NUMBER);
    VERIFIER_TEXTE(lv_textarea_get_text(ta), "200");

    /* --- (b) valider "210" : BACKEND_ACTION_GCODE, script
     * SET_HEATER_TEMPERATURE HEATER=extruder TARGET=210. ------------------ */
    lv_textarea_set_text(ta, "210");
    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=210") != NULL);
    lv_timer_handler(); /* acheve la fermeture asynchrone du clavier */
    VERIFIER(dernier_enfant_calque_superieur() == NULL);
    source_etat_sim_cycle(); /* draine avant la suite */

    /* --- (c) valider "999" (hors borne [0,350]) : notification d'erreur,
     * AUCUN gcode envoye (taille de file inchangee). --------------------- */
    lv_obj_send_event(ctx->cellules[0].racine, LV_EVENT_CLICKED, NULL);
    racine_clavier = dernier_enfant_calque_superieur();
    kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);
    lv_textarea_set_text(ta, "999");
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant);
    VERIFIER(!lv_obj_has_flag(bandeau, LV_OBJ_FLAG_HIDDEN));
    VERIFIER_TEXTE(lv_label_get_text(bandeau_texte), "Invalid temperature (0-350)");
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(bandeau, 0), lv_color_hex(0xB3352C))); /* erreur */
    lv_timer_handler();

    /* --- (d) saisie non numerique : meme resultat (erreur, rien envoye). - */
    lv_obj_send_event(ctx->cellules[0].racine, LV_EVENT_CLICKED, NULL);
    racine_clavier = dernier_enfant_calque_superieur();
    kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);
    lv_textarea_set_text(ta, "abc");
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant);
    VERIFIER_TEXTE(lv_label_get_text(bandeau_texte), "Invalid temperature (0-350)");
    lv_timer_handler();

    /* --- (d bis) bornes EXACTES [0,350] (revue finale jalon 3b) : 350 passe
     * (TARGET=350 envoye), 351 est rejete sans rien envoyer -- la borne haute
     * est inclusive, exactement comme le code (cible <= 350). ------------- */
    lv_obj_send_event(ctx->cellules[0].racine, LV_EVENT_CLICKED, NULL);
    racine_clavier = dernier_enfant_calque_superieur();
    kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);
    lv_textarea_set_text(ta, "350");
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1); /* 350 accepte */
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=350") != NULL);
    lv_timer_handler();
    source_etat_sim_cycle();

    lv_obj_send_event(ctx->cellules[0].racine, LV_EVENT_CLICKED, NULL);
    racine_clavier = dernier_enfant_calque_superieur();
    kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);
    lv_textarea_set_text(ta, "351");
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant); /* 351 rejete, rien envoye */
    VERIFIER_TEXTE(lv_label_get_text(bandeau_texte), "Invalid temperature (0-350)");
    lv_timer_handler();

    /* --- (e) annuler : rien envoye, clavier disparait sans invoquer le
     * gcode (spec : "valeur NULL (annule) => rien"). --------------------- */
    lv_obj_send_event(ctx->cellules[0].racine, LV_EVENT_CLICKED, NULL);
    racine_clavier = dernier_enfant_calque_superieur();
    kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    VERIFIER(kb != NULL);
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_CANCEL, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant);
    lv_timer_handler();
    VERIFIER(dernier_enfant_calque_superieur() == NULL);

    /* --- cellule plateau ("Bed target"), prerempli 55, valider "70" :
     * HEATER=heater_bed -- bed-cell test (brief). ------------------------- */
    lv_obj_send_event(ctx->cellules[1].racine, LV_EVENT_CLICKED, NULL);
    racine_clavier = dernier_enfant_calque_superieur();
    VERIFIER(racine_clavier != NULL);
    kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);
    VERIFIER_TEXTE(lv_textarea_get_text(ta), "55");
    lv_textarea_set_text(ta, "70");
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=70") != NULL);
    lv_timer_handler();
    source_etat_sim_cycle();

    /* --- (f) prereglage PLA : DEUX gcodes queues par le MEME clic -- buse
     * ACTIVE (T0, 210) puis plateau (60), voir source_etat_sim_avant_derniere_commande()
     * (necessaire ici : source_etat_sim_cycle() depile toute la file d'un
     * coup, impossible d'inspecter le premier gcode en cyclant entre les
     * deux). --------------------------------------------------------------*/
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->preset_boutons[ECRAN_ACCUEIL_IDLE_PRESET_PLA], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 2);
    VERIFIER(source_etat_sim_avant_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=210") != NULL);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=60") != NULL);
    source_etat_sim_cycle();

    /* --- (g) "Off" : buse active a 0, plateau a 0 -- meme chemin que PLA/
     * PETG/ABS, jamais un cas special (voir preset_bouton_cb()). ---------- */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->preset_boutons[ECRAN_ACCUEIL_IDLE_PRESET_OFF], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 2);
    VERIFIER(source_etat_sim_avant_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=0") != NULL);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER(strstr(arguments, "SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=0") != NULL);
    source_etat_sim_cycle();

    lv_obj_delete(parent);
    free(brut);
}
