/* Sous-projet 7 (réglages WiFi à l'écran), tâche 4 : l'écran ECRAN_REGLAGES_WIFI
 * -- voir ecran_reglages_wifi.h pour le contrat. La façade wifi_* est mockée
 * (host-test/tests/wifi_mock.c) : trois réseaux factices, dont deux chiffrés et
 * un ouvert. Quatre groupes de preuves :
 *
 *   1. Le balayage (mock synchrone) peuple la liste : trois SSID, cadenas
 *      présent sur les chiffrés et absent sur l'ouvert, en-tête reflétant
 *      wifi_etat().
 *   2. Tap sur un réseau chiffré -> clavier ouvert (titre = SSID) -> valider un
 *      mot de passe -> wifi_reconfigurer(ssid, motdepasse) tracé par le mock.
 *   3. Garde EN_COURS : tant qu'une reconfiguration est en cours, un tap ne
 *      relance rien (aucun clavier, aucun nouvel appel).
 *   4. Tap sur un réseau ouvert -> pas de clavier, wifi_reconfigurer(ssid, "")
 *      direct. Plus la traduction des états de reconfiguration dans l'en-tête.
 *
 * Événements tactiles simulés via lv_obj_send_event() (LV_EVENT_CLICKED sur une
 * ligne, LV_EVENT_READY sur le clavier), exactement comme test_ecran_macros.c
 * et test_clavier.c -- aucun pixel n'est jamais examiné. */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "ecran_reglages_wifi.h"
#include "petit_test.h"
#include "wifi_mock.h"

/* --- Utilitaires (calqués sur test_clavier.c) --------------------------- */

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

static void construire_ecran(lv_obj_t **parent_sortie, ecran_reglages_wifi_ctx_t **ctx_sortie,
                              void **brut_sortie)
{
    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_REGLAGES_WIFI.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_reglages_wifi_ctx_t *ctx = (ecran_reglages_wifi_ctx_t *)brut;
    ECRAN_REGLAGES_WIFI.construire(parent, ctx);
    /* Mock synchrone : le balayage est déjà terminé quand construire() rend la
     * main -- ce premier mettre_a_jour() peuple donc la liste tout de suite. */
    ECRAN_REGLAGES_WIFI.mettre_a_jour(NULL, false, ctx);
    *parent_sortie = parent;
    *ctx_sortie = ctx;
    *brut_sortie = brut;
}

/* --- Groupe 1+2+3 : liste, tap chiffré, garde EN_COURS ------------------ */

static void section_liste_et_taps(void)
{
    printf("suite : ecran reglages wifi (liste + tap chiffre + garde)\n");

    wifi_mock_reset();

    lv_obj_t *parent;
    ecran_reglages_wifi_ctx_t *ctx;
    void *brut;
    construire_ecran(&parent, &ctx, &brut);

    /* Les trois réseaux factices sont listés, dans l'ordre du mock. */
    VERIFIER(ctx->nb == 3);
    VERIFIER_TEXTE(lv_label_get_text(ctx->ssid_labels[0]), "MaBox_5G");
    VERIFIER_TEXTE(lv_label_get_text(ctx->ssid_labels[1]), "Livebox");
    VERIFIER_TEXTE(lv_label_get_text(ctx->ssid_labels[2]), "CafeOuvert");
    /* Ligne 4 masquée : trois réseaux seulement. */
    VERIFIER(lv_obj_has_flag(ctx->boutons[3], LV_OBJ_FLAG_HIDDEN));
    VERIFIER(!lv_obj_has_flag(ctx->boutons[0], LV_OBJ_FLAG_HIDDEN));

    /* Cadenas présent sur les deux chiffrés, absent sur l'ouvert. */
    VERIFIER(!lv_obj_has_flag(ctx->cadenas[0], LV_OBJ_FLAG_HIDDEN));
    VERIFIER(!lv_obj_has_flag(ctx->cadenas[1], LV_OBJ_FLAG_HIDDEN));
    VERIFIER(lv_obj_has_flag(ctx->cadenas[2], LV_OBJ_FLAG_HIDDEN));

    /* En-tête : wifi_etat() rend « MaBox_5G » connecté, reconfig INACTIF. */
    VERIFIER_TEXTE(lv_label_get_text(ctx->etat_label), "Connected to MaBox_5G");

    /* Liste non vide : ni « Recherche… » ni « Aucun réseau » visibles. */
    VERIFIER(lv_obj_has_flag(ctx->recherche_label, LV_OBJ_FLAG_HIDDEN));
    VERIFIER(lv_obj_has_flag(ctx->vide_label, LV_OBJ_FLAG_HIDDEN));

    /* --- Groupe 2 : tap sur MaBox_5G (chiffré) -> clavier -> valider. --- */
    VERIFIER(dernier_enfant_calque_superieur() == NULL); /* aucun clavier avant */
    lv_obj_send_event(ctx->boutons[0], LV_EVENT_CLICKED, NULL);

    lv_obj_t *racine = dernier_enfant_calque_superieur();
    VERIFIER(racine != NULL); /* le clavier a bien été ouvert */
    /* Titre du clavier = SSID tapé (premier label du conteneur clavier). */
    lv_obj_t *titre = enfant_de_classe(racine, &lv_label_class);
    VERIFIER(titre != NULL);
    VERIFIER_TEXTE(lv_label_get_text(titre), "MaBox_5G");

    lv_obj_t *kb = enfant_de_classe(racine, &lv_keyboard_class);
    lv_obj_t *ta = enfant_de_classe(racine, &lv_textarea_class);
    VERIFIER(kb != NULL);
    VERIFIER(ta != NULL);

    lv_textarea_set_text(ta, "secret123");
    lv_obj_send_event(kb, LV_EVENT_READY, NULL);
    lv_timer_handler(); /* exécute la fermeture asynchrone du clavier */

    /* Le mock a enregistré exactement wifi_reconfigurer("MaBox_5G","secret123"). */
    VERIFIER(wifi_mock_appels_reconfigurer() == 1);
    VERIFIER_TEXTE(wifi_mock_dernier_ssid(), "MaBox_5G");
    VERIFIER_TEXTE(wifi_mock_dernier_pass(), "secret123");

    /* --- Groupe 3 : garde EN_COURS. wifi_reconfigurer() ci-dessus a laissé
     * l'état à EN_COURS ; un tap sur un autre réseau chiffré ne doit RIEN
     * relancer (aucun clavier ouvert, aucun nouvel appel). --- */
    VERIFIER(wifi_reconfig_etat() == WIFI_RECONFIG_EN_COURS);
    VERIFIER(dernier_enfant_calque_superieur() == NULL); /* plus de clavier */
    lv_obj_send_event(ctx->boutons[1], LV_EVENT_CLICKED, NULL); /* Livebox */
    VERIFIER(dernier_enfant_calque_superieur() == NULL);        /* toujours aucun clavier */
    VERIFIER(wifi_mock_appels_reconfigurer() == 1);             /* inchangé */

    lv_obj_delete(parent);
    free(brut);
}

/* --- Groupe 4a : réseau ouvert -> reconfig direct, sans clavier --------- */

static void section_reseau_ouvert(void)
{
    printf("suite : ecran reglages wifi (reseau ouvert -> sans clavier)\n");

    wifi_mock_reset();

    lv_obj_t *parent;
    ecran_reglages_wifi_ctx_t *ctx;
    void *brut;
    construire_ecran(&parent, &ctx, &brut);

    VERIFIER(dernier_enfant_calque_superieur() == NULL);
    lv_obj_send_event(ctx->boutons[2], LV_EVENT_CLICKED, NULL); /* CafeOuvert (ouvert) */
    /* Aucun clavier : un réseau ouvert n'a pas de mot de passe. */
    VERIFIER(dernier_enfant_calque_superieur() == NULL);
    /* wifi_reconfigurer("CafeOuvert","") appelé directement. */
    VERIFIER(wifi_mock_appels_reconfigurer() == 1);
    VERIFIER_TEXTE(wifi_mock_dernier_ssid(), "CafeOuvert");
    VERIFIER_TEXTE(wifi_mock_dernier_pass(), "");

    lv_obj_delete(parent);
    free(brut);
}

/* --- Groupe 4b : traduction des états de reconfiguration dans l'en-tête - */

static void section_entete_etats(void)
{
    printf("suite : ecran reglages wifi (en-tete des etats)\n");

    wifi_mock_reset();

    lv_obj_t *parent;
    ecran_reglages_wifi_ctx_t *ctx;
    void *brut;
    construire_ecran(&parent, &ctx, &brut);

    VERIFIER_TEXTE(lv_label_get_text(ctx->etat_label), "Connected to MaBox_5G");

    wifi_mock_definir_reconfig_etat(WIFI_RECONFIG_EN_COURS);
    ECRAN_REGLAGES_WIFI.mettre_a_jour(NULL, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->etat_label), "Connecting...");

    wifi_mock_definir_reconfig_etat(WIFI_RECONFIG_REUSSI);
    ECRAN_REGLAGES_WIFI.mettre_a_jour(NULL, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->etat_label), "Connected to MaBox_5G (saved)");

    wifi_mock_definir_reconfig_etat(WIFI_RECONFIG_CONNECTE_NON_PERSISTE);
    ECRAN_REGLAGES_WIFI.mettre_a_jour(NULL, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->etat_label), "Connected (not saved)");

    wifi_mock_definir_reconfig_etat(WIFI_RECONFIG_ECHOUE);
    ECRAN_REGLAGES_WIFI.mettre_a_jour(NULL, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->etat_label), "Failed - check password");

    /* Retour INACTIF, non connecté : l'en-tête montre l'état courant. */
    wifi_mock_definir_reconfig_etat(WIFI_RECONFIG_INACTIF);
    wifi_mock_definir_etat("", false);
    ECRAN_REGLAGES_WIFI.mettre_a_jour(NULL, false, ctx);
    VERIFIER_TEXTE(lv_label_get_text(ctx->etat_label), "Not connected");

    lv_obj_delete(parent);
    free(brut);
}

void suite_ecran_reglages_wifi(void)
{
    section_liste_et_taps();
    section_reseau_ouvert();
    section_entete_etats();
}
