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
#include "ecran_accueil.h"
#include "ecran_configuration.h"
#include "habillage.h"
#include "hote_parse.h"
#include "navigation.h"
#include "petit_test.h"
#include "source_reglages.h"
#include "source_reglages_sim.h"

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

/* Dernier enfant NON masque (LV_OBJ_FLAG_HIDDEN) de `parent`, en partant de
 * la fin. Nécessaire pour retrouver l'écran empilé au sommet de la pile de
 * navigation.c parmi les enfants de lv_screen_active() : "le dernier enfant
 * tout court" ne suffit PAS dès qu'un lv_dropdown vit sur cet écran, voir le
 * commentaire de section_topologie_reelle(). */
static lv_obj_t *dernier_enfant_visible(lv_obj_t *parent)
{
    if (parent == NULL) {
        return NULL;
    }
    uint32_t n = lv_obj_get_child_count(parent);
    for (uint32_t i = n; i > 0; i--) {
        lv_obj_t *enfant = lv_obj_get_child(parent, i - 1);
        if (!lv_obj_has_flag(enfant, LV_OBJ_FLAG_HIDDEN)) {
            return enfant;
        }
    }
    return NULL;
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

    /* vide : refuse, message d'erreur ecrit -- texte EXACT verifie (pas
     * seulement "non vide") pour distinguer ce cas d'une saisie non-vide
     * mais invalide (revue tache 8, round 2, Q3 : l'egalite de texte, pas
     * juste sa non-vacuite, est ce qui prouve que la branche "vide" a
     * vraiment ete prise plutot qu'une autre branche de refus). */
    erreur[0] = '\0';
    VERIFIER(ecran_configuration_valider("", &h, erreur, sizeof(erreur)) == false);
    VERIFIER_TEXTE(erreur, "Printer address cannot be empty");

    /* NULL : refuse, ne plante pas */
    VERIFIER(ecran_configuration_valider(NULL, &h, erreur, sizeof(erreur)) == false);

    /* adresse seule, sans ':' : port par defaut applique (brief : "accepte
     * 192.168.1.50") -- contrairement a hote_parse() seul, qui rejette
     * l'absence totale de ':' (voir test_hote_parse.c, "sansport"). */
    memset(&h, 0, sizeof(h));
    VERIFIER(ecran_configuration_valider("192.168.1.50", &h, erreur, sizeof(erreur)) == true);
    VERIFIER_TEXTE(h.adresse, "192.168.1.50");
    VERIFIER(h.port == HOTE_PARSE_PORT_DEFAUT);

    /* Contournement de l'espace via la synthese du port par defaut (revue
     * tache 8, round 2, IMPORTANT) : "192.168.1.50 " (espace de FIN, sans
     * ':') est synthetise en "192.168.1.50 :7125" AVANT d'etre remis a
     * hote_parse() -- l'espace de fin de la saisie BRUTE devient alors
     * INTERIEUR a la chaine synthetisee, hors de portee du garde-fou de
     * bordure de hote_parse() (qui n'inspecte que le premier/dernier
     * caractere de ce qu'on lui donne). Sans le controle ci-dessous, ce cas
     * est ACCEPTE avec une adresse " 192.168.1.50 " -- persistee en NVS,
     * en echec permanent, "Settings saved" mentant sur ce qui vient d'etre
     * enregistre (S5.3). Verifie qu'un espace de tete reste lui rejete (il
     * reste en bordure, deja couvert par round 1) : la difference de
     * traitement entre les deux est exactement le bug. */
    erreur[0] = '\0';
    VERIFIER(ecran_configuration_valider("192.168.1.50 ", &h, erreur, sizeof(erreur)) == false);
    VERIFIER(erreur[0] != '\0');
    erreur[0] = '\0';
    VERIFIER(ecran_configuration_valider("192.168.1.50\t", &h, erreur, sizeof(erreur)) == false);
    erreur[0] = '\0';
    VERIFIER(ecran_configuration_valider(" 192.168.1.50", &h, erreur, sizeof(erreur)) == false);

    /* Espace INTERIEUR, sans ':' ("my printer") : jamais protege par aucune
     * bordure, avant ou apres synthese -- pas davantage un hote exploitable
     * qu'un espace en bordure, meme regle unique appliquee a la saisie
     * BRUTE avant toute synthese (revue tache 8, round 2). */
    erreur[0] = '\0';
    VERIFIER(ecran_configuration_valider("my printer", &h, erreur, sizeof(erreur)) == false);
    VERIFIER(erreur[0] != '\0');

    /* Espace interieur AVEC ':' ("my printer:7125") : cette saisie evite la
     * branche de synthese (elle contient deja un ':'), donc la garde
     * d'espaces posee au round 2 dans la branche sans ':' ne la voyait pas ;
     * et une fois chez hote_parse(), l'espace est interieur a l'adresse
     * decoupee ("my printer"), hors de portee de son garde-fou de bordure.
     * Meme classe de trou que le contournement par synthese, par l'autre
     * branche (revue tache 8, round 2 bis, trouve par le coordinateur). */
    erreur[0] = '\0';
    VERIFIER(ecran_configuration_valider("my printer:7125", &h, erreur, sizeof(erreur)) == false);
    VERIFIER(erreur[0] != '\0');

    /* adresse:port classique (brief : "accepte klipper.local:7125") */
    memset(&h, 0, sizeof(h));
    VERIFIER(ecran_configuration_valider("klipper.local:7125", &h, erreur, sizeof(erreur)) == true);
    VERIFIER_TEXTE(h.adresse, "klipper.local");
    VERIFIER(h.port == 7125);

    /* litteral IPv6 SANS crochets : ambigu, rejete -- delegue integralement
     * a hote_parse() (revue tache 8, round 1 : voir test_hote_parse.c pour
     * le detail de pourquoi "fe80::1:8080" ne peut plus etre distingue de
     * "a:b:c"). */
    memset(&h, 0, sizeof(h));
    VERIFIER(ecran_configuration_valider("fe80::1:8080", &h, erreur, sizeof(erreur)) == false);

    /* litteral IPv6 ENTRE CROCHETS (RFC 3986) : la forme exploitable,
     * delegue elle aussi integralement a hote_parse(). */
    memset(&h, 0, sizeof(h));
    VERIFIER(ecran_configuration_valider("[fe80::1]:8080", &h, erreur, sizeof(erreur)) == true);
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
     * refuse (meme cas que test_hote_parse.c, hote_parse(":1234") == false).
     * Texte exact verifie : distinct du message "vide" ci-dessus, meme si
     * l'adresse resultante est elle aussi vide -- la CAUSE differe (saisie
     * non vide mais hote_parse() la juge inexploitable). */
    erreur[0] = '\0';
    VERIFIER(ecran_configuration_valider(":1234", &h, erreur, sizeof(erreur)) == false);
    VERIFIER_TEXTE(erreur, "Printer address is not valid");

    /* hote trop long (>= BACKEND_HOTE_LONGUEUR_MAX), sans port : refuse par
     * hote_parse() lui-meme (adresse trop longue pour BACKEND_HOTE_LONGUEUR_MAX),
     * pas par le controle de troncature de cette fonction -- meme message
     * que ":1234" ci-dessus puisque c'est la meme cause (hote_parse() rend
     * faux). */
    char long_sans_port[BACKEND_HOTE_LONGUEUR_MAX + 32];
    memset(long_sans_port, 'x', sizeof(long_sans_port) - 1);
    long_sans_port[sizeof(long_sans_port) - 1] = '\0';
    erreur[0] = '\0';
    VERIFIER(ecran_configuration_valider(long_sans_port, &h, erreur, sizeof(erreur)) == false);
    VERIFIER_TEXTE(erreur, "Printer address is not valid");

    /* meme hote trop long, avec port explicite cette fois : refuse aussi
     * (meme regle que test_hote_parse.c -- adresse trop longue, la chaine
     * entiere est jugee inexploitable, peu importe le port). */
    char long_avec_port[BACKEND_HOTE_LONGUEUR_MAX + 40];
    snprintf(long_avec_port, sizeof(long_avec_port), "%s:1234", long_sans_port);
    VERIFIER(ecran_configuration_valider(long_avec_port, &h, erreur, sizeof(erreur)) == false);

    /* saisie grotesquement longue SANS ':' (au-dela de CLAVIER_VALEUR_MAX,
     * jamais produite par le clavier tactile lui-meme mais cette fonction
     * reste appelable avec n'importe quelle chaine) : refusee, mais PAS une
     * preuve du controle de troncature (revue tache 8, round 2, Q3, corrige
     * a la lecture -- l'affirmation precedente de ce commentaire etait
     * fausse) -- une fois tronque a CLAVIER_VALEUR_MAX+8 octets par le
     * snprintf() de synthese du port par defaut, le tampon ne contient plus
     * AUCUN ':' du tout (les 191 'y' remplissent le tampon avant meme
     * d'atteindre le ":7125" synthetise), donc c'est hote_parse() lui-meme
     * qui refuse (chaine sans ':'), pas le controle de longueur. Le
     * discriminateur reel du controle de troncature est le cas SUIVANT. */
    char enorme[CLAVIER_VALEUR_MAX + 64];
    memset(enorme, 'y', sizeof(enorme) - 1);
    enorme[sizeof(enorme) - 1] = '\0';
    VERIFIER(ecran_configuration_valider(enorme, &h, erreur, sizeof(erreur)) == false);

    /* Cas DANGEREUX qui discrimine reellement le controle de troncature
     * (revue tache 8, round 2, Q3) : une adresse valide suivie d'un ':' et
     * d'un port grotesquement long. Ici le ':' est present DES LE DEPART
     * (branche "deja adresse:port" de ecran_configuration_valider(), pas la
     * branche de synthese) : un snprintf() tronque a CLAVIER_VALEUR_MAX+8
     * octets laisse "192.168.1.50:" PUIS une partie des 'y' dans le tampon
     * -- un ':' y survit. Sans le controle `(size_t)longueur >= sizeof(chaine)`,
     * hote_parse() sur ce tampon tronque verrait une adresse "192.168.1.50"
     * parfaitement valide (port non numerique -> par defaut) et
     * ACCEPTERAIT silencieusement une saisie qui ne l'est pas -- exactement
     * la troncature silencieuse que le brief de la tache 8 interdit
     * explicitement. Observe en RED avec le garde-fou affaibli a
     * `if (longueur < 0)` (voir le rapport de tache 8, section Q3) avant
     * d'etre restaure : `ecran_configuration_valider() == true`,
     * `h.adresse == "192.168.1.50"` -- la troncature passait inapercue. */
    char dangereux[13 + 131];
    snprintf(dangereux, sizeof(dangereux), "192.168.1.50:");
    memset(dangereux + 13, 'y', sizeof(dangereux) - 13 - 1);
    dangereux[sizeof(dangereux) - 1] = '\0';
    erreur[0] = '\0';
    VERIFIER(ecran_configuration_valider(dangereux, &h, erreur, sizeof(erreur)) == false);
    VERIFIER_TEXTE(erreur, "Printer address is too long");

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

    /* aucun hote jamais enregistre a ce stade -- GARANTI par
     * source_reglages_sim_reinit() en tete de suite_ecran_configuration()
     * (revue tache 8, round 1, Q8 : avant ce correctif, ce n'etait vrai que
     * par CHANCE d'ordre d'execution, cette suite etant la seule a toucher
     * ui_reglages_* et la derniere du binaire -- un defaut recurrent de ce
     * jalon, deja vu sous forme de singletons file-static ailleurs). Champ
     * vide, placeholder affiche plutot qu'une chaine vide silencieuse. */
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

    /* --- Chemin d'annulation (revue tache 8, round 1, Q4) : rouvrir le
     * clavier (pre-rempli avec la valeur qui vient d'etre validee) puis
     * appuyer sur CANCEL au lieu de READY doit laisser le champ EXACTEMENT
     * intact -- ni ctx->saisie ni le libelle affiche ne doivent changer,
     * contrairement au chemin READY teste ci-dessus. */
    lv_obj_send_event(ctx->bouton_modifier, LV_EVENT_CLICKED, NULL);
    racine_clavier = dernier_enfant_calque_superieur();
    VERIFIER(racine_clavier != NULL);
    ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    VERIFIER(ta != NULL);
    VERIFIER(kb != NULL);
    /* pre-rempli avec la valeur ISSUE du READY precedent, pas l'originale */
    VERIFIER_TEXTE(lv_textarea_get_text(ta), "newhost.local:1234");

    lv_textarea_set_text(ta, "ceci-ne-doit-jamais-etre-enregistre");
    lv_obj_send_event(kb, LV_EVENT_CANCEL, NULL);
    lv_timer_handler();

    /* le champ n'a PAS bouge : ni la valeur, ni le libelle affiche */
    VERIFIER_TEXTE(ctx->saisie, "newhost.local:1234");
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeur_label), "newhost.local:1234");
    VERIFIER(dernier_enfant_calque_superieur() == NULL);

    /* --- Edit rouvre normalement APRES une annulation : la garde "un seul
     * clavier a la fois" de clavier.c ne reste pas coincee en "ouvert" par
     * un CANCEL mal referme. --- */
    lv_obj_send_event(ctx->bouton_modifier, LV_EVENT_CLICKED, NULL);
    racine_clavier = dernier_enfant_calque_superieur();
    VERIFIER(racine_clavier != NULL);
    ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    VERIFIER(ta != NULL);
    VERIFIER_TEXTE(lv_textarea_get_text(ta), "newhost.local:1234");
    lv_obj_send_event(enfant_de_classe(racine_clavier, &lv_keyboard_class), LV_EVENT_CANCEL, NULL);
    lv_timer_handler();
    VERIFIER(dernier_enfant_calque_superieur() == NULL);

    lv_obj_delete(parent);
    free(brut);
}

static void section_enregistrer(void)
{
    printf("suite : ecran configuration (bouton Save)\n");

    /* Une seule habillage_construire() par binaire de tests (singleton, voir
     * habillage.h) : cette suite est la SEULE a appeler habillage_construire()
     * dans tout le harnais (voir tests/main.c) -- aucune autre suite avant OU
     * apres elle ne le fait. Corrige (revue tache 9, fix round 1, LOW) : la
     * version precedente de ce commentaire disait "cette suite est la
     * derniere du harnais", vrai au moment ou elle a ete ecrite mais rendu
     * FAUX par l'ajout de suite_commandes() (tests/test_commandes.c), qui
     * s'execute apres celle-ci et reutilise cet habillage deja construit --
     * voir son propre commentaire de tete pour le detail de cette
     * reutilisation. */
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

        /* navigation_accueil() a bien reduit la pile a son fond -- MECANISME
         * seulement : ECRAN_JOUET_CONFIG n'est pas "accueil", ce test ne
         * prouve donc pas que Save ramene reellement a l'ecran d'accueil
         * dans la VRAIE topologie de app_main.c. C'est exactement le trou
         * que la revue de la tache 8 (round 1, Q1) a trouve : avec la
         * topologie reelle (ECRAN_CONFIGURATION seul, sans ECRAN_ACCUEIL en
         * dessous), navigation_accueil() est un no-op a profondeur 1 -- Save
         * devenait un cul-de-sac que CE test, construit sur une topologie que
         * app_main.c ne produit jamais, ne pouvait pas voir. Voir
         * section_topologie_reelle() plus bas pour la preuve avec la vraie
         * pile. */
        VERIFIER(navigation_profondeur() == 1);

        VERIFIER(!lv_obj_has_flag(bandeau, LV_OBJ_FLAG_HIDDEN));
        VERIFIER(!lv_color_eq(lv_obj_get_style_bg_color(bandeau, 0), lv_color_hex(0xB3352C)));

        lv_obj_delete(parent);
        free(brut);
    }
}

static void section_topologie_reelle(void)
{
    printf("suite : ecran configuration (topologie reelle : accueil + configuration)\n");

    /* Topologie EXACTE que app_main.c construit desormais sur un appareil
     * jamais configure (revue tache 8, round 1, Q1) : ECRAN_ACCUEIL en fond
     * de pile, ECRAN_CONFIGURATION empile PAR-DESSUS -- jamais
     * ECRAN_CONFIGURATION seul, qui rendrait navigation_accueil() (Save) un
     * no-op a profondeur 1. navigation_init() directement sur
     * lv_screen_active() plutot que via habillage_construire() (deja appele
     * une fois par section_enregistrer -- singleton, voir habillage.h) :
     * ce test n'a besoin que de la pile de navigation elle-meme, pas de la
     * barre d'etat. Sans danger vis-a-vis de l'etat laisse par
     * section_enregistrer : navigation_init() detruit proprement tout ecran
     * deja empile avant de rediriger conteneur_racine (voir navigation.c). */
    navigation_init(lv_screen_active());
    VERIFIER(navigation_empiler(&ECRAN_ACCUEIL) == ESP_OK);
    VERIFIER(navigation_empiler(&ECRAN_CONFIGURATION) == ESP_OK);
    VERIFIER(navigation_profondeur() == 2);
    VERIFIER_TEXTE(navigation_id_courant(), "configuration");

    /* Widgets retrouves par PARCOURS de l'arbre reel -- navigation.c
     * n'expose deliberement aucun accesseur vers le ctx/racine de l'ecran
     * courant (voir ecran.h). ECRAN_CONFIGURATION est le dernier enfant
     * VISIBLE de lv_screen_active() (empile en dernier, navigation cache
     * l'ecran precedent) -- PAS litteralement le dernier enfant tout court :
     * lv_dropdown_create() (LVGL 9.2, lv_dropdown.c:671) cree la LISTE du
     * menu deroulant comme enfant de lv_obj_get_screen(), pas du dropdown
     * lui-meme, et l'ajoute donc APRES racine_config parmi les enfants de
     * l'ecran -- masquee (LV_OBJ_FLAG_HIDDEN) tant que le menu n'est pas
     * ouvert, mais bien presente des la construction. Trouvaille faite en
     * diagnostiquant un SEGV ici (revue tache 8, round 1, Q1) : un compte
     * d'enfants de 6 au lieu des 5 attendus, "dernier enfant" pointant sur
     * cette liste cachee plutot que sur racine_config -- voir
     * dernier_enfant_visible() ci-dessous. Recyclee automatiquement par LVGL
     * quand racine_config est detruit (lv_dropdown_destructor() supprime
     * dropdown->list, voir lv_dropdown.c:683) : pas une fuite, seulement un
     * piege de traversal pour ce test. bouton_enregistrer, lui, reste bien
     * le DERNIER enfant DIRECT de racine_config (le dropdown lui-meme,
     * contrairement a sa liste, est un enfant normal de son parent). */
    lv_obj_t *racine_config = dernier_enfant_visible(lv_screen_active());
    VERIFIER(racine_config != NULL);
    lv_obj_t *bouton_modifier = enfant_de_classe(racine_config, &lv_button_class);
    VERIFIER(bouton_modifier != NULL);
    lv_obj_t *bouton_enregistrer =
        lv_obj_get_child(racine_config, lv_obj_get_child_count(racine_config) - 1);
    VERIFIER(bouton_enregistrer != NULL);
    VERIFIER(lv_obj_check_type(bouton_enregistrer, &lv_button_class));

    /* Adresse valide saisie via le VRAI clavier (pas un ctx force a la
     * main) : preuve de bout en bout, pas seulement du mecanisme Save. */
    lv_obj_send_event(bouton_modifier, LV_EVENT_CLICKED, NULL);
    lv_obj_t *racine_clavier = dernier_enfant_calque_superieur();
    VERIFIER(racine_clavier != NULL);
    lv_obj_t *ta = enfant_de_classe(racine_clavier, &lv_textarea_class);
    lv_obj_t *kb = enfant_de_classe(racine_clavier, &lv_keyboard_class);
    VERIFIER(ta != NULL);
    VERIFIER(kb != NULL);
    if (ta == NULL || kb == NULL) {
        return; /* deja signale ci-dessus ; evite un plantage en cascade */
    }
    lv_textarea_set_text(ta, "192.168.1.99:7125");
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    lv_timer_handler();

    /* Save, avec la topologie REELLE cette fois : navigation_accueil() doit
     * reellement revenir a "accueil", pas rester bloque a profondeur 1 sans
     * bouger -- le defaut Q1 exact que la revue a reproduit avec la vraie
     * topologie de app_main.c. */
    lv_obj_send_event(bouton_enregistrer, LV_EVENT_CLICKED, NULL);

    VERIFIER(navigation_profondeur() == 1);
    VERIFIER_TEXTE(navigation_id_courant(), "accueil");

    backend_hote_t apres;
    VERIFIER(ui_reglages_hote(&apres) == true);
    VERIFIER_TEXTE(apres.adresse, "192.168.1.99");
    VERIFIER(apres.port == 7125);
}

void suite_ecran_configuration(void)
{
    /* Revue tache 8, round 1, Q8 : remet la facade reglages PC/simulateur a
     * son etat initial AVANT toute section -- sans quoi cette suite ne
     * serait "non configuree" au depart que par chance d'ordre d'execution
     * (voir le commentaire de section_construction_non_configure()). */
    source_reglages_sim_reinit();

    section_validation();
    section_construction_non_configure();
    section_construction_prerempli();
    section_clavier();
    section_enregistrer();
    section_topologie_reelle();
}
