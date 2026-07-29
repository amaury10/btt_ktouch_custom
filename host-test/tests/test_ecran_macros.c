/* Tâche 6 (jalon 3a) : l'écran macros -- voir ecran_macros.h pour le contrat
 * et task-6-brief.md pour les scénarios exigés. Cinq groupes de preuves :
 *
 *   1. ecran_macros_construire_arguments() -- fonction PURE, sans LVGL.
 *   2. Filtrage des macros `_préfixées` + liste vide ("No macros") + ligne de
 *      troncature -- construction directe du contexte (même technique que
 *      test_ecran_accueil.c), sans navigation ni boucle simulée.
 *   3. Pagination (20 macros, 2 pages).
 *   4. Grisage systématique sur donnees_perimees -- round-trip complet,
 *      couleurs RÉSOLUES (pas seulement LV_STATE_DISABLED, leçon de la revue
 *      finale du jalon 2b -- voir pomper_transitions_style() ci-dessous).
 *   5. Trace du seam : tap -> ui_commander(BACKEND_ACTION_MACRO, "{\"nom\":..}")
 *      avec le NOM RÉELLEMENT AFFICHÉ sur le bouton, notification "Macro
 *      sent" en succès synchrone, "Command failed: <nom>" en échec
 *      SYNCHRONE (file pleine), et la bannière générique existante
 *      ("Command failed: macro", sans nom -- voir le commentaire de
 *      bouton_macro_cb() dans ecran_macros.c) sur l'échec ASYNCHRONE que
 *      MACRO_ECHEC démontre (backend_factice.c, scénario 11). RÉUTILISE la
 *      boucle simulée et l'habillage déjà construits par suite_commandes()
 *      (singletons process-wide, voir son commentaire de tête) -- ce fichier
 *      DOIT donc être enregistré APRÈS elle dans tests/main.c. */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "backend.h"
#include "backend_factice.h"
#include "ecran_macros.h"
#include "etat_klipper.h"
#include "habillage.h"
#include "navigation.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

/* ------------------------------------------------------------------------
 * Groupe 1 : ecran_macros_construire_arguments() -- fonction pure.
 * ------------------------------------------------------------------------ */

static void groupe_construire_arguments(void)
{
    printf("suite : ecran macros (construire_arguments)\n");

    char tampon[ECRAN_MACROS_ARGUMENTS_MAX];

    memset(tampon, 0, sizeof(tampon));
    VERIFIER(ecran_macros_construire_arguments("LOAD_FILAMENT", tampon, sizeof(tampon)) == true);
    VERIFIER_TEXTE(tampon, "{\"nom\":\"LOAD_FILAMENT\"}");

    /* NULL / taille nulle : false, jamais un plantage. */
    VERIFIER(ecran_macros_construire_arguments(NULL, tampon, sizeof(tampon)) == false);
    VERIFIER(ecran_macros_construire_arguments("X", NULL, sizeof(tampon)) == false);
    VERIFIER(ecran_macros_construire_arguments("X", tampon, 0) == false);

    /* Tampon trop court : false, jamais tronque en silence -- même
     * discipline que rpc_construire_requete() (moonraker_rpc.h). */
    char court[8];
    VERIFIER(ecran_macros_construire_arguments("LOAD_FILAMENT", court, sizeof(court)) == false);
}

/* ------------------------------------------------------------------------
 * Petits utilitaires partagés par les groupes 2-4.
 * ------------------------------------------------------------------------ */

static void construire_ecran(lv_obj_t **parent_sortie, ecran_macros_ctx_t **ctx_sortie, void **brut_sortie)
{
    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_MACROS.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_macros_ctx_t *ctx = (ecran_macros_ctx_t *)brut;
    ECRAN_MACROS.construire(parent, ctx);
    *parent_sortie = parent;
    *ctx_sortie = ctx;
    *brut_sortie = brut;
}

/* Remplit `e->macros[0..n-1]` avec `noms`, borne le compte, remet le reste
 * de l'etat a zero -- petit constructeur partage par les groupes 2 et 3. */
static void remplir_macros(etat_klipper_t *e, const char *const *noms, uint8_t n)
{
    memset(e, 0, sizeof(*e));
    for (uint8_t i = 0; i < n; i++) {
        snprintf(e->macros[i], KLIPPER_MACRO_NOM_MAX, "%s", noms[i]);
    }
    e->nb_macros = n;
}

/* ------------------------------------------------------------------------
 * Groupe 2 : filtrage des `_prefixees`, liste vide, ligne de troncature.
 * ------------------------------------------------------------------------ */

static void groupe_filtrage_et_etats(void)
{
    printf("suite : ecran macros (filtrage, vide, troncature)\n");

    lv_obj_t *parent;
    ecran_macros_ctx_t *ctx;
    void *brut;
    construire_ecran(&parent, &ctx, &brut);

    /* --- tous les emplacements sont crees, la ligne d'avertissement et le
     * texte "vide" existent, les deux boutons de pagination et le libelle de
     * page aussi. */
    for (uint8_t i = 0; i < ECRAN_MACROS_PAGE_TAILLE; i++) {
        VERIFIER(ctx->boutons[i] != NULL);
        VERIFIER(ctx->labels[i] != NULL);
    }
    VERIFIER(ctx->avertissement != NULL);
    VERIFIER(ctx->vide != NULL);
    VERIFIER(ctx->bouton_precedent != NULL);
    VERIFIER(ctx->bouton_suivant != NULL);
    VERIFIER(ctx->page_label != NULL);

    /* --- etat entierement a zero (nb_macros=0) : "No macros", jamais un
     * ecran muet (brief). Aucun bouton visible, pagination masquee. */
    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    ECRAN_MACROS.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_obj_has_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN));
    VERIFIER_TEXTE(lv_label_get_text(ctx->vide), "No macros");
    for (uint8_t i = 0; i < ECRAN_MACROS_PAGE_TAILLE; i++) {
        VERIFIER(lv_obj_has_flag(ctx->boutons[i], LV_OBJ_FLAG_HIDDEN));
    }
    VERIFIER(lv_obj_has_flag(ctx->bouton_precedent, LV_OBJ_FLAG_HIDDEN));
    VERIFIER(lv_obj_has_flag(ctx->bouton_suivant, LV_OBJ_FLAG_HIDDEN));

    /* --- scenario U1 (memes noms que backend_factice.c, g_macros_u1) :
     * "_CACHEE" NE DOIT JAMAIS apparaitre a l'ecran -- coeur du critere de
     * succes 1 de la spec (jalon 3, §10). */
    static const char *const macros_u1[] = {
        "HOME_ALL", "_CACHEE", "PURGE_PARAM", "MACRO_ECHEC",
        "CHANGE_TOOL", "LOAD_FILAMENT", "UNLOAD_FILAMENT", "CALIBRATE_OFFSETS",
    };
    remplir_macros(&etat, macros_u1, (uint8_t)(sizeof(macros_u1) / sizeof(macros_u1[0])));
    ECRAN_MACROS.mettre_a_jour(&etat, false, ctx);

    VERIFIER(lv_obj_has_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN)); /* plus "vide" */
    VERIFIER(ctx->nb_filtrees == 7); /* 8 macros, une cachee */
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[0]), "HOME_ALL");
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[1]), "PURGE_PARAM");
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[2]), "MACRO_ECHEC");
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[3]), "CHANGE_TOOL");
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[4]), "LOAD_FILAMENT");
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[5]), "UNLOAD_FILAMENT");
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[6]), "CALIBRATE_OFFSETS");
    /* Emplacement 7 (le 8eme de la page) est inutilise -- masque, pas un
     * ancien libelle qui trainerait. */
    VERIFIER(lv_obj_has_flag(ctx->boutons[7], LV_OBJ_FLAG_HIDDEN));
    /* Aucun label visible ne porte jamais "_CACHEE", explicitement -- pas
     * seulement "7 macros comptees", la preuve directe que le brief exige. */
    for (uint8_t i = 0; i < ECRAN_MACROS_PAGE_TAILLE; i++) {
        VERIFIER(strcmp(lv_label_get_text(ctx->labels[i]), "_CACHEE") != 0);
    }
    /* Une seule page (7 <= 16) : pagination masquee. */
    VERIFIER(lv_obj_has_flag(ctx->bouton_precedent, LV_OBJ_FLAG_HIDDEN));
    VERIFIER(lv_obj_has_flag(ctx->bouton_suivant, LV_OBJ_FLAG_HIDDEN));
    /* Pas de troncature dans ce scenario : la ligne reste masquee. */
    VERIFIER(lv_obj_has_flag(ctx->avertissement, LV_OBJ_FLAG_HIDDEN));

    /* --- macros_tronquees : ligne d'avertissement HONNETE, visible. */
    etat.macros_tronquees = true;
    ECRAN_MACROS.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_obj_has_flag(ctx->avertissement, LV_OBJ_FLAG_HIDDEN));
    VERIFIER_TEXTE(lv_label_get_text(ctx->avertissement), "Some macros are not shown (list truncated)");

    /* Redevient faux : la ligne redisparait -- reversible, pas a sens
     * unique (meme lecon que le grisage). */
    etat.macros_tronquees = false;
    ECRAN_MACROS.mettre_a_jour(&etat, false, ctx);
    VERIFIER(lv_obj_has_flag(ctx->avertissement, LV_OBJ_FLAG_HIDDEN));

    /* --- torture memoire : un nom de macro occupant TOUT le champ SANS
     * octet nul (meme classe de defaut que le nom de fichier de
     * ecran_accueil.c, voir son commentaire pour le detail complet du
     * mecanisme ASan/redzone). Alloue sur le TAS, rempli d'un octet non nul
     * PARTOUT, puis SEULS nb_macros et macros_tronquees sont ramenes a une
     * valeur valide (nb_macros doit rester <= KLIPPER_MACROS_MAX pour que la
     * boucle explore les 48 emplacements, tous non-nul-termines dans ce
     * test -- pire cas que UN SEUL nom pathologique ; macros_tronquees est
     * un `_Bool`, un octet 0x41 est une valeur invalide pour ce type sous
     * -fsanitize=undefined, meme lecon que impression_en_pause dans
     * test_ecran_accueil.c). */
    etat_klipper_t *e = malloc(sizeof(*e));
    VERIFIER(e != NULL);
    memset(e, 0x41, sizeof(*e));
    e->nb_macros = KLIPPER_MACROS_MAX;
    e->macros_tronquees = false;
    VERIFIER((ECRAN_MACROS.mettre_a_jour(e, false, ctx), true));
    /* Chaque nom filtre doit rester borne a KLIPPER_MACRO_NOM_MAX-1 octets
     * (la place utile hors le octet nul final) -- aucun octet nul dans la
     * source ne l'a jamais aide a s'arreter plus tot. */
    for (uint8_t i = 0; i < ctx->nb_filtrees; i++) {
        VERIFIER(strlen(ctx->macros_filtrees[i]) < KLIPPER_MACRO_NOM_MAX);
    }
    free(e);

    /* --- fix round 1 (revue tache 6, L2) : le clamp de nb_macros a
     * KLIPPER_MACROS_MAX (ecran_macros.c, "meme borne defensive que
     * web_nb_macros_serialisables()") est PORTEUR -- sans lui, un
     * nb_macros=200 (un producteur hostile ou bogue, exactement le meme
     * defaut nomme par la lecon de web_macros.h a la tache 1) ecrirait
     * jusqu'a 200 entrees dans ctx->macros_filtrees[48], debordant de 152
     * emplacements le tableau fixe -- et, une fois les 48 premiers
     * emplacements remplis, du tampon ALLOUE (calloc, voir
     * construire_ecran()) lui-meme, un heap-buffer-overflow qu'ASan attrape.
     * Le test precedent (nb_macros = KLIPPER_MACROS_MAX pile) ne pouvait PAS
     * exercer ce clamp : sans lui, il ne fait jamais rien puisque la
     * boucle s'arrete deja a 48 par construction du memset. CE test-ci
     * envoie explicitement nb_macros AU-DELA de la borne. RED observe en
     * ecrivant ce test : retirer temporairement le clamp
     * (`if (nb_macros_source > KLIPPER_MACROS_MAX) { nb_macros_source =
     * KLIPPER_MACROS_MAX; }`) dans ecran_macros.c fait planter ce bloc sous
     * ASan (heap-buffer-overflow, ecriture) -- voir le rapport de tache pour
     * la trace complete. Noms simples ('A'..'Z' cycliques, PAS l'octet 0x41
     * uniforme du bloc precedent) : le but ici est le COMPTE, pas
     * l'absence d'octet nul, et un nom repete partout confondrait le
     * defaut de comptage avec une deduplication qui n'existe pas. */
    etat_klipper_t *e2 = malloc(sizeof(*e2));
    VERIFIER(e2 != NULL);
    memset(e2, 0, sizeof(*e2));
    /* `e2->macros[]` ne fait QUE KLIPPER_MACROS_MAX emplacements (c'est la
     * structure REELLE, voir etat_klipper.h) -- nb_macros=200 modelise
     * exactement ce que macros_tronquees existe pour signaler : un
     * producteur qui ANNONCE en connaitre davantage que ce que la structure
     * peut porter. Seuls les KLIPPER_MACROS_MAX emplacements REELLEMENT
     * alloues sont remplis (un nom lisible different par emplacement,
     * jamais vide/'_' pour que chacun compte comme filtre) ; les indices
     * au-dela restent a zero (jamais lus : la boucle de ecran_macros.c
     * s'arrete a e2->nb_macros, mais le clamp doit l'empecher de depasser
     * KLIPPER_MACROS_MAX AVANT d'atteindre un indice hors du tableau reel). */
    for (uint8_t i = 0; i < KLIPPER_MACROS_MAX; i++) {
        snprintf(e2->macros[i], KLIPPER_MACRO_NOM_MAX, "SURNOMBRE_%02u", (unsigned)i);
    }
    e2->nb_macros = 200;
    e2->macros_tronquees = true; /* honnete : e2 pretend en connaitre plus que KLIPPER_MACROS_MAX */
    VERIFIER((ECRAN_MACROS.mettre_a_jour(e2, false, ctx), true)); /* ne doit jamais planter (ASan) */
    VERIFIER(ctx->nb_filtrees <= KLIPPER_MACROS_MAX);
    free(e2);

    lv_obj_delete(parent);
    free(brut);
}

/* ------------------------------------------------------------------------
 * Groupe 3 : pagination (20 macros -> 2 pages).
 * ------------------------------------------------------------------------ */

static void groupe_pagination(void)
{
    printf("suite : ecran macros (pagination)\n");

    lv_obj_t *parent;
    ecran_macros_ctx_t *ctx;
    void *brut;
    construire_ecran(&parent, &ctx, &brut);

    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    char noms[20][KLIPPER_MACRO_NOM_MAX];
    const char *pointeurs[20];
    for (uint8_t i = 0; i < 20; i++) {
        snprintf(noms[i], sizeof(noms[i]), "MACRO_%02u", (unsigned)(i + 1));
        pointeurs[i] = noms[i];
    }
    remplir_macros(&etat, pointeurs, 20);
    ECRAN_MACROS.mettre_a_jour(&etat, false, ctx);

    VERIFIER(ctx->nb_filtrees == 20);
    VERIFIER(!lv_obj_has_flag(ctx->bouton_precedent, LV_OBJ_FLAG_HIDDEN));
    VERIFIER(!lv_obj_has_flag(ctx->bouton_suivant, LV_OBJ_FLAG_HIDDEN));
    VERIFIER_TEXTE(lv_label_get_text(ctx->page_label), "Page 1/2");
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[0]), "MACRO_01");
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[15]), "MACRO_16");
    /* Precedent desactive (premiere page), Suivant actif. */
    VERIFIER(lv_obj_has_state(ctx->bouton_precedent, LV_STATE_DISABLED));
    VERIFIER(!lv_obj_has_state(ctx->bouton_suivant, LV_STATE_DISABLED));

    /* --- Suivant : page 2, 4 macros restantes (17..20) dans les 4 premiers
     * emplacements, le reste masque. */
    lv_obj_send_event(ctx->bouton_suivant, LV_EVENT_CLICKED, NULL);
    VERIFIER_TEXTE(lv_label_get_text(ctx->page_label), "Page 2/2");
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[0]), "MACRO_17");
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[3]), "MACRO_20");
    VERIFIER(lv_obj_has_flag(ctx->boutons[4], LV_OBJ_FLAG_HIDDEN));
    VERIFIER(!lv_obj_has_state(ctx->bouton_precedent, LV_STATE_DISABLED));
    VERIFIER(lv_obj_has_state(ctx->bouton_suivant, LV_STATE_DISABLED)); /* derniere page */

    /* Un clic supplementaire sur Suivant (deja a la derniere page, garde
     * defensive de bouton_suivant_cb -- lv_obj_send_event() ne passe jamais
     * par LV_STATE_DISABLED) ne change rien. */
    lv_obj_send_event(ctx->bouton_suivant, LV_EVENT_CLICKED, NULL);
    VERIFIER_TEXTE(lv_label_get_text(ctx->page_label), "Page 2/2");

    /* --- Precedent : retour page 1. */
    lv_obj_send_event(ctx->bouton_precedent, LV_EVENT_CLICKED, NULL);
    VERIFIER_TEXTE(lv_label_get_text(ctx->page_label), "Page 1/2");
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[0]), "MACRO_01");

    lv_obj_delete(parent);
    free(brut);
}

/* ------------------------------------------------------------------------
 * Groupe 4 : grisage systematique (donnees_perimees), couleurs RESOLUES.
 * ------------------------------------------------------------------------ */

/* Meme lecon que test_commandes.c (revue finale jalon 2b) : le theme LVGL
 * par defaut anime bg_color/text_color sur CHAQUE changement d'etat -- sans
 * avancer l'horloge LVGL, la couleur RESOLUE reste celle de depart meme
 * apres lv_obj_add_state(LV_STATE_DISABLED). Redefinie ICI (pas partagee) :
 * chaque fichier de test du harnais l'a deja fait separement. */
static void pomper_transitions_style(void)
{
    for (int i = 0; i < 5; i++) {
        lv_tick_inc(100);
        lv_timer_handler();
    }
}

static void groupe_grisage(void)
{
    printf("suite : ecran macros (grisage resolu)\n");

    lv_obj_t *parent;
    ecran_macros_ctx_t *ctx;
    void *brut;
    construire_ecran(&parent, &ctx, &brut);

    etat_klipper_t etat;
    static const char *const macros[] = { "HOME_ALL", "LOAD_FILAMENT" };
    remplir_macros(&etat, macros, 2);
    ECRAN_MACROS.mettre_a_jour(&etat, false, ctx);
    pomper_transitions_style();

    lv_color_t fond_actif = lv_obj_get_style_bg_color(ctx->boutons[0], LV_PART_MAIN);
    lv_color_t texte_actif = lv_obj_get_style_text_color(ctx->labels[0], LV_PART_MAIN);

    /* --- perime : grise, couleur RESOLUE differente (CRITICAL, revue finale
     * jalon 2b -- LV_STATE_DISABLED seul est pixel-identique a l'etat arme). */
    ECRAN_MACROS.mettre_a_jour(&etat, true, ctx);
    VERIFIER(lv_obj_has_state(ctx->boutons[0], LV_STATE_DISABLED));
    pomper_transitions_style();
    VERIFIER(!lv_color_eq(lv_obj_get_style_bg_color(ctx->boutons[0], LV_PART_MAIN), fond_actif));
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->labels[0], LV_PART_MAIN), texte_actif));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->labels[0], LV_PART_MAIN), lv_color_hex(0x6B7280)));
    lv_color_t gris_avertissement = lv_obj_get_style_text_color(ctx->avertissement, 0);
    VERIFIER(lv_color_eq(gris_avertissement, lv_color_hex(0x6B7280)));

    /* Un clic direct sur un bouton grise ne doit RIEN envoyer (garde
     * defensive de bouton_macro_cb() -- lv_obj_send_event() ne passe jamais
     * par la verification tactile de LV_STATE_DISABLED). */
    VERIFIER(source_etat_sim_est_demarre() == true); /* deja demarre par suite_commandes() */
    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[0], LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant);

    /* --- redevient normal : couleur RESOLUE EXACTEMENT celle de depart, pas
     * seulement "differente du gris" (meme lecon que tuile_griser()). */
    ECRAN_MACROS.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_obj_has_state(ctx->boutons[0], LV_STATE_DISABLED));
    pomper_transitions_style();
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(ctx->boutons[0], LV_PART_MAIN), fond_actif));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->labels[0], LV_PART_MAIN), texte_actif));

    lv_obj_delete(parent);
    free(brut);
}

/* ------------------------------------------------------------------------
 * Groupe 5 : trace du seam -- tap -> ui_commander(), notifications.
 * ------------------------------------------------------------------------ */

static void groupe_trace_seam(void)
{
    printf("suite : ecran macros (trace du seam -> backend)\n");

    /* Meme garde d'ordonnancement que suite_commandes() (voir son
     * commentaire de tete dans test_commandes.c) : ce groupe reutilise les
     * DEUX singletons process-wide (habillage, boucle simulee) deja etablis
     * par les suites precedentes. */
    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: groupe_trace_seam() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    /* Restaure g_bandeau comme dernier enfant de lv_screen_active() (meme
     * technique que section_echec_asynchrone() dans test_commandes.c, voir
     * son commentaire complet) : capture les pointeurs UNE FOIS ici, avant
     * de construire quoi que ce soit d'autre par-dessus -- ils restent
     * valables ensuite quels que soient les objets ajoutes plus tard (LVGL
     * ne deplace jamais un objet existant quand un frere est cree). */
    navigation_init(lv_screen_active());
    lv_obj_t *ecran = lv_screen_active();
    lv_obj_t *bandeau = lv_obj_get_child(ecran, lv_obj_get_child_count(ecran) - 1);
    VERIFIER(bandeau != NULL);
    lv_obj_t *bandeau_texte = lv_obj_get_child(bandeau, 0);
    VERIFIER(bandeau_texte != NULL);

    /* Fix round 1 (revue tache 6, M2 MEDIUM) : le pompage a vide qui vivait
     * ICI (pour absorber un sceau "reset" laisse arme par test_jouet.c) a
     * ete retire -- le nettoyage se fait desormais A LA SOURCE
     * (source_etat_sim_reset_echec(), appelee par test_jouet.c lui-meme,
     * voir son commentaire). Un band-aid cote consommateur cassait
     * silencieusement des qu'un troisieme fichier s'intercalait entre les
     * deux, ou continuait de tourner pour rien si le producteur cessait un
     * jour de fuir : corriger au point de PRODUCTION rend cette suite-ci
     * independante de ce que d'autres suites laissent trainer. */

    lv_obj_t *parent;
    ecran_macros_ctx_t *ctx;
    void *brut;
    construire_ecran(&parent, &ctx, &brut);

    /* Memes noms que backend_factice.c, scenario 11 (U1) : MACRO_ECHEC
     * echoue TOUJOURS (sentinelle), LOAD_FILAMENT reussit TOUJOURS (connue
     * de macro_connue(), independamment du scenario actif -- voir son
     * commentaire dans backend_factice.c). */
    etat_klipper_t etat;
    static const char *const macros[] = {
        "HOME_ALL", "_CACHEE", "PURGE_PARAM", "MACRO_ECHEC",
        "CHANGE_TOOL", "LOAD_FILAMENT", "UNLOAD_FILAMENT", "CALIBRATE_OFFSETS",
    };
    remplir_macros(&etat, macros, (uint8_t)(sizeof(macros) / sizeof(macros[0])));
    ECRAN_MACROS.mettre_a_jour(&etat, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[2]), "MACRO_ECHEC");
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[4]), "LOAD_FILAMENT");

    /* --- succes synchrone (acceptee en file) : "Macro sent: <nom>",
     * PAS "launched"/"succeeded" (fix round 1, revue tache 6, N1) -- voir le
     * commentaire de bouton_macro_cb() dans ecran_macros.c pour pourquoi ce
     * mot precis est le seul honnete ici. */
    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[4], LV_EVENT_CLICKED, NULL); /* LOAD_FILAMENT */
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(!lv_obj_has_flag(bandeau, LV_OBJ_FLAG_HIDDEN));
    VERIFIER_TEXTE(lv_label_get_text(bandeau_texte), "Macro sent: LOAD_FILAMENT");
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(bandeau, 0), lv_color_hex(0x1B2430))); /* info, pas erreur */

    /* Execution reelle : LOAD_FILAMENT est connue, reussit -- aucune
     * bannerie generique ne doit ecraser celle deja affichee. */
    source_etat_sim_cycle();
    habillage_pomper();
    VERIFIER_TEXTE(lv_label_get_text(bandeau_texte), "Macro sent: LOAD_FILAMENT");

    /* --- echec ASYNCHRONE (MACRO_ECHEC, backend_factice.c) : accepte en
     * file (banniere "sent" d'abord), puis l'execution reelle echoue
     * -- remplace par la banniere GENERIQUE existante ("Command failed:
     * macro", SANS le nom -- le seam ne porte que le nom de l'ACTION,
     * jamais ses arguments, voir ui_commande_echec() dans source_etat.h).
     * C'est EXACTEMENT le chemin queue/RPC que la trouvaille B (revue des
     * taches 4/5) demande a MACRO_ECHEC de demontrer -- pas une panne reelle
     * de Klipper (notify_gcode_response, hors de portee de ce seam
     * aujourd'hui). */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[2], LV_EVENT_CLICKED, NULL); /* MACRO_ECHEC */
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER_TEXTE(lv_label_get_text(bandeau_texte), "Macro sent: MACRO_ECHEC");

    source_etat_sim_cycle(); /* execute -> commande() rend ESP_FAIL (sentinelle) */
    habillage_pomper();      /* remonte l'echec ASYNCHRONE au bandeau */
    VERIFIER_TEXTE(lv_label_get_text(bandeau_texte), "Command failed: macro");
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(bandeau, 0), lv_color_hex(0xB3352C))); /* erreur */

    lv_obj_delete(parent);
    free(brut);
}

/* ------------------------------------------------------------------------
 * Groupe 6 : echec SYNCHRONE (file pleine) -- chemin distinct, meme
 * technique que section_file_pleine() dans test_commandes.c.
 * ------------------------------------------------------------------------ */

static void groupe_file_pleine(void)
{
    printf("suite : ecran macros (file pleine -> notification avec nom)\n");

    navigation_init(lv_screen_active());
    lv_obj_t *ecran = lv_screen_active();
    lv_obj_t *bandeau = lv_obj_get_child(ecran, lv_obj_get_child_count(ecran) - 1);
    VERIFIER(bandeau != NULL);
    lv_obj_t *bandeau_texte = lv_obj_get_child(bandeau, 0);
    VERIFIER(bandeau_texte != NULL);

    habillage_notifier("sentinelle-macros-file-pleine", false);

    lv_obj_t *parent;
    ecran_macros_ctx_t *ctx;
    void *brut;
    construire_ecran(&parent, &ctx, &brut);

    etat_klipper_t etat;
    static const char *const macros[] = { "HOME_ALL" };
    remplir_macros(&etat, macros, 1);
    ECRAN_MACROS.mettre_a_jour(&etat, false, ctx);

    /* Remplit la file (profondeur 4, FILE_PROFONDEUR dans
     * simulateur/source_etat_sim.c) directement via ui_commander(), puis un
     * clic REEL sur la macro pour le 5eme -- lui seul doit rendre
     * ESP_ERR_NO_MEM et faire echouer bouton_macro_cb() de facon
     * SYNCHRONE (avant meme que la commande n'atteigne jamais le backend). */
    VERIFIER(source_etat_sim_file_taille() == 0);
    for (int i = 0; i < 4; i++) {
        VERIFIER(ui_commander(BACKEND_ACTION_PAUSE, NULL) == ESP_OK);
    }
    VERIFIER(source_etat_sim_file_taille() == 4);

    lv_obj_send_event(ctx->boutons[0], LV_EVENT_CLICKED, NULL); /* HOME_ALL */
    VERIFIER(source_etat_sim_file_taille() == 4); /* inchangee : refusee */
    VERIFIER(!lv_obj_has_flag(bandeau, LV_OBJ_FLAG_HIDDEN));
    VERIFIER_TEXTE(lv_label_get_text(bandeau_texte), "Command failed: HOME_ALL");
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(bandeau, 0), lv_color_hex(0xB3352C)));

    source_etat_sim_cycle(); /* draine les 4 PAUSE reellement en file, hygiene de fin de test */

    lv_obj_delete(parent);
    free(brut);
}

void suite_ecran_macros(void)
{
    groupe_construire_arguments();
    groupe_filtrage_et_etats();
    groupe_pagination();
    groupe_grisage();
    groupe_trace_seam();
    groupe_file_pleine();
}
