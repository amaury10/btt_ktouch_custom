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
    VERIFIER(etat.extrudeurs[0].actuelle > 300.0f);
    VERIFIER(strlen(etat.fichier) == KLIPPER_FICHIER_MAX - 1);

    /* Scenario aberrant : hors plage plausible, sert a verifier qu'un
     * affichage rend "--" plutot qu'un nombre faux (voir
     * ui_format_temperature() dans ui/widgets/tuile.h, plage [-5, 500]). */
    backend_factice_scenario(4);
    VERIFIER(d->rafraichir(&etat) == ESP_OK);
    VERIFIER(etat.extrudeurs[0].actuelle > 500.0f);
    VERIFIER(etat.plateau.actuelle < -5.0f);

    /* Un scenario mono-extrudeur remplit nb_extrudeurs et presente. */
    VERIFIER(etat.nb_extrudeurs == 1);
    VERIFIER(etat.extrudeurs[0].presente == true);
    VERIFIER(etat.extrudeurs[1].presente == false);

    /* Les actions connues sont acceptees, les inconnues refusees explicitement. */
    VERIFIER(d->commande(&etat, BACKEND_ACTION_PAUSE, NULL) == ESP_OK);
    VERIFIER(d->commande(&etat, "action_inexistante", NULL) == ESP_ERR_NOT_SUPPORTED);

    /* Scenario 10 (« CR-10 ») : imprimante mono-extrudeur d'entree de gamme,
     * au repos, plateau chauffant, quatre macros simples. Les scenarios 0-9
     * ci-dessus (jalon 2b) restent inchanges ; 10/11/12 sont les nouveaux
     * paliers de la tache 2, jalon 3a. */
    backend_factice_scenario(10);
    VERIFIER(d->rafraichir(&etat) == ESP_OK);
    VERIFIER(etat.nb_extrudeurs == 1);
    VERIFIER(etat.extrudeurs[0].presente == true);
    VERIFIER(etat.extrudeurs[1].presente == false);
    VERIFIER(etat.plateau.presente == true);
    VERIFIER(!etat.impression_en_cours);
    VERIFIER(etat.nb_macros == 4);
    VERIFIER_TEXTE(etat.macros[0], "BED_MESH_CALIBRATE");
    VERIFIER_TEXTE(etat.macros[3], "LIGHTS_TOGGLE");
    VERIFIER(!etat.macros_tronquees);

    /* Scenario 11 (« U1 ») : changeur d'outils a quatre extrudeurs. outil_actif
     * doit evoluer d'un cycle a l'autre (voir le brief : « qui tourne » comme
     * le ferait un vrai changeur), huit macros dont une prefixee "_" (filtree
     * par l'interface plus tard, pas ici), une a parametres (PURGE_PARAM) et
     * une qui echoue systematiquement a la commande (MACRO_ECHEC). */
    backend_factice_scenario(11);
    VERIFIER(d->rafraichir(&etat) == ESP_OK);
    VERIFIER(etat.nb_extrudeurs == 4);
    VERIFIER(etat.extrudeurs[0].presente == true);
    VERIFIER(etat.extrudeurs[1].presente == true);
    VERIFIER(etat.extrudeurs[2].presente == true);
    VERIFIER(etat.extrudeurs[3].presente == true);
    VERIFIER(etat.extrudeurs[4].presente == false);
    VERIFIER(etat.plateau.presente == true);
    uint8_t outil_actif_cycle1 = etat.outil_actif;
    VERIFIER(d->rafraichir(&etat) == ESP_OK);
    uint8_t outil_actif_cycle2 = etat.outil_actif;
    VERIFIER(outil_actif_cycle2 != outil_actif_cycle1);
    VERIFIER(etat.nb_macros == 8);
    VERIFIER_TEXTE(etat.macros[0], "HOME_ALL");
    VERIFIER_TEXTE(etat.macros[7], "CALIBRATE_OFFSETS");
    VERIFIER(!etat.macros_tronquees);

    /* Scenario 12 (« 8 tetes ») : machine a huit extrudeurs synthetiques et
     * 48 macros -- exactement KLIPPER_MACROS_MAX, la borne reelle de la
     * structure (voir le CRITICAL corrige a la revue de la tache 1 sur cet
     * OOB precis, firmware/main/web_macros.h). macros_tronquees signale que
     * le producteur en connait davantage que ce que la structure peut porter
     * -- vrai UNIQUEMENT sur ce scenario parmi tous ceux de ce fichier. */
    backend_factice_scenario(12);
    VERIFIER(d->rafraichir(&etat) == ESP_OK);
    VERIFIER(etat.nb_extrudeurs == 8);
    for (int i = 0; i < 8; i++) {
        VERIFIER(etat.extrudeurs[i].presente == true);
    }
    VERIFIER(etat.nb_macros == KLIPPER_MACROS_MAX);
    VERIFIER_TEXTE(etat.macros[0], "MACRO_01");
    VERIFIER_TEXTE(etat.macros[KLIPPER_MACROS_MAX - 1], "MACRO_48");
    VERIFIER(etat.macros_tronquees == true);

    /* BACKEND_ACTION_MACRO : parcourt arguments_json = {"nom":"<macro>"}.
     * MACRO_ECHEC echoue toujours (sentinelle de demonstration, voir le
     * scenario 11 ci-dessus) ; PURGE_PARAM, elle, est une macro connue et
     * reussit -- exactement le contraste que demande le brief. */
    VERIFIER(d->commande(&etat, BACKEND_ACTION_MACRO, "{\"nom\":\"PURGE_PARAM\"}") == ESP_OK);
    VERIFIER(d->commande(&etat, BACKEND_ACTION_MACRO, "{\"nom\":\"MACRO_ECHEC\"}") == ESP_FAIL);
    VERIFIER(d->commande(&etat, BACKEND_ACTION_MACRO, "{\"nom\":\"MACRO_INCONNUE\"}") == ESP_ERR_NOT_SUPPORTED);
    VERIFIER(d->commande(&etat, BACKEND_ACTION_MACRO, NULL) == ESP_ERR_NOT_SUPPORTED);

    d->arreter(&etat);
}
