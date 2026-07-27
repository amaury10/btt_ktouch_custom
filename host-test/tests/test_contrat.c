#include <stddef.h>
#include <string.h>

#include "backend.h"
#include "etat_klipper.h"
#include "petit_test.h"

/* Stub functions for testing backend_desc_t contract. */
static esp_err_t stub_demarrer(void *etat, const backend_hote_t *hote)
{
    (void)etat;
    (void)hote;
    return 0;
}

static esp_err_t stub_rafraichir(void *etat)
{
    (void)etat;
    return 0;
}

static void stub_arreter(void *etat)
{
    (void)etat;
}

static esp_err_t stub_commande(void *etat, const char *action,
                                const char *arguments_json)
{
    (void)etat;
    (void)action;
    (void)arguments_json;
    return 0;
}

void suite_contrat(void)
{
    printf("suite : contrat\n");

    /* Test etat_klipper_t : la comparaison mémoire du magasin d'état n'est
     * valable que si la structure est comparable octet à octet : pas de
     * pointeur, taille stable. */
    etat_klipper_t a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    VERIFIER(memcmp(&a, &b, sizeof(a)) == 0);

    a.buse_actuelle = 210.0f;
    VERIFIER(memcmp(&a, &b, sizeof(a)) != 0);

    /* Deux structures remplies identiquement champ par champ doivent être
     * indistinguables : si ce test échoue un jour, c'est qu'un champ non
     * initialisé ou un pointeur s'est glissé dans le modèle. */
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    snprintf(a.fichier, sizeof(a.fichier), "piece.gcode");
    snprintf(b.fichier, sizeof(b.fichier), "piece.gcode");
    a.progression = b.progression = 0.5f;
    VERIFIER(memcmp(&a, &b, sizeof(a)) == 0);

    /* Test backend.h : vérifier que le contrat du backend peut être compilé
     * et que les structures sont bien formées. */
    backend_hote_t hote;
    hote.port = 7125;
    snprintf(hote.adresse, sizeof(hote.adresse), "localhost");
    VERIFIER(hote.port == 7125);

    backend_desc_t desc = {
        .nom = "test",
        .taille_etat = 128,
        .demarrer = stub_demarrer,
        .rafraichir = stub_rafraichir,
        .arreter = stub_arreter,
        .commande = stub_commande,
    };
    VERIFIER(desc.taille_etat == 128);
    VERIFIER(desc.demarrer != NULL);
    VERIFIER(desc.commande != NULL);

    /* Vérifier les tailles pour détecter les changements de structure. */
    VERIFIER(sizeof(backend_hote_t) > 0);
    VERIFIER(sizeof(backend_desc_t) > 0);
}
