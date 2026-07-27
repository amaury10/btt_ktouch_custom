#include <string.h>

#include "backend_factice.h"
#include "petit_test.h"

void suite_backend_factice(void)
{
    printf("suite : backend factice\n");

    const backend_desc_t *d = backend_factice_desc();
    VERIFIER_TEXTE(d->nom, "factice");
    VERIFIER(d->taille_etat == sizeof(etat_klipper_t));
    VERIFIER(d->demarrer != NULL && d->rafraichir != NULL);
    VERIFIER(d->arreter != NULL && d->commande != NULL);

    /* Le socle alloue : on imite ce qu'il fera. */
    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));
    backend_hote_t hote = { .adresse = "factice", .port = 0 };
    VERIFIER(d->demarrer(&etat, &hote) == ESP_OK);

    /* Scenario repos. */
    backend_factice_scenario(0);
    VERIFIER(d->rafraichir(&etat) == ESP_OK);
    VERIFIER_TEXTE(etat.etat, "standby");
    VERIFIER(!etat.impression_en_cours);

    /* Scenario impression : la progression doit avancer d'un appel a l'autre,
     * sinon le magasin d'etat ne detecterait aucun changement et l'interface
     * paraitrait figee. */
    backend_factice_scenario(1);
    VERIFIER(d->rafraichir(&etat) == ESP_OK);
    float p1 = etat.progression;
    VERIFIER(etat.impression_en_cours);
    VERIFIER(d->rafraichir(&etat) == ESP_OK);
    VERIFIER(etat.progression > p1);

    /* Scenario pause. */
    backend_factice_scenario(2);
    VERIFIER(d->rafraichir(&etat) == ESP_OK);
    VERIFIER(etat.impression_en_pause);

    /* Scenario extreme : sert a verifier que l'interface ne deborde pas.
     * Plausible (350C, dans [-5, 500]) : distinct du scenario 4 ci-dessous. */
    backend_factice_scenario(3);
    VERIFIER(d->rafraichir(&etat) == ESP_OK);
    VERIFIER(etat.buse_actuelle > 300.0f);
    VERIFIER(strlen(etat.fichier) == KLIPPER_FICHIER_MAX - 1);

    /* Scenario aberrant : hors plage plausible, sert a verifier qu'un
     * affichage rend "--" plutot qu'un nombre faux (voir
     * ui_format_temperature() dans ui/widgets/tuile.h, plage [-5, 500]). */
    backend_factice_scenario(4);
    VERIFIER(d->rafraichir(&etat) == ESP_OK);
    VERIFIER(etat.buse_actuelle > 500.0f);
    VERIFIER(etat.plateau_actuel < -5.0f);

    /* Les actions connues sont acceptees, les inconnues refusees explicitement. */
    VERIFIER(d->commande(&etat, BACKEND_ACTION_PAUSE, NULL) == ESP_OK);
    VERIFIER(d->commande(&etat, "action_inexistante", NULL) == ESP_ERR_NOT_SUPPORTED);

    d->arreter(&etat);
}
