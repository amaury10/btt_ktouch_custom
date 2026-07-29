#include <string.h>

#include "etat_klipper.h"
#include "moonraker_boite.h"
#include "petit_test.h"

/* Tâche 5, jalon 3a : la boîte aux lettres portable entre la tâche WS et
 * `backend_moonraker_rafraichir()` — voir moonraker_boite.h pour le contrat
 * complet (un slot, pas une file ; écrasement ; pas de verrou, ce harnais
 * l'appelle depuis un seul fil d'exécution). */

static void section_depot_puis_drain(void)
{
    printf("suite : moonraker_boite (depot puis drain)\n");

    moonraker_boite_t b;
    memset(&b, 0, sizeof(b));
    VERIFIER(boite_a_du_neuf(&b) == false);

    etat_klipper_t depose;
    memset(&depose, 0, sizeof(depose));
    strncpy(depose.etat, "printing", sizeof(depose.etat) - 1);
    depose.progression = 0.42f;

    boite_deposer(&b, &depose);
    VERIFIER(boite_a_du_neuf(&b) == true);

    etat_klipper_t sortie;
    memset(&sortie, 0xAA, sizeof(sortie)); /* sentinelle : ne doit rien garder de ce bruit apres un drain reussi */
    VERIFIER(boite_drainer(&b, &sortie) == true);
    VERIFIER(memcmp(&sortie, &depose, sizeof(sortie)) == 0);

    /* Le drain consomme : la boite redevient vide. */
    VERIFIER(boite_a_du_neuf(&b) == false);
}

static void section_deux_depots_un_seul_drain(void)
{
    printf("suite : moonraker_boite (deux depots, un seul drain -- ecrasement)\n");

    moonraker_boite_t b;
    memset(&b, 0, sizeof(b));

    etat_klipper_t premier;
    memset(&premier, 0, sizeof(premier));
    strncpy(premier.etat, "premier", sizeof(premier.etat) - 1);

    etat_klipper_t second;
    memset(&second, 0, sizeof(second));
    strncpy(second.etat, "second", sizeof(second.etat) - 1);

    boite_deposer(&b, &premier);
    boite_deposer(&b, &second); /* ecrase le premier depot, jamais rejoue */
    VERIFIER(boite_a_du_neuf(&b) == true);

    etat_klipper_t sortie;
    memset(&sortie, 0, sizeof(sortie));
    VERIFIER(boite_drainer(&b, &sortie) == true);
    VERIFIER_TEXTE(sortie.etat, "second"); /* jamais "premier" */

    /* Un second drain sur une boite deja videe : false, sortie intacte. */
    etat_klipper_t sentinelle;
    memset(&sentinelle, 0x55, sizeof(sentinelle));
    etat_klipper_t sortie2 = sentinelle;
    VERIFIER(boite_drainer(&b, &sortie2) == false);
    VERIFIER(memcmp(&sortie2, &sentinelle, sizeof(sortie2)) == 0);
}

static void section_drain_sur_vide(void)
{
    printf("suite : moonraker_boite (drain sur boite jamais servie)\n");

    moonraker_boite_t b;
    memset(&b, 0, sizeof(b));

    etat_klipper_t sentinelle;
    memset(&sentinelle, 0x77, sizeof(sentinelle));
    etat_klipper_t sortie = sentinelle;

    VERIFIER(boite_a_du_neuf(&b) == false);
    VERIFIER(boite_drainer(&b, &sortie) == false);
    VERIFIER(memcmp(&sortie, &sentinelle, sizeof(sortie)) == 0);
}

static void section_a_du_neuf_bascule(void)
{
    printf("suite : moonraker_boite (a_du_neuf bascule)\n");

    moonraker_boite_t b;
    memset(&b, 0, sizeof(b));
    etat_klipper_t etat;
    memset(&etat, 0, sizeof(etat));

    VERIFIER(boite_a_du_neuf(&b) == false);
    boite_deposer(&b, &etat);
    VERIFIER(boite_a_du_neuf(&b) == true);

    etat_klipper_t sortie;
    VERIFIER(boite_drainer(&b, &sortie) == true);
    VERIFIER(boite_a_du_neuf(&b) == false);

    /* Un nouveau depot apres un drain fait a nouveau basculer le drapeau. */
    boite_deposer(&b, &etat);
    VERIFIER(boite_a_du_neuf(&b) == true);
}

void suite_moonraker_boite(void)
{
    section_depot_puis_drain();
    section_deux_depots_un_seul_drain();
    section_drain_sur_vide();
    section_a_du_neuf_bascule();
}
