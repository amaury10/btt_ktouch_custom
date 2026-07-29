/* Tache 11 : preuve d'integration du backend et de l'ecran jouets
 * (exemples/backend_jouet/) -- le test decisif du modele en fork. Trois
 * volets :
 *
 *   1. boucle_cycle() sur backend_jouet_desc() : le compteur avance
 *      REELLEMENT d'un cycle a l'autre. Meme piege que CRITICAL 1 (revue de
 *      fin de jalon 2a, voir host-test/tests/test_boucle_cycle.c) : le
 *      socle remet le tampon a zero avant CHAQUE rafraichir(), un backend
 *      qui relirait sa propre progression depuis ce tampon la verrait
 *      toujours a zero. Reprend exactement le schema de ce fichier-la.
 *
 *   2. ECRAN_JOUET.construire()/.mettre_a_jour() : le libelle affiche le bon
 *      compte, se grise sur donnees_perimees -- jamais un pixel regarde,
 *      meme technique que host-test/tests/test_ecran_accueil.c.
 *
 *   3. "reset" : backend_jouet_desc()->commande() remet le compteur a zero,
 *      teste ICI directement contre le backend (logique complete couverte).
 *      Le bouton Reset de l'ecran appelle bien ui_commander("reset", NULL) --
 *      prouve via source_etat_sim_file_taille(), meme technique que
 *      host-test/tests/test_commandes.c section 3 -- mais la commande n'est
 *      PAS ensuite executee contre backend_jouet_desc() ICI : source_etat_sim.c
 *      est un singleton process-wide (voir son commentaire de tete), deja lie
 *      a backend_factice_desc() par test_commandes.c (enregistre AVANT ce
 *      fichier dans tests/main.c, et ce fichier doit le rester). Rejouer un
 *      second source_etat_sim_demarrer() ici echouerait (deja demarre) et
 *      lier un troisieme backend au meme singleton n'a pas de sens.
 *      ui_commander() ne connait que la profondeur de la file, jamais le
 *      type du backend qui la draine : verifier qu'un clic l'incremente de 1
 *      suffit a prouver le cablage bouton -> ui_commander("reset", ...),
 *      sans avoir besoin d'executer la commande jusqu'au bout ici. Detail
 *      complet de cette limite du harnais : revue de la tache 11, jalon 2b. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "backend_jouet.h"
#include "boucle_cycle.h"
#include "ecran_jouet.h"
#include "etat_store.h"
#include "liaison.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

/* ------------------------------------------------------------------------
 * Section 1+3a : boucle_cycle() (compteur qui avance) et "reset" contre le
 * backend directement.
 * ------------------------------------------------------------------------ */

static void section_boucle_cycle(void)
{
    printf("suite : jouet (boucle_cycle)\n");

    const backend_desc_t *d = backend_jouet_desc();
    VERIFIER(d != NULL);
    if (d == NULL) {
        return;
    }

    etat_store_t store;
    VERIFIER(etat_store_init(&store, d->taille_etat));

    liaison_t liaison;
    liaison_init(&liaison, 3, 10);

    backend_hote_t hote = { .adresse = "jouet", .port = 0 };
    void *initial = etat_store_tampon_arriere(&store);
    VERIFIER(d->demarrer(initial, &hote) == ESP_OK);
    etat_store_valider(&store);

    uint32_t generation_avant = etat_store_generation(&store);
    VERIFIER(generation_avant == 0);

    /* Cinq cycles : si le backend relisait son compteur depuis le tampon que
     * boucle_cycle() vient de remettre a zero (au lieu de porter g_compteur
     * en statique de fichier, voir backend_jouet.c), chaque cycle produirait
     * exactement 1 sans jamais varier -- comparer le tout premier cycle au
     * dernier est ce qui piege ce defaut-la (meme raisonnement que
     * test_boucle_cycle.c). */
    uint32_t compteur_premier_cycle = 0;
    int succes_compte = 0;
    for (int i = 0; i < 5; i++) {
        bool succes = boucle_cycle(&store, &liaison, d);
        VERIFIER(succes);
        if (succes) {
            succes_compte++;
            etat_store_valider(&store);
        }
        if (i == 0) {
            compteur_premier_cycle = ((const etat_jouet_t *)etat_store_lire(&store))->compteur;
        }
    }
    VERIFIER(succes_compte == 5);
    VERIFIER(compteur_premier_cycle == 1);

    uint32_t compteur_final = ((const etat_jouet_t *)etat_store_lire(&store))->compteur;
    VERIFIER(compteur_final == 5);
    VERIFIER(compteur_final > compteur_premier_cycle);

    const etat_jouet_t *e = (const etat_jouet_t *)etat_store_lire(&store);
    VERIFIER_TEXTE(e->libelle, "toy");

    uint32_t generation_apres = etat_store_generation(&store);
    VERIFIER(generation_apres > generation_avant);
    VERIFIER(generation_apres == 5);

    VERIFIER(liaison_etat(&liaison) == LIAISON_EN_LIGNE);
    VERIFIER(liaison_echecs_consecutifs(&liaison) == 0);

    /* "reset" directement contre le backend (contrat commande(), voir
     * backend.h) : remet g_compteur a zero, observable au cycle suivant --
     * `etat` lui-meme reste zero avant chaque rafraichir() (meme contrat
     * qu'au-dessus), donc le seul endroit ou verifier l'effet de reset() est
     * le PROCHAIN cycle, pas `etat` lui-meme juste apres l'appel. */
    void *etat_pour_commande = etat_store_tampon_arriere(&store);
    VERIFIER(d->commande(etat_pour_commande, "reset", NULL) == ESP_OK);
    VERIFIER(boucle_cycle(&store, &liaison, d));
    etat_store_valider(&store);
    uint32_t compteur_apres_reset = ((const etat_jouet_t *)etat_store_lire(&store))->compteur;
    VERIFIER(compteur_apres_reset == 1); /* redemarre a 1, exactement comme au tout premier cycle */

    /* Action inconnue : ESP_ERR_NOT_SUPPORTED, jamais un succes silencieux
     * (meme politique que backend_factice_commande(), voir
     * test_backend_factice.c). */
    VERIFIER(d->commande(etat_pour_commande, "action_inexistante", NULL) == ESP_ERR_NOT_SUPPORTED);

    etat_store_liberer(&store);
}

/* ------------------------------------------------------------------------
 * Section 2 : ECRAN_JOUET -- affichage du compte, grisage.
 * ------------------------------------------------------------------------ */

/* CRITICAL (revue finale jalon 2b) : meme decouverte, meme remede que
 * pomper_transitions_style() dans test_commandes.c (voir son commentaire
 * pour le detail complet) -- le theme LVGL par defaut anime bg_color/
 * text_color sur un changement d'etat (LV_THEME_DEFAULT_TRANSITION_TIME=80,
 * simulateur/lv_conf.h), et ce harnais ne fait jamais avancer lv_tick_get()
 * ailleurs, donc l'animation reste figee a t=0 sans ce pompage explicite. */
static void pomper_transitions_style(void)
{
    for (int i = 0; i < 5; i++) {
        lv_tick_inc(100);
        lv_timer_handler();
    }
}

static void section_ecran(void)
{
    printf("suite : jouet (ecran)\n");

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_JOUET.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_jouet_ctx_t *ctx = (ecran_jouet_ctx_t *)brut;
    ECRAN_JOUET.construire(parent, ctx);
    VERIFIER(ctx->valeur != NULL);
    VERIFIER(ctx->bouton != NULL);

    etat_jouet_t etat;
    memset(&etat, 0, sizeof(etat));
    etat.compteur = 42;
    snprintf(etat.libelle, sizeof(etat.libelle), "toy");

    ECRAN_JOUET.mettre_a_jour(&etat, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeur), "Count: 42");
    VERIFIER(!lv_obj_has_state(ctx->bouton, LV_STATE_DISABLED));
    VERIFIER(ctx->donnees_perimees == false);

    /* Reference active, capturee avant tout grisage -- meme technique que
     * section_ecran_accueil_boutons() dans test_commandes.c (revue finale
     * jalon 2b, CRITICAL) : lv_obj_has_state() seul ne prouve rien, c'est le
     * drapeau que le code casse posait deja sans qu'aucun pixel ne bouge
     * (0/32000 pixels de difference constates par la revue). La couleur
     * RESOLUE doit reellement changer. */
    lv_color_t fond_actif = lv_obj_get_style_bg_color(ctx->bouton, LV_PART_MAIN);
    lv_obj_t *label = lv_obj_get_child(ctx->bouton, 0);
    lv_color_t texte_actif = lv_obj_get_style_text_color(label, LV_PART_MAIN);

    /* Grisage systematique (spec 5.3), round-trip complet -- meme discipline
     * que test_ecran_accueil.c : donnees_perimees=false doit toujours rendre
     * l'ecran normal, y compris apres etre passe par l'etat grise. */
    etat.compteur = 43;
    ECRAN_JOUET.mettre_a_jour(&etat, true, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->valeur), "Count: 43");
    VERIFIER(lv_obj_has_state(ctx->bouton, LV_STATE_DISABLED));
    VERIFIER(ctx->donnees_perimees == true);
    /* pomper_transitions_style() termine la transition de theme avant de
     * lire (voir son commentaire) : lv_obj_add_state() pose l'etat de facon
     * synchrone, mais la couleur RESOLUE reste celle de depart tant que
     * l'animation n'a pas tourne. */
    pomper_transitions_style();
    VERIFIER(!lv_color_eq(lv_obj_get_style_bg_color(ctx->bouton, LV_PART_MAIN), fond_actif));
    VERIFIER(!lv_color_eq(lv_obj_get_style_text_color(label, LV_PART_MAIN), texte_actif));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(label, LV_PART_MAIN), lv_color_hex(0x6B7280)));

    ECRAN_JOUET.mettre_a_jour(&etat, false, ctx);
    VERIFIER(!lv_obj_has_state(ctx->bouton, LV_STATE_DISABLED));
    VERIFIER(ctx->donnees_perimees == false);
    /* Retour EXACT a la reference active -- un grisage a sens unique serait
     * tout aussi trompeur (lecon de la revue tache 4). */
    pomper_transitions_style();
    VERIFIER(lv_color_eq(lv_obj_get_style_bg_color(ctx->bouton, LV_PART_MAIN), fond_actif));
    VERIFIER(lv_color_eq(lv_obj_get_style_text_color(label, LV_PART_MAIN), texte_actif));

    lv_obj_delete(parent);
    free(brut);
}

/* ------------------------------------------------------------------------
 * Section 3b : le bouton Reset appelle bien ui_commander("reset", NULL) --
 * voir le commentaire de tete pour la limite exacte de ce que ce test peut
 * prouver dans ce harnais.
 * ------------------------------------------------------------------------ */

static void section_bouton_reset_ui_commander(void)
{
    printf("suite : jouet (bouton reset -> ui_commander)\n");

    /* La boucle simulee est deja demarree ici (singleton process-wide),
     * liee a backend_factice_desc() par test_commandes.c -- voir le
     * commentaire de tete de ce fichier pour pourquoi ce test ne peut pas
     * (et n'a pas besoin de) faire executer "reset" par backend_jouet_desc()
     * a travers ce seam-la. */
    VERIFIER(source_etat_sim_file_taille() == 0);

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_JOUET.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_jouet_ctx_t *ctx = (ecran_jouet_ctx_t *)brut;
    ECRAN_JOUET.construire(parent, ctx);

    etat_jouet_t etat;
    memset(&etat, 0, sizeof(etat));
    ECRAN_JOUET.mettre_a_jour(&etat, false, ctx);

    /* Grise : un clic direct (lv_obj_send_event() ne passe jamais par la
     * verification tactile de lv_indev.c sur LV_STATE_DISABLED) ne doit rien
     * envoyer -- meme garde defensive que ecran_accueil.c, meme preuve que
     * test_commandes.c. */
    ECRAN_JOUET.mettre_a_jour(&etat, true, ctx);
    lv_obj_send_event(ctx->bouton, LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == 0);

    /* Frais : le clic empile "reset". */
    ECRAN_JOUET.mettre_a_jour(&etat, false, ctx);
    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->bouton, LV_EVENT_CLICKED, NULL);
    VERIFIER(source_etat_sim_file_taille() == avant + 1);

    /* Draine avant de rendre la main : la commande echoue contre
     * backend_factice (ESP_ERR_NOT_SUPPORTED, "reset" lui est inconnu). */
    source_etat_sim_cycle();

    /* Fix round 1 (revue tache 6, M2 MEDIUM) : cet echec ARME le sceau a un
     * seul emplacement de ui_commande_echec() (source_etat_sim.c) -- ce
     * fichier N'EST PLUS forcement le dernier de la suite (voir tests/main.c :
     * suite_ecran_macros() tourne apres lui et appelle habillage_pomper(),
     * qui aurait sinon herite ce sceau "reset" a tort). L'ancien commentaire
     * ("ce fichier est le dernier de la suite, sans consequence sur quoi que
     * ce soit apres") etait devenu FAUX des l'ajout de cette suite-la sans
     * que rien ici ne le signale -- exactement la classe de couplage fragile
     * qu'un nettoyage a la source (plutot qu'un pompage a vide chez le
     * consommateur suivant) elimine. Draine ICI, au point de PRODUCTION du
     * sceau -- jamais a la charge d'une suite future de savoir qu'il existe. */
    source_etat_sim_reset_echec();

    lv_obj_delete(parent);
    free(brut);
}

void suite_jouet(void)
{
    /* Garde d'ordonnancement (revue finale jalon 2b) : section_bouton_reset_ui_commander()
     * ci-dessous reutilise la boucle simulee deja demarree par
     * suite_commandes() (singleton process-wide, voir le commentaire de tete
     * de ce fichier) -- sans elle, une inversion accidentelle de l'ordre
     * d'enregistrement dans tests/main.c plantait en SEGV brut, sans le
     * moindre compte de verifications affiche. source_etat_sim_est_demarre()
     * transforme ce SEGV muet en une erreur nommee des l'entree de cette
     * suite. Meme technique que habillage_est_construit() dans
     * suite_commandes() (test_commandes.c). */
    if (!source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_jouet() exige que suite_commandes() ait deja demarre la boucle "
               "simulee (singleton process-wide) -- verifier l'ordre d'enregistrement dans "
               "tests/main.c.\n");
        exit(1);
    }

    section_boucle_cycle();
    section_ecran();
    section_bouton_reset_ui_commander();
}
