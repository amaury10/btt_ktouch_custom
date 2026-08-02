/* Sous-projet 6 (browser de fichiers), tâche 3 (dernière tâche) : l'écran
 * fichiers -- voir ecran_fichiers.h pour le contrat. Quatre groupes de
 * preuves, même structure que test_ecran_macros.c (voir son commentaire de
 * tête) :
 *
 *   1. Construction + liste vide ("No files") + remplissage simple + ligne
 *      de troncature -- construction directe du contexte, sans navigation ni
 *      boucle simulée.
 *   2. Pagination (20 fichiers, 2 pages).
 *   3. Grisage systématique sur donnees_perimees -- round-trip complet,
 *      couleurs RÉSOLUES (même lecon que test_ecran_macros.c).
 *   4. Trace du seam : tap -> confirmation OUVERTE (aucun gcode envoyé) ->
 *      confirmer -> BACKEND_ACTION_GCODE avec le script
 *      "SDCARD_PRINT_FILE FILENAME=<nom>" (le NOM RÉELLEMENT AFFICHÉ sur le
 *      bouton) ; décliner -> aucun gcode. RÉUTILISE la boucle simulée et
 *      l'habillage déjà construits par suite_commandes() (singletons
 *      process-wide, voir son commentaire de tête) -- ce fichier DOIT donc
 *      être enregistré APRÈS elle dans tests/main.c. */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "backend.h"
#include "confirmation.h"
#include "ecran_fichiers.h"
#include "etat_klipper.h"
#include "klipper_fichiers.h"
#include "habillage.h"
#include "navigation.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

/* ------------------------------------------------------------------------
 * Petits utilitaires partagés par les groupes.
 * ------------------------------------------------------------------------ */

static void construire_ecran(lv_obj_t **parent_sortie, ecran_fichiers_ctx_t **ctx_sortie, void **brut_sortie)
{
    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_FICHIERS.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_fichiers_ctx_t *ctx = (ecran_fichiers_ctx_t *)brut;
    ECRAN_FICHIERS.construire(parent, ctx);
    *parent_sortie = parent;
    *ctx_sortie = ctx;
    *brut_sortie = brut;
}

/* Injecte `noms[0..n-1]` (+ `tronques`) dans le store dedie de fichiers
 * (klipper_fichiers.h) : la liste de fichiers ne vit plus dans etat_klipper_t
 * (sortie du champ `fichiers[]` pour liberer la RAM interne), c'est desormais
 * ce store que ecran_fichiers.mettre_a_jour() relit. `n == 0` (noms peut etre
 * NULL) vide le store. */
static void definir_store_fichiers(const char *const *noms, uint8_t n, bool tronques)
{
    klipper_fichiers_t f;
    memset(&f, 0, sizeof(f));
    for (uint8_t i = 0; i < n; i++) {
        snprintf(f.noms[i], KLIPPER_FICHIER_MAX, "%s", noms[i]);
    }
    f.nb = n;
    f.tronques = tronques;
    klipper_fichiers_definir(&f);
}

/* Injecte `noms` dans le store (tronques=false) et remet `e` a zero -- `e`
 * reste passe a mettre_a_jour() (qui exige un etat non-NULL) meme s'il ne
 * porte plus les fichiers. Petit constructeur partage par les groupes 1 et 2,
 * meme technique que remplir_macros() dans test_ecran_macros.c. */
static void remplir_fichiers(etat_klipper_t *e, const char *const *noms, uint8_t n)
{
    memset(e, 0, sizeof(*e));
    definir_store_fichiers(noms, n, false);
}

/* Memes helpers de parcours d'arbre que test_commandes.c/test_integration_rail.c :
 * confirmation.h reste opaque sur ses lv_obj_t* internes, chaque fichier de
 * test qui a besoin d'y entrer les retrouve par parcours (aucune API
 * partagee creee pour l'occasion). Redefinis ici, meme politique que les
 * autres. */
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

/* Meme helper que test_ecran_macros.c/test_integration_rail.c : fait avancer
 * l'horloge LVGL pour que la couleur/bordure RESOLUE reflete un
 * lv_obj_add_state() deja applique de facon synchrone (transition de theme). */
static void pomper_transitions_style(void)
{
    for (int i = 0; i < 5; i++) {
        lv_tick_inc(100);
        lv_timer_handler();
    }
}

/* ------------------------------------------------------------------------
 * Groupe 1 : construction, liste vide ("No files"), remplissage simple,
 * ligne de troncature.
 * ------------------------------------------------------------------------ */

static void groupe_construction_et_etats(void)
{
    printf("suite : ecran fichiers (construction, vide, remplissage, troncature)\n");

    lv_obj_t *parent;
    ecran_fichiers_ctx_t *ctx;
    void *brut;
    construire_ecran(&parent, &ctx, &brut);

    /* --- tous les emplacements sont crees, la ligne d'avertissement et le
     * texte "vide" existent, les deux boutons de pagination et le libelle de
     * page aussi. */
    for (uint8_t i = 0; i < ECRAN_FICHIERS_PAGE_TAILLE; i++) {
        VERIFIER(ctx->boutons[i] != NULL);
        VERIFIER(ctx->labels[i] != NULL);
    }
    VERIFIER(ctx->avertissement != NULL);
    VERIFIER(ctx->vide != NULL);
    VERIFIER(ctx->bouton_precedent != NULL);
    VERIFIER(ctx->bouton_suivant != NULL);
    VERIFIER(ctx->page_label != NULL);

    /* --- etat entierement a zero (nb_fichiers=0) : "No files", jamais un
     * ecran muet -- l'instantane du dernier connect peut ne contenir aucun
     * fichier, ce n'est pas une erreur (brief). Aucun bouton visible,
     * pagination masquee. */
    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    definir_store_fichiers(NULL, 0, false); /* store vide */
    ECRAN_FICHIERS.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_obj_has_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN));
    VERIFIER_TEXTE(lv_label_get_text(ctx->vide), "No files");
    for (uint8_t i = 0; i < ECRAN_FICHIERS_PAGE_TAILLE; i++) {
        VERIFIER(lv_obj_has_flag(ctx->boutons[i], LV_OBJ_FLAG_HIDDEN));
    }
    VERIFIER(lv_obj_has_flag(ctx->bouton_precedent, LV_OBJ_FLAG_HIDDEN));
    VERIFIER(lv_obj_has_flag(ctx->bouton_suivant, LV_OBJ_FLAG_HIDDEN));

    /* --- deux fichiers, dont un chemin de sous-dossier (T2 : le nom peut
     * contenir un '/', affiche tel quel, aucune navigation de dossiers -- voir
     * le commentaire de tete de ecran_fichiers.h). */
    static const char *const fichiers[] = { "a.gcode", "sub/b.gcode" };
    remplir_fichiers(&etat, fichiers, 2);
    ECRAN_FICHIERS.mettre_a_jour(&etat, false, ctx);

    VERIFIER(lv_obj_has_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN)); /* plus "vide" */
    VERIFIER(ctx->nb_fichiers == 2);
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[0]), "a.gcode");
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[1]), "sub/b.gcode");
    VERIFIER(!lv_obj_has_flag(ctx->boutons[0], LV_OBJ_FLAG_HIDDEN));
    VERIFIER(!lv_obj_has_flag(ctx->boutons[1], LV_OBJ_FLAG_HIDDEN));
    /* Emplacement 2 (le 3eme) est inutilise -- masque, pas un ancien libelle
     * qui trainerait. */
    VERIFIER(lv_obj_has_flag(ctx->boutons[2], LV_OBJ_FLAG_HIDDEN));
    /* Une seule page (2 <= 16) : pagination masquee. */
    VERIFIER(lv_obj_has_flag(ctx->bouton_precedent, LV_OBJ_FLAG_HIDDEN));
    VERIFIER(lv_obj_has_flag(ctx->bouton_suivant, LV_OBJ_FLAG_HIDDEN));
    /* Pas de troncature dans ce scenario : la ligne reste masquee. */
    VERIFIER(lv_obj_has_flag(ctx->avertissement, LV_OBJ_FLAG_HIDDEN));

    /* --- tronques : ligne d'avertissement HONNETE, visible (le drapeau vient
     * desormais du store, pas de l'etat). */
    definir_store_fichiers(fichiers, 2, true);
    ECRAN_FICHIERS.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_obj_has_flag(ctx->avertissement, LV_OBJ_FLAG_HIDDEN));
    VERIFIER_TEXTE(lv_label_get_text(ctx->avertissement), "Some files are not shown (list truncated)");

    /* Redevient faux : la ligne redisparait -- reversible, pas a sens
     * unique (meme lecon que le grisage). */
    definir_store_fichiers(fichiers, 2, false);
    ECRAN_FICHIERS.mettre_a_jour(&etat, false, ctx);
    VERIFIER(lv_obj_has_flag(ctx->avertissement, LV_OBJ_FLAG_HIDDEN));

    lv_obj_delete(parent);
    free(brut);
}

/* ------------------------------------------------------------------------
 * Groupe 2 : pagination (20 fichiers -> 2 pages).
 * ------------------------------------------------------------------------ */

static void groupe_pagination(void)
{
    printf("suite : ecran fichiers (pagination)\n");

    lv_obj_t *parent;
    ecran_fichiers_ctx_t *ctx;
    void *brut;
    construire_ecran(&parent, &ctx, &brut);

    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    char noms[20][KLIPPER_FICHIER_MAX];
    const char *pointeurs[20];
    for (uint8_t i = 0; i < 20; i++) {
        snprintf(noms[i], sizeof(noms[i]), "FILE_%02u.gcode", (unsigned)(i + 1));
        pointeurs[i] = noms[i];
    }
    remplir_fichiers(&etat, pointeurs, 20);
    ECRAN_FICHIERS.mettre_a_jour(&etat, false, ctx);

    VERIFIER(ctx->nb_fichiers == 20);
    VERIFIER(!lv_obj_has_flag(ctx->bouton_precedent, LV_OBJ_FLAG_HIDDEN));
    VERIFIER(!lv_obj_has_flag(ctx->bouton_suivant, LV_OBJ_FLAG_HIDDEN));
    VERIFIER_TEXTE(lv_label_get_text(ctx->page_label), "Page 1/2");
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[0]), "FILE_01.gcode");
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[15]), "FILE_16.gcode");
    /* Precedent desactive (premiere page), Suivant actif. */
    VERIFIER(lv_obj_has_state(ctx->bouton_precedent, LV_STATE_DISABLED));
    VERIFIER(!lv_obj_has_state(ctx->bouton_suivant, LV_STATE_DISABLED));

    /* --- Suivant : page 2, 4 fichiers restants (17..20) dans les 4 premiers
     * emplacements, le reste masque. */
    lv_obj_send_event(ctx->bouton_suivant, LV_EVENT_CLICKED, NULL);
    VERIFIER_TEXTE(lv_label_get_text(ctx->page_label), "Page 2/2");
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[0]), "FILE_17.gcode");
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[3]), "FILE_20.gcode");
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
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[0]), "FILE_01.gcode");

    lv_obj_delete(parent);
    free(brut);
}

/* ------------------------------------------------------------------------
 * Groupe 3 : grisage systematique (donnees_perimees), couleurs RESOLUES.
 * ------------------------------------------------------------------------ */

static void groupe_grisage(void)
{
    printf("suite : ecran fichiers (grisage resolu)\n");

    lv_obj_t *parent;
    ecran_fichiers_ctx_t *ctx;
    void *brut;
    construire_ecran(&parent, &ctx, &brut);

    etat_klipper_t etat;
    static const char *const fichiers[] = { "a.gcode", "sub/b.gcode" };
    remplir_fichiers(&etat, fichiers, 2);
    ECRAN_FICHIERS.mettre_a_jour(&etat, false, ctx);
    pomper_transitions_style();

    lv_color_t fond_actif = lv_obj_get_style_bg_color(ctx->boutons[0], LV_PART_MAIN);
    lv_color_t texte_actif = lv_obj_get_style_text_color(ctx->labels[0], LV_PART_MAIN);

    /* --- perime : grise, couleur RESOLUE differente (meme lecon que
     * test_ecran_macros.c -- LV_STATE_DISABLED seul est pixel-identique a
     * l'etat arme). */
    ECRAN_FICHIERS.mettre_a_jour(&etat, true, ctx);
    VERIFIER(lv_obj_has_state(ctx->boutons[0], LV_STATE_DISABLED));
    pomper_transitions_style();
    VERIFIER(!lv_color_eq(lv_obj_get_style_bg_color(ctx->boutons[0], LV_PART_MAIN), fond_actif));
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(ctx->labels[0], LV_PART_MAIN), texte_actif));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->labels[0], LV_PART_MAIN), lv_color_hex(0x6B7280)));

    /* Un clic direct sur un bouton grise ne doit RIEN ouvrir (garde
     * defensive de bouton_fichier_cb() -- lv_obj_send_event() ne passe
     * jamais par la verification tactile de LV_STATE_DISABLED). */
    VERIFIER(!confirmation_est_ouverte());
    lv_obj_send_event(ctx->boutons[0], LV_EVENT_CLICKED, NULL);
    VERIFIER(!confirmation_est_ouverte());

    /* --- redevient normal : couleur RESOLUE EXACTEMENT celle de depart. */
    ECRAN_FICHIERS.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_obj_has_state(ctx->boutons[0], LV_STATE_DISABLED));
    pomper_transitions_style();
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(ctx->boutons[0], LV_PART_MAIN), fond_actif));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(ctx->labels[0], LV_PART_MAIN), texte_actif));

    lv_obj_delete(parent);
    free(brut);
}

/* ------------------------------------------------------------------------
 * Groupe 4 : trace du seam -- tap -> confirmation -> ui_commander().
 * ------------------------------------------------------------------------ */

static void groupe_trace_seam(void)
{
    printf("suite : ecran fichiers (trace du seam -> confirmation -> backend)\n");

    /* Meme garde d'ordonnancement que groupe_trace_seam() dans
     * test_ecran_macros.c (voir son commentaire de tete) : reutilise les
     * DEUX singletons process-wide (habillage, boucle simulee) deja etablis
     * par les suites precedentes. */
    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: groupe_trace_seam() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    navigation_init(lv_screen_active());

    lv_obj_t *parent;
    ecran_fichiers_ctx_t *ctx;
    void *brut;
    construire_ecran(&parent, &ctx, &brut);

    etat_klipper_t etat;
    static const char *const fichiers[] = { "a.gcode", "sub/b.gcode" };
    remplir_fichiers(&etat, fichiers, 2);
    ECRAN_FICHIERS.mettre_a_jour(&etat, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[0]), "a.gcode");
    VERIFIER_TEXTE(lv_label_get_text(ctx->labels[1]), "sub/b.gcode");

    char action[32];
    char arguments[192];

    /* --- tap sur "a.gcode" : ouvre une confirmation, N'ENVOIE RIEN tant que
     * non confirme. */
    VERIFIER(!confirmation_est_ouverte());
    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[0], LV_EVENT_CLICKED, NULL); /* "a.gcode" */
    VERIFIER(confirmation_est_ouverte());
    VERIFIER(source_etat_sim_file_taille() == avant); /* rien envoye avant confirmation */

    lv_obj_t *mbox = dernier_msgbox();
    VERIFIER(mbox != NULL);
    VERIFIER_TEXTE(lv_label_get_text(lv_msgbox_get_title(mbox)), "Print file?");
    lv_obj_t *pied = lv_msgbox_get_footer(mbox);
    VERIFIER(pied != NULL);
    lv_obj_t *bouton_decliner = lv_obj_get_child(pied, 0);
    lv_obj_t *bouton_action = lv_obj_get_child(pied, 1);
    VERIFIER(bouton_decliner != NULL);
    VERIFIER(bouton_action != NULL);
    /* destructif=false (voir ecran_fichiers.c) : pas de rouge, contrairement
     * a E-STOP/Cancel print. */
    VERIFIER(!lv_color_eq(lv_obj_get_style_bg_color(bouton_action, 0), lv_color_hex(0xE74C3C)));

    /* --- confirmer : le script part EXACTEMENT pour "a.gcode", le nom
     * REELLEMENT affiche sur ce bouton au moment du tap. */
    lv_obj_send_event(bouton_action, LV_EVENT_CLICKED, NULL);
    /* Le rappel de confirmation.c est SYNCHRONE (voir fermer_et_rappeler()) :
     * la commande est deja en file ici, sans lv_timer_handler() prealable --
     * meme constat que test_integration_rail.c pour STOP. */
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    VERIFIER(strstr(arguments, "\"script\":\"SDCARD_PRINT_FILE FILENAME=a.gcode\"") != NULL);
    lv_timer_handler();      /* acheve la fermeture asynchrone du dialogue */
    source_etat_sim_cycle(); /* draine avant la suite */
    VERIFIER(!confirmation_est_ouverte());

    /* --- tap sur "sub/b.gcode", DECLIN : n'envoie rien -- le chemin de
     * sous-dossier ('/') est transmis tel quel dans le message de
     * confirmation, aucune navigation de dossiers. */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[1], LV_EVENT_CLICKED, NULL); /* "sub/b.gcode" */
    VERIFIER(confirmation_est_ouverte());
    mbox = dernier_msgbox();
    VERIFIER(mbox != NULL);
    /* lv_msgbox_get_content() rend le CONTENEUR du message (mbox->content,
     * voir lv_msgbox.c) -- le label ajoute par lv_msgbox_add_text() en est
     * le premier enfant, jamais le conteneur lui-meme (contrairement au
     * titre, ou lv_msgbox_get_title() rend directement le label -- voir
     * lv_msgbox_add_title()). */
    lv_obj_t *contenu = lv_obj_get_child(lv_msgbox_get_content(mbox), 0);
    VERIFIER(contenu != NULL);
    VERIFIER_TEXTE(lv_label_get_text(contenu), "sub/b.gcode");
    pied = lv_msgbox_get_footer(mbox);
    bouton_decliner = lv_obj_get_child(pied, 0);
    VERIFIER(bouton_decliner != NULL);
    lv_obj_send_event(bouton_decliner, LV_EVENT_CLICKED, NULL);
    lv_timer_handler();
    VERIFIER(source_etat_sim_file_taille() == avant); /* rien envoye sur un declin */
    VERIFIER(!confirmation_est_ouverte());

    lv_obj_delete(parent);
    free(brut);
}

void suite_ecran_fichiers(void)
{
    groupe_construction_et_etats();
    groupe_pagination();
    groupe_grisage();
    groupe_trace_seam();
}
