/* Task 2 (jalon OTA firmware) : smoke test de l'ecran Updater -- ex-sixieme
 * stub de la tache 7 (voir ecran_stub.c), remplace par un vrai ecran d'etat
 * en lecture seule (slot OTA courant, version firmware, rappel de
 * procedure). Hors ESP_PLATFORM (ce harnais tourne sur PC), ecran_updater.c
 * replie sur "Slot: sim"/"Version: dev" -- ce test verifie donc ces valeurs
 * de repli exactement, pas les valeurs cible (esp_ota_get_running_partition()/
 * esp_app_get_description() ne sont testables qu'au flash reel, voir
 * task-2-brief.md).
 *
 * Construction directe (calloc du contexte a la taille du descripteur, puis
 * ECRAN_UPDATER.construire()) plutot que navigation_empiler() -- meme choix
 * que test_ecran_stub.c/test_ecran_niveau_lit.c, pour la meme raison (tester
 * uniquement le contrat de cet ecran). AUCUNE dependance sur
 * habillage_est_construit()/source_etat_sim_est_demarre() : cet ecran
 * n'envoie jamais de commande et ne lit jamais l'etat backend
 * (mettre_a_jour = NULL), meme absence de seam que suite_ecran_stub() --
 * peut donc tourner a n'importe quel endroit de tests/main.c. */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "ecran_updater.h"
#include "petit_test.h"

void suite_ecran_updater(void)
{
    printf("suite : ecran updater (etat, lecture seule)\n");

    VERIFIER(ECRAN_UPDATER.id != NULL);
    VERIFIER_TEXTE(ECRAN_UPDATER.id, "updater");
    VERIFIER(ECRAN_UPDATER.titre != NULL);
    VERIFIER_TEXTE(ECRAN_UPDATER.titre, "Updater");
    VERIFIER(ECRAN_UPDATER.construire != NULL);
    /* Contenu entierement statique (voir ecran_updater.h) : ni mise a jour,
     * ni destruction au-dela du contexte alloue par le socle. */
    VERIFIER(ECRAN_UPDATER.mettre_a_jour == NULL);
    VERIFIER(ECRAN_UPDATER.detruire == NULL);

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *brut = calloc(1, ECRAN_UPDATER.taille_contexte);
    VERIFIER(brut != NULL);
    ecran_updater_ctx_t *ctx = (ecran_updater_ctx_t *)brut;

    /* Le smoke test : construire() ne doit pas planter, meme sans backend ni
     * boucle simulee derriere lui. */
    ECRAN_UPDATER.construire(parent, ctx);

    VERIFIER(ctx->label_slot != NULL);
    VERIFIER(ctx->label_version != NULL);
    VERIFIER(ctx->label_update != NULL);

    /* Hors ESP_PLATFORM (ce harnais), ecran_updater.c replie sur ces deux
     * valeurs exactes -- voir son commentaire de tete. */
    VERIFIER_TEXTE(lv_label_get_text(ctx->label_slot), "Slot: sim");
    VERIFIER_TEXTE(lv_label_get_text(ctx->label_version), "Version: dev");
    /* Rappel de procedure, texte FIXE quelle que soit la plateforme -- meme
     * valeur EXACTE que le brief de la tache. */
    VERIFIER_TEXTE(lv_label_get_text(ctx->label_update), "Update via /ota (browser)");

    lv_obj_delete(parent);
    free(brut);
}
