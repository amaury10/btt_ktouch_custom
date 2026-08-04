/* Feature "Console gcode", tache B (integration ESP) : smoke test de
 * ECRAN_CONSOLE -- pas de suite dediee prevue au depart (meme choix que
 * ecran_power.c, voir test_ecran_stub.c), ajoutee ici quand meme car ce
 * fichier N'EST PAS compile sous ESP-IDF (idf.py hors de portee de cet
 * agent, controleur de gate) : c'est la SEULE verification d'execution
 * reelle (au-dela de la simple compilation) du cycle construire()/
 * mettre_a_jour()/callbacks avant le flash materiel.
 *
 * Trois groupes de preuves :
 *   1. Scrollback : console_log_ajouter() puis mettre_a_jour() -> le label
 *      du scrollback reflete le contenu du store, dans l'ordre, joint par
 *      '\n' -- et re-mettre_a_jour() sans neuve generation ne re-formate
 *      rien (pas verifie pixel a pixel, juste que le texte reste correct).
 *   2. Saisie -> envoi : tap sur le champ ouvre le clavier tactile (meme
 *      patron que test_ecran_reglages_wifi.c/test_clavier.c), valider pose
 *      le texte dans le champ SANS envoyer, tap "Send" echo localement PUIS
 *      trace ui_commander(BACKEND_ACTION_GCODE, {"script":"..."}) --
 *      caracteres a echapper (guillemet) compris -- et vide le champ apres.
 *   3. "Clear" : console_log_effacer() -> le scrollback redevient vide au
 *      prochain mettre_a_jour().
 *
 * DOIT rester APRES suite_ecran_configuration() (habillage construit) ET
 * suite_commandes() (boucle simulee demarree) -- meme garde d'ordonnancement
 * que test_ecran_actions.c, verifiee a l'entree de la suite. */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "backend.h"
#include "console_log.h"
#include "ecran_console.h"
#include "habillage.h"
#include "petit_test.h"
#include "source_etat.h"
#include "source_etat_sim.h"

/* --- Utilitaires (calques sur test_ecran_reglages_wifi.c/test_clavier.c) - */

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

static void construire_ecran(lv_obj_t **parent_sortie, ecran_console_ctx_t **ctx_sortie, void **brut_sortie)
{
    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_CONSOLE.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_console_ctx_t *ctx = (ecran_console_ctx_t *)brut;
    ECRAN_CONSOLE.construire(parent, ctx);
    *parent_sortie = parent;
    *ctx_sortie = ctx;
    *brut_sortie = brut;
}

void suite_ecran_console(void)
{
    printf("suite : ecran console (scrollback + saisie clavier + envoi)\n");

    if (!habillage_est_construit() || !source_etat_sim_est_demarre()) {
        printf("ERREUR: suite_ecran_console() exige que suite_ecran_configuration() ET "
               "suite_commandes() aient deja tourne -- verifier l'ordre dans tests/main.c.\n");
        exit(1);
    }

    console_log_effacer(); /* store process-wide singleton : etat propre avant cette suite */

    lv_obj_t *parent;
    ecran_console_ctx_t *ctx;
    void *brut;
    construire_ecran(&parent, &ctx, &brut);

    /* ---------------------------------------------------------------------
     * Groupe 1 : scrollback vide au depart, puis reflete le store apres
     * console_log_ajouter() + mettre_a_jour(). --------------------------- */
    ECRAN_CONSOLE.mettre_a_jour(NULL, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->zone_label), "");

    console_log_ajouter("Klipper state: Ready");
    console_log_ajouter("// echo: test");
    ECRAN_CONSOLE.mettre_a_jour(NULL, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->zone_label), "Klipper state: Ready\n// echo: test");

    /* Un second mettre_a_jour() sans neuve generation ne doit rien casser
     * (relit le meme texte, generation inchangee). */
    ECRAN_CONSOLE.mettre_a_jour(NULL, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->zone_label), "Klipper state: Ready\n// echo: test");

    /* ---------------------------------------------------------------------
     * Groupe 2 : champ vide au depart (placeholder), "Send" desactive. ---- */
    VERIFIER_TEXTE(lv_label_get_text(ctx->champ_label), "Tap to type a command");
    VERIFIER(lv_obj_has_state(ctx->bouton_envoyer, LV_STATE_DISABLED));

    /* Tap sur le champ -> ouvre le clavier tactile (meme patron que
     * test_ecran_reglages_wifi.c). */
    VERIFIER(dernier_enfant_calque_superieur() == NULL); /* aucun clavier avant */
    lv_obj_send_event(ctx->champ, LV_EVENT_CLICKED, NULL);

    lv_obj_t *racine = dernier_enfant_calque_superieur();
    VERIFIER(racine != NULL); /* le clavier a bien ete ouvert */
    lv_obj_t *titre = enfant_de_classe(racine, &lv_label_class);
    VERIFIER(titre != NULL);
    VERIFIER_TEXTE(lv_label_get_text(titre), "Command");

    lv_obj_t *kb = enfant_de_classe(racine, &lv_keyboard_class);
    lv_obj_t *ta = enfant_de_classe(racine, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);

    /* Commande avec un guillemet -- point de securite central de cette
     * feature (echappement JSON de la saisie libre, voir json_util.h). */
    lv_textarea_set_text(ta, "M117 say \"hi\"");
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    lv_timer_handler(); /* execute la fermeture asynchrone du clavier */

    /* Valider pose le texte dans le champ SANS envoyer -- aucune commande
     * tracee, "Send" desormais actif. */
    VERIFIER(source_etat_sim_est_demarre());
    size_t avant_envoi = source_etat_sim_file_taille();
    VERIFIER_TEXTE(lv_label_get_text(ctx->champ_label), "M117 say \"hi\"");
    VERIFIER(!lv_obj_has_state(ctx->bouton_envoyer, LV_STATE_DISABLED));
    VERIFIER(source_etat_sim_file_taille() == avant_envoi);

    /* Tap "Send" : echo local (visible au prochain mettre_a_jour()) PUIS
     * ui_commander(BACKEND_ACTION_GCODE, {"script":"<echappe>"}). */
    lv_obj_send_event(ctx->bouton_envoyer, LV_EVENT_CLICKED, NULL);

    VERIFIER(source_etat_sim_file_taille() == avant_envoi + 1);
    char action[32];
    char arguments[256];
    VERIFIER(source_etat_sim_derniere_commande(action, sizeof(action), arguments, sizeof(arguments)) == true);
    VERIFIER_TEXTE(action, BACKEND_ACTION_GCODE);
    /* Le guillemet doit avoir ete echappe (\") -- jamais un JSON casse par la
     * saisie libre de l'utilisateur. */
    VERIFIER(strstr(arguments, "\"script\":\"M117 say \\\"hi\\\"\"") != NULL);
    source_etat_sim_cycle();

    /* Le champ est vide apres l'envoi -- placeholder + "Send" desactive de
     * nouveau. */
    VERIFIER_TEXTE(lv_label_get_text(ctx->champ_label), "Tap to type a command");
    VERIFIER(lv_obj_has_state(ctx->bouton_envoyer, LV_STATE_DISABLED));

    /* L'echo local est bien dans le store (pousse par bouton_envoyer_cb()
     * AVANT ui_commander()) -- visible au prochain mettre_a_jour(). */
    ECRAN_CONSOLE.mettre_a_jour(NULL, false, ctx);
    const char *texte_scrollback = lv_label_get_text(ctx->zone_label);
    VERIFIER(strstr(texte_scrollback, ">> M117 say \"hi\"") != NULL);

    /* ---------------------------------------------------------------------
     * Groupe 3 : "Clear" vide le SCROLLBACK (pas le champ, deja vide ici de
     * toute facon) -- console_log_effacer(), visible au prochain
     * mettre_a_jour(). ----------------------------------------------------- */
    lv_obj_send_event(ctx->bouton_effacer, LV_EVENT_CLICKED, NULL);
    ECRAN_CONSOLE.mettre_a_jour(NULL, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->zone_label), "");

    lv_obj_delete(parent);
    free(brut);
}
