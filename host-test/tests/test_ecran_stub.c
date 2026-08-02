/* Sous-projet "panneaux KlipperScreen", tache 7 : smoke test des six ecrans
 * stub (Power/Bed Mesh/Input Shaper/Spoolman/Updater/Console) -- backend
 * absent pour chacun (voir ecran_stub.h), donc rien a verifier au-dela du
 * contrat minimal : id/titre non vides et attendus, construire() ne plante
 * pas. Construction directe (calloc du contexte a la taille du descripteur,
 * puis <ECRAN>.construire()) plutot que navigation_empiler() -- meme choix
 * que test_ecran_niveau_lit.c/test_ecran_zcalibrate.c, pour la meme raison
 * (tester uniquement le contrat de cet ecran).
 *
 * Contrairement aux autres suites d'ecran de ce fichier, AUCUNE dependance
 * sur habillage_est_construit()/source_etat_sim_est_demarre() : ces six
 * ecrans n'envoient jamais de commande et ne lisent jamais l'etat backend
 * (mettre_a_jour = NULL), donc aucun seam a tracer -- peut tourner a
 * n'importe quel endroit de tests/main.c. */
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"

#include "ecran_stub.h"
#include "petit_test.h"

typedef struct {
    const ecran_desc_t *ecran;
    const char *id_attendu;
    const char *titre_attendu;
} stub_attendu_t;

static void verifier_stub(const stub_attendu_t *attendu)
{
    const ecran_desc_t *ecran = attendu->ecran;

    VERIFIER(ecran->id != NULL);
    VERIFIER(ecran->id[0] != '\0');
    VERIFIER_TEXTE(ecran->id, attendu->id_attendu);

    VERIFIER(ecran->titre != NULL);
    VERIFIER(ecran->titre[0] != '\0');
    VERIFIER_TEXTE(ecran->titre, attendu->titre_attendu);

    VERIFIER(ecran->construire != NULL);
    /* Aucune donnee dynamique (voir ecran_stub.h) : ni mise a jour, ni
     * destruction. */
    VERIFIER(ecran->mettre_a_jour == NULL);
    VERIFIER(ecran->detruire == NULL);

    lv_obj_t *parent = lv_obj_create(lv_screen_active());
    void *contexte = NULL;
    if (ecran->taille_contexte > 0) {
        contexte = calloc(1, ecran->taille_contexte);
        VERIFIER(contexte != NULL);
    }

    /* Le smoke test : construire() ne doit pas planter, meme sans backend ni
     * boucle simulee derriere lui. */
    ecran->construire(parent, contexte);

    free(contexte);
    lv_obj_delete(parent);
}

void suite_ecran_stub(void)
{
    printf("suite : ecrans stub (backend absent)\n");

    static const stub_attendu_t STUBS_ATTENDUS[] = {
        { &ECRAN_POWER, "power", "Power" },
        { &ECRAN_BED_MESH, "bed_mesh", "Bed Mesh" },
        { &ECRAN_INPUT_SHAPER, "input_shaper", "Input Shaper" },
        { &ECRAN_SPOOLMAN, "spoolman", "Spoolman" },
        { &ECRAN_UPDATER, "updater", "Updater" },
        { &ECRAN_CONSOLE, "console", "Console" },
    };

    for (size_t i = 0; i < sizeof(STUBS_ATTENDUS) / sizeof(STUBS_ATTENDUS[0]); i++) {
        verifier_stub(&STUBS_ATTENDUS[i]);
    }
}
