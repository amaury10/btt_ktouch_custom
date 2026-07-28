/* Suite de la tâche 8 : l'écran de première configuration (voir
 * ecran_configuration.h pour le contrat, et task-8-brief.md pour les
 * scénarios exigés).
 *
 * Deux familles de tests, pour deux raisons différentes de rester PC-testable :
 * - ecran_configuration_valider() est une fonction PURE (pas de LVGL, pas de
 *   NVS) : testée directement, en dérivant les cas de hote_parse.c (déjà
 *   exhaustivement testé au jalon 2a), jamais en dupliquant sa logique.
 * - le reste (préremplissage, clavier, bouton Save) passe par la façade
 *   ui/source_reglages.h plutôt que core/reglages.h directement : reglages.c
 *   inclut nvs.h (ESP-IDF) et ne se lie jamais sur PC (voir source_reglages.h
 *   pour le détail) -- simulateur/source_reglages_sim.c, réutilisé ici comme
 *   source_etat_sim.c l'est déjà, stocke dans une variable statique de
 *   fichier que ce test relit directement, sans jamais toucher la NVS.
 *
 * Construction DIRECTE (calloc + ECRAN_CONFIGURATION.construire()), comme
 * test_ecran_accueil.c : ce fichier teste le contrat de CET écran, pas celui
 * de la pile de navigation (déjà couverte par test_navigation.c). Pour les
 * sections 5/6 (bouton Save), qui doivent prouver que navigation_accueil()
 * et habillage_notifier() sont réellement appelés, un écran-jouet est empilé
 * séparément via navigation_empiler() -- la pile de navigation.c n'a besoin
 * de contenir aucun écran particulier pour que ces deux fonctions livrent
 * leurs effets observables (navigation_profondeur(), l'objet bandeau) ; ce
 * n'est pas le MÊME ctx que la construction directe qui déclenche le clic,
 * et c'est délibéré : ce test vérifie que le rappel appelle bien les deux
 * fonctions réelles, pas que cette instance précise d'écran est enregistrée
 * dans la pile au moment du clic. */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "backend.h"
#include "clavier.h"
#include "ecran_configuration.h"
#include "habillage.h"
#include "hote_parse.h"
#include "navigation.h"
#include "petit_test.h"
#include "source_reglages.h"

/* --- Helpers de traversal, dupliqués de test_clavier.c (chaque fichier de
 * test garde ses petits helpers statiques, même politique que
 * test_clavier.c -- pas d'API partagée créée pour la seule occasion des
 * tests). --- */
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

static lv_obj_t *dernier_enfant_calque_superieur(void)
{
    lv_obj_t *calque = lv_layer_top();
    uint32_t n = lv_obj_get_child_count(calque);
    if (n == 0) {
        return NULL;
    }
    return lv_obj_get_child(calque, n - 1);
}

/* Écran-jouet vide, empilé uniquement pour donner à navigation_accueil() une
 * pile non triviale à réduire (voir le commentaire de tête). */
static void ecran_jouet_construire(lv_obj_t *parent, void *ctx)
{
    (void)ctx;
    lv_label_create(parent);
}

static const ecran_desc_t ECRAN_JOUET_CONFIG = {
    .id = "jouet-config", .titre = "Jouet", .taille_contexte = 0,
    .construire = ecran_jouet_construire, .mettre_a_jour = NULL, .detruire = NULL,
};

static void section_validation(void)
{
    printf("suite : ecran configuration (validation)\n");

    backend_hote_t h;
    char erreur[64];

    /* vide : refuse, message d'erreur ecrit */
    erreur[0] = '\0';
    VERIFIER(ecran_configuration_valider("", &h, erreur, sizeof(erreur)) == false);
    VERIFIER(erreur[0] != '\0');

    /* NULL : refuse, ne plante pas */
    VERIFIER(ecran_configuration_valider(NULL, &h, erreur, sizeof(erreur)) == false);

    /* adresse seule, sans ':' : port par defaut applique (brief : "accepte
     * 192.168.1.50") -- contrairement a hote_parse() seul, qui rejette
     * l'absence totale de ':' (voir test_hote_parse.c, "sansport"). */
    memset(&h, 0, sizeof(h));
    VERIFIER(ecran_configuration_valider("192.168.1.50", &h, erreur, sizeof(erreur)) == true);
    VERIFIER_TEXTE(h.adresse, "192.168.1.50");
    VERIFIER(h.port == HOTE_PARSE_PORT_DEFAUT);

    /* adresse:port classique (brief : "accepte klipper.local:7125") */
    memset(&h, 0, sizeof(h));
    VERIFIER(ecran_configuration_valider("klipper.local:7125", &h, erreur, sizeof(erreur)) == true);
    VERIFIER_TEXTE(h.adresse, "klipper.local");
    VERIFIER(h.port == 7125);

    /* litteral IPv6 avec port : delegue integralement a hote_parse(), meme
     * regle "dernier ':'" (voir test_hote_parse.c). */
    memset(&h, 0, sizeof(h));
    VERIFIER(ecran_configuration_valider("fe80::1:8080", &h, erreur, sizeof(erreur)) == true);
    VERIFIER_TEXTE(h.adresse, "fe80::1");
    VERIFIER(h.port == 8080);

    /* port non numerique : hote_parse() ne rejette PAS ce cas, il retombe
     * sur le port par defaut sans toucher a l'adresse (voir
     * test_hote_parse.c, "hote.local:abc") -- ce n'est donc PAS un cas de
     * refus, meme si ca peut sembler contre-intuitif. */
    memset(&h, 0, sizeof(h));
    VERIFIER(ecran_configuration_valider("klipper.local:abc", &h, erreur, sizeof(erreur)) == true);
    VERIFIER_TEXTE(h.adresse, "klipper.local");
    VERIFIER(h.port == HOTE_PARSE_PORT_DEFAUT);

    /* ':' en tout debut : adresse vide malgre un port numeriquement valide,
     * refuse (meme cas que test_hote_parse.c, hote_parse(":1234") == false). */
    erreur[0] = '\0';
    VERIFIER(ecran_configuration_valider(":1234", &h, erreur, sizeof(erreur)) == false);
    VERIFIER(erreur[0] != '\0');

    /* hote trop long (>= BACKEND_HOTE_LONGUEUR_MAX), sans port : refuse. */
    char long_sans_port[BACKEND_HOTE_LONGUEUR_MAX + 32];
    memset(long_sans_port, 'x', sizeof(long_sans_port) - 1);
    long_sans_port[sizeof(long_sans_port) - 1] = '\0';
    erreur[0] = '\0';
    VERIFIER(ecran_configuration_valider(long_sans_port, &h, erreur, sizeof(erreur)) == false);
    VERIFIER(erreur[0] != '\0');

    /* meme hote trop long, avec port explicite cette fois : refuse aussi
     * (meme regle que test_hote_parse.c -- adresse trop longue, la chaine
     * entiere est jugee inexploitable, peu importe le port). */
    char long_avec_port[BACKEND_HOTE_LONGUEUR_MAX + 40];
    snprintf(long_avec_port, sizeof(long_avec_port), "%s:1234", long_sans_port);
    VERIFIER(ecran_configuration_valider(long_avec_port, &h, erreur, sizeof(erreur)) == false);

    /* saisie grotesquement longue (au-dela de CLAVIER_VALEUR_MAX, jamais
     * produite par le clavier tactile lui-meme mais cette fonction reste
     * appelable avec n'importe quelle chaine) : refuse via le controle de
     * troncature, pas par hote_parse(). */
    char enorme[CLAVIER_VALEUR_MAX + 64];
    memset(enorme, 'y', sizeof(enorme) - 1);
    enorme[sizeof(enorme) - 1] = '\0';
    erreur[0] = '\0';
    VERIFIER(ecran_configuration_valider(enorme, &h, erreur, sizeof(erreur)) == false);
    VERIFIER(erreur[0] != '\0');

    /* hote_sortie NULL : ne plante pas, refuse juste d'ecrire */
    VERIFIER((ecran_configuration_valider("192.168.1.1", NULL, erreur, sizeof(erreur)), true));
}

static void section_construction_non_configure(void)
{
    printf("suite : ecran configuration (construction, non configure)\n");

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_CONFIGURATION.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_configuration_ctx_t *ctx = (ecran_configuration_ctx_t *)brut;

    ECRAN_CONFIGURATION.construire(parent, ctx);
    VERIFIER(ctx->valeur_label != NULL);
    VERIFIER(ctx->bouton_modifier != NULL);
    VERIFIER(ctx->dropdown_type != NULL);
    VERIFIER(ctx->bouton_enregistrer != NULL);

    /* aucun hote jamais enregistre a ce stade (premiere suite a toucher
     * ui_reglages_*, voir le commentaire de tete) : champ vide, placeholder
     * affiche plutot qu'une chaine vide silencieuse. */
    VERIFIER_TEXTE(ctx->saisie, "");
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeur_label), "Not configured");

    /* selecteur de machine : une seule entree, pas d'entree factice (brief). */
    VERIFIER_TEXTE(lv_dropdown_get_options(ctx->dropdown_type), "Klipper / Moonraker");
    char selection[32];
    lv_dropdown_get_selected_str(ctx->dropdown_type, selection, sizeof(selection));
    VERIFIER_TEXTE(selection, "Klipper / Moonraker");

    lv_obj_delete(parent);
    free(brut);
}

static void section_construction_prerempli(void)
{
    printf("suite : ecran configuration (construction, prerempli)\n");

    backend_hote_t existant = { .adresse = "192.168.1.77", .port = 7125 };
    VERIFIER(ui_reglages_definir_hote(&existant) == ESP_OK);

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_CONFIGURATION.taille_contexte);
    ecran_configuration_ctx_t *ctx = (ecran_configuration_ctx_t *)brut;
    ECRAN_CONFIGURATION.construire(parent, ctx);

    VERIFIER_TEXTE(ctx->saisie, "192.168.1.77:7125");
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeur_label), "192.168.1.77:7125");

    lv_obj_delete(parent);
    free(brut);
}

static void section_clavier(void)
{
    printf("suite : ecran configuration (clavier)\n");

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_CONFIGURATION.taille_contexte);
    ecran_configuration_ctx_t *ctx = (ecran_configuration_ctx_t *)brut;
    ECRAN_CONFIGURATION.construire(parent, ctx);
    /* Etat de depart connu, independant de ce que la section precedente a pu
     * enregistrer (ui_reglages_definir_hote() est global au binaire). */
    snprintf(ctx->saisie, sizeof(ctx->saisie), "%s", "192.168.1.1:7125");
    lv_label_set_text(ctx->valeur_label, ctx->saisie);

    VERIFIER(dernier_enfant_calque_superieur() == NULL);
    lv_obj_send_event(ctx->bouton_modifier, LV_EVENT_CLICKED, NULL);

    lv_obj_t *racine_clavier = dernier_enfant_calque_superieur();
    VERIFIER(racine_clavier != NULL);
    lv_obj_t *ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    lv_obj_t *kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    VERIFIER(ta != NULL);
    VERIFIER(kb != NULL);
    /* pre-rempli avec la valeur courante du champ, pas une chaine vide */
    VERIFIER_TEXTE(lv_textarea_get_text(ta), "192.168.1.1:7125");

    lv_textarea_set_text(ta, "newhost.local:1234");
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    lv_timer_handler(); /* ferme reellement le clavier (destruction asynchrone) */

    VERIFIER_TEXTE(ctx->saisie, "newhost.local:1234");
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeur_label), "newhost.local:1234");
    VERIFIER(dernier_enfant_calque_superieur() == NULL);

    lv_obj_delete(parent);
    free(brut);
}

static void section_enregistrer(void)
{
    printf("suite : ecran configuration (bouton Save)\n");

    /* Une seule habillage_construire() par binaire de tests (singleton, voir
     * habillage.h) : cette suite est la derniere du harnais (voir
     * tests/main.c), aucune autre n'appelle habillage_construire() avant
     * elle. */
    habillage_construire(lv_screen_active());
    lv_obj_t *bandeau = lv_obj_get_child(lv_screen_active(), lv_obj_get_child_count(lv_screen_active()) - 1);
    VERIFIER(bandeau != NULL);

    VERIFIER(navigation_empiler(&ECRAN_JOUET_CONFIG) == ESP_OK);
    VERIFIER(navigation_empiler(&ECRAN_JOUET_CONFIG) == ESP_OK);
    VERIFIER(navigation_profondeur() == 2);

    /* --- saisie invalide : notifie une erreur, n'enregistre rien, ne
     * navigue nulle part (brief : "reste sur l'ecran ; il ne ferme rien"). */
    {
        backend_hote_t avant;
        bool configure_avant = ui_reglages_hote(&avant);

        lv_obj_t *parent = lv_obj_create(lv_screen_active());
        void *brut = calloc(1, ECRAN_CONFIGURATION.taille_contexte);
        ecran_configuration_ctx_t *ctx = (ecran_configuration_ctx_t *)brut;
        ECRAN_CONFIGURATION.construire(parent, ctx);
        ctx->saisie[0] = '\0'; /* invalide : vide */

        lv_obj_send_event(ctx->bouton_enregistrer, LV_EVENT_CLICKED, NULL);

        backend_hote_t apres;
        bool configure_apres = ui_reglages_hote(&apres);
        VERIFIER(configure_apres == configure_avant);
        VERIFIER_TEXTE(apres.adresse, avant.adresse);
        VERIFIER(apres.port == avant.port);

        VERIFIER(navigation_profondeur() == 2);

        VERIFIER(!lv_obj_has_flag(bandeau, LV_OBJ_FLAG_HIDDEN));
        VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(bandeau, 0), lv_color_hex(0xB3352C)));

        lv_obj_delete(parent);
        free(brut);
    }

    /* --- saisie valide : enregistre, notifie un succes, revient au fond de
     * la pile (brief : reglages_definir_hote(), habillage_notifier("Settings
     * saved", false), navigation_accueil() -- dans cet ordre). */
    {
        lv_obj_t *parent = lv_obj_create(lv_screen_active());
        void *brut = calloc(1, ECRAN_CONFIGURATION.taille_contexte);
        ecran_configuration_ctx_t *ctx = (ecran_configuration_ctx_t *)brut;
        ECRAN_CONFIGURATION.construire(parent, ctx);
        snprintf(ctx->saisie, sizeof(ctx->saisie), "%s", "192.168.1.200:7125");

        lv_obj_send_event(ctx->bouton_enregistrer, LV_EVENT_CLICKED, NULL);

        backend_hote_t apres;
        VERIFIER(ui_reglages_hote(&apres) == true);
        VERIFIER_TEXTE(apres.adresse, "192.168.1.200");
        VERIFIER(apres.port == 7125);

        /* navigation_accueil() a bien reduit la pile a son fond */
        VERIFIER(navigation_profondeur() == 1);

        VERIFIER(!lv_obj_has_flag(bandeau, LV_OBJ_FLAG_HIDDEN));
        VERIFIER(!lv_color_eq(lv_obj_get_style_bg_color(bandeau, 0), lv_color_hex(0xB3352C)));

        lv_obj_delete(parent);
        free(brut);
    }
}

void suite_ecran_configuration(void)
{
    section_validation();
    section_construction_non_configure();
    section_construction_prerempli();
    section_clavier();
    section_enregistrer();
}
