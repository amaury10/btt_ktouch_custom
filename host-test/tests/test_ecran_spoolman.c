/* Écran Spoolman (spec 2026-08-15-spoolman-design.md) : remplace
 * test_ecran_stub.c, supprimé avec le dernier stub. Ce fichier N'EST PAS
 * compilé sous ESP-IDF : c'est la SEULE vérification d'exécution réelle
 * (au-delà de la compilation) du cycle construire()/mettre_a_jour()/
 * callbacks avant le flash matériel.
 *
 * Quatre groupes de preuves :
 *   1. Liste vide et « jamais reçue » ne disent PAS la même chose, et
 *      Spoolman hors ligne le dit franchement.
 *   2. Une liste alimente les rangées : fabricant, filament, matière, poids,
 *      et la bobine active porte la coche.
 *   3. Un tap trace ui_commander(BACKEND_ACTION_SPOOLMAN, {"spool_id":N})
 *      APRÈS confirmation seulement -- et retaper la bobine DÉJÀ active
 *      n'envoie rien.
 *   4. Pagination : 12 bobines -> 3 pages, la page suivante montre la suite.
 *
 * DOIT rester APRÈS suite_ecran_configuration() (habillage construit) ET
 * suite_commandes() (boucle simulée démarrée) -- même garde d'ordonnancement
 * que test_ecran_console.c, vérifiée à l'entrée de la suite. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "backend.h"
#include "ecran_spoolman.h"
#include "confirmation.h"
#include "habillage.h"
#include "moonraker_ws_mock.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"
#include "spoolman_store.h"

static void construire_ecran(lv_obj_t **parent_sortie, ecran_spoolman_ctx_t **ctx_sortie,
                             void **brut_sortie)
{
    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_SPOOLMAN.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_spoolman_ctx_t *ctx = (ecran_spoolman_ctx_t *)brut;
    ECRAN_SPOOLMAN.construire(parent, ctx);
    *parent_sortie = parent;
    *ctx_sortie = ctx;
    *brut_sortie = brut;
}

/* Dernier objet du calque supérieur = le dialogue de confirmation modal
 * (même technique que test_ecran_console.c/test_ecran_fichiers.c). */
static lv_obj_t *dernier_enfant_calque_superieur(void)
{
    lv_obj_t *calque = lv_layer_top();
    uint32_t n = lv_obj_get_child_count(calque);
    return (n == 0) ? NULL : lv_obj_get_child(calque, n - 1);
}

static lv_obj_t *dernier_msgbox(void)
{
    lv_obj_t *fond = dernier_enfant_calque_superieur();
    return (fond == NULL) ? NULL : lv_obj_get_child(fond, 0);
}

/* Confirme le dialogue ouvert : pied de page -> enfant 1 = bouton d'action
 * (enfant 0 = déclin). MÊME chemin que test_ecran_fichiers.c -- chercher un
 * bouton par son libellé était une fausse bonne idée : "Clear" matchait
 * d'abord le TITRE « Clear active spool? », le dialogue restait ouvert, et
 * le singleton de confirmation empoisonnait la suite suivante. */
static void confirmer_dialogue(const char *titre_attendu)
{
    lv_obj_t *mbox = dernier_msgbox();
    VERIFIER(mbox != NULL);
    VERIFIER_TEXTE(lv_label_get_text(lv_msgbox_get_title(mbox)), titre_attendu);
    lv_obj_t *pied = lv_msgbox_get_footer(mbox);
    VERIFIER(pied != NULL);
    lv_obj_t *bouton_action = lv_obj_get_child(pied, 1);
    VERIFIER(bouton_action != NULL);
    lv_obj_send_event(bouton_action, LV_EVENT_CLICKED, NULL);
    lv_timer_handler();
}

static void remplir_liste(uint8_t nb)
{
    spoolman_liste_t liste;
    memset(&liste, 0, sizeof(liste));
    liste.nb = nb;
    liste.connue = true;
    for (uint8_t i = 0; i < nb; i++) {
        liste.bobines[i].id = (int32_t)(i + 1);
        snprintf(liste.bobines[i].filament, SPOOLMAN_TEXTE_MAX, "Filament %u", (unsigned)(i + 1));
        snprintf(liste.bobines[i].fabricant, SPOOLMAN_TEXTE_MAX, "Marque%u", (unsigned)(i + 1));
        snprintf(liste.bobines[i].matiere, SPOOLMAN_MATIERE_MAX, "PLA");
        liste.bobines[i].restant_g = 500.0f;
        liste.bobines[i].restant_connu = true;
        liste.bobines[i].total_g = 1000.0f;
    }
    spoolman_definir_liste(&liste);
}

void suite_ecran_spoolman(void)
{
    printf("suite : ecran spoolman (liste de bobines + selection de l'active)\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_spoolman() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    /* Contrat de l'ecran_desc_t, ce que verifiait test_ecran_stub.c. */
    VERIFIER_TEXTE(ECRAN_SPOOLMAN.id, "spoolman");
    VERIFIER_TEXTE(ECRAN_SPOOLMAN.titre, "Spoolman");
    VERIFIER(ECRAN_SPOOLMAN.construire != NULL);
    VERIFIER(ECRAN_SPOOLMAN.mettre_a_jour != NULL);

    lv_obj_t *parent;
    ecran_spoolman_ctx_t *ctx;
    void *brut;
    construire_ecran(&parent, &ctx, &brut);

    /* ---------------------------------------------------------------------
     * Groupe 1 : rien de connu -> "Loading", puis liste vide REELLE -> "No
     * spools", puis Spoolman hors ligne -> l'en-tete le DIT. ------------- */
    ECRAN_SPOOLMAN.mettre_a_jour(NULL, false, ctx);
    VERIFIER(strstr(lv_label_get_text(ctx->vide), "Loading") != NULL);
    VERIFIER_TEXTE(lv_label_get_text(ctx->page_label), "1/1");

    spoolman_liste_t vide;
    memset(&vide, 0, sizeof(vide));
    spoolman_definir_liste(&vide); /* connue = vraie, nb = 0 */
    ECRAN_SPOOLMAN.mettre_a_jour(NULL, false, ctx);
    VERIFIER(strstr(lv_label_get_text(ctx->vide), "No spools") != NULL);
    VERIFIER(strstr(lv_label_get_text(ctx->entete), "No active spool") != NULL);

    spoolman_definir_connecte(false);
    ECRAN_SPOOLMAN.mettre_a_jour(NULL, false, ctx);
    VERIFIER(strstr(lv_label_get_text(ctx->entete), "offline") != NULL);
    spoolman_definir_connecte(true);

    /* ---------------------------------------------------------------------
     * Groupe 2 : la liste alimente les rangees ; l'active porte la coche. */
    remplir_liste(3);
    spoolman_definir_actif(2);
    ECRAN_SPOOLMAN.mettre_a_jour(NULL, false, ctx);

    VERIFIER(lv_obj_has_flag(ctx->vide, LV_OBJ_FLAG_HIDDEN));
    const char *rangee0 = lv_label_get_text(ctx->labels[0]);
    VERIFIER(strstr(rangee0, "Marque1") != NULL);
    VERIFIER(strstr(rangee0, "Filament 1") != NULL);
    VERIFIER(strstr(rangee0, "(PLA)") != NULL);
    VERIFIER(strstr(rangee0, "500 / 1000 g") != NULL);
    VERIFIER(strstr(lv_label_get_text(ctx->labels[1]), LV_SYMBOL_OK) != NULL); /* la 2e est active */
    VERIFIER(strstr(rangee0, LV_SYMBOL_OK) == NULL);
    VERIFIER(strstr(lv_label_get_text(ctx->entete), "Filament 2") != NULL);
    /* Les rangees au-dela de la liste sont MASQUEES, pas remplies de debris. */
    VERIFIER(!lv_obj_has_flag(ctx->boutons[2], LV_OBJ_FLAG_HIDDEN));
    VERIFIER(lv_obj_has_flag(ctx->boutons[3], LV_OBJ_FLAG_HIDDEN));
    VERIFIER(lv_obj_has_flag(ctx->boutons[4], LV_OBJ_FLAG_HIDDEN));

    /* Poids inconnu : "?" et JAMAIS "0 g". */
    spoolman_liste_t sans_poids;
    memset(&sans_poids, 0, sizeof(sans_poids));
    sans_poids.nb = 1;
    sans_poids.connue = true;
    sans_poids.bobines[0].id = 7;
    snprintf(sans_poids.bobines[0].filament, SPOOLMAN_TEXTE_MAX, "Inconnu");
    spoolman_definir_liste(&sans_poids);
    ECRAN_SPOOLMAN.mettre_a_jour(NULL, false, ctx);
    VERIFIER(strstr(lv_label_get_text(ctx->labels[0]), "? g") != NULL);
    VERIFIER(strstr(lv_label_get_text(ctx->labels[0]), "0 g") == NULL);

    /* ---------------------------------------------------------------------
     * Groupe 3 : tap -> confirmation -> commande. ------------------------ */
    remplir_liste(3);
    spoolman_definir_actif(2);
    ECRAN_SPOOLMAN.mettre_a_jour(NULL, false, ctx);

    char action[32];
    char arguments[128];

    size_t avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->boutons[1], LV_EVENT_CLICKED, NULL); /* la bobine DEJA active */
    VERIFIER(source_etat_sim_file_taille() == avant);           /* ni confirmation, ni envoi */

    lv_obj_send_event(ctx->boutons[0], LV_EVENT_CLICKED, NULL); /* bobine 1 */
    VERIFIER(confirmation_est_ouverte());
    VERIFIER(source_etat_sim_file_taille() == avant); /* rien AVANT la confirmation */
    confirmer_dialogue("Set as loaded spool?");
    VERIFIER(!confirmation_est_ouverte());
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments,
                                               sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_SPOOLMAN);
    VERIFIER_TEXTE(arguments, "{\"spool_id\":1}");
    source_etat_sim_cycle();

    /* Clear active -> {} : la cle est OMISE, jamais nulle. Moonraker rend
     * HTTP 400 sur {"spool_id":null} (verifie sur machine reelle le
     * 2026-08-15) et la bobine restait active -- ce VERIFIER_TEXTE est le
     * garde-fou contre la reintroduction du null. */
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->bouton_effacer, LV_EVENT_CLICKED, NULL);
    confirmer_dialogue("Clear active spool?");
    VERIFIER(!confirmation_est_ouverte());
    VERIFIER(source_etat_sim_file_taille() == avant + 1);
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments,
                                               sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_SPOOLMAN);
    VERIFIER_TEXTE(arguments, "{}");
    source_etat_sim_cycle();

    /* Refresh : declenche VRAIMENT une redemande de la liste (le mock
     * compte, voir moonraker_ws_mock.c) -- sans envoyer de commande. */
    moonraker_ws_mock_reinitialiser();
    avant = source_etat_sim_file_taille();
    lv_obj_send_event(ctx->bouton_rafraichir, LV_EVENT_CLICKED, NULL);
    VERIFIER(moonraker_ws_mock_demandes_bobines() == 1);
    VERIFIER(source_etat_sim_file_taille() == avant);

    /* ---------------------------------------------------------------------
     * Groupe 4 : pagination. --------------------------------------------- */
    remplir_liste(12);
    ECRAN_SPOOLMAN.mettre_a_jour(NULL, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->page_label), "1/3");
    VERIFIER(strstr(lv_label_get_text(ctx->labels[0]), "Filament 1") != NULL);

    lv_obj_send_event(ctx->bouton_suivant, LV_EVENT_CLICKED, NULL);
    ECRAN_SPOOLMAN.mettre_a_jour(NULL, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->page_label), "2/3");
    VERIFIER(strstr(lv_label_get_text(ctx->labels[0]), "Filament 6") != NULL);

    lv_obj_send_event(ctx->bouton_precedent, LV_EVENT_CLICKED, NULL);
    ECRAN_SPOOLMAN.mettre_a_jour(NULL, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->page_label), "1/3");

    /* La liste retrecit sous la page courante : jamais une page fantome. */
    lv_obj_send_event(ctx->bouton_suivant, LV_EVENT_CLICKED, NULL);
    lv_obj_send_event(ctx->bouton_suivant, LV_EVENT_CLICKED, NULL);
    ECRAN_SPOOLMAN.mettre_a_jour(NULL, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->page_label), "3/3");
    remplir_liste(2);
    ECRAN_SPOOLMAN.mettre_a_jour(NULL, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->page_label), "1/1");

    source_etat_sim_cycle();
    /* Filet de securite : un dialogue laisse ouvert ici ferait echouer la
       SUITE SUIVANTE (singleton process-wide), pas celle-ci -- exactement le
       faux coupable qu'on veut s'epargner. */
    VERIFIER(!confirmation_est_ouverte());
    lv_obj_del(parent);
    free(brut);
}
