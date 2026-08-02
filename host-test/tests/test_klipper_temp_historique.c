#include <math.h>
#include <string.h>

#include "klipper_temp_historique.h"
#include "petit_test.h"

/* Petite fabrique d'etat Klipper a plat, zero partout sauf les champs que le
 * store lit (extrudeurs[0], extrudeurs[1], plateau) -- le store est
 * totalement independant du reste de etat_klipper_t (etat texte, position,
 * fichiers...), donc les laisser a zero ne fausse aucune verification ici. */
static etat_klipper_t etat_temperatures(float e0, bool e0_presente,
                                         float e1, bool e1_presente,
                                         float plateau, bool plateau_presente)
{
    etat_klipper_t e;
    memset(&e, 0, sizeof(e));
    e.extrudeurs[0].actuelle = e0;
    e.extrudeurs[0].presente = e0_presente;
    e.extrudeurs[1].actuelle = e1;
    e.extrudeurs[1].presente = e1_presente;
    e.plateau.actuelle       = plateau;
    e.plateau.presente       = plateau_presente;
    return e;
}

/* Cette suite DOIT etre la PREMIERE a manipuler klipper_temp_historique
 * (store statique process-wide, meme politique que klipper_fichiers.c) --
 * garantie par tests/main.c, qui l'appelle juste avant
 * suite_ecran_accueil_hub() (sous-projet "graphes de temperature", tache 3 :
 * ecran_accueil_hub.c construit desormais un `lv_chart` backfille depuis ce
 * meme store, et sa suite pousse elle-meme quelques points connus PAR-DESSUS
 * l'etat que CETTE suite laisse derriere elle). Le contrat public de ce store
 * n'offre aucune remise a zero : (b) le wraparound ci-dessous EXIGE un
 * tampon VIERGE (130 pousser consecutifs de valeurs 0..129 sur la serie 0,
 * verifies un a un), pas seulement generation()==0 -- une suite qui
 * s'executerait avant celle-ci le romprait, meme si elle ne poussait que des
 * valeurs sans rapport. Une fois le tampon plein (120/120), il RESTE plein
 * pour toujours (chaque pousser suivant evince exactement le point le plus
 * ancien) -- les sections suivantes de CETTE suite s'appuient sur cette
 * propriete de regime permanent plutot que sur un tampon vide, et
 * suite_ecran_accueil_hub() en herite a son tour. */
void suite_klipper_temp_historique(void)
{
    printf("suite : klipper_temp_historique\n");

    VERIFIER(klipper_temp_historique_generation() == 0);

    /* (b) Wraparound : 130 pousser de valeurs 0..129 sur la serie 0
     * (extrudeurs[0]). Le tampon (120 points) ne garde que les 120 plus
     * recents : 10..129, en ordre chronologique. */
    for (int k = 0; k < 130; k++) {
        etat_klipper_t e = etat_temperatures((float)k, true, 0.0f, false, 0.0f, false);
        klipper_temp_historique_pousser(&e);
    }
    VERIFIER(klipper_temp_historique_generation() == 130);

    int16_t buf[KLIPPER_HISTO_POINTS];
    size_t  n = klipper_temp_historique_serie(0, buf, KLIPPER_HISTO_POINTS);
    VERIFIER(n == KLIPPER_HISTO_POINTS);
    bool chrono_ok = true;
    for (int k = 0; k < KLIPPER_HISTO_POINTS; k++) {
        if (buf[k] != (int16_t)(10 + k)) {
            chrono_ok = false;
            break;
        }
    }
    VERIFIER(chrono_ok);

    int16_t dernier_b = -1;
    VERIFIER(klipper_temp_historique_dernier(0, &dernier_b));
    VERIFIER(dernier_b == 129);

    /* (a) 3 pousser supplementaires avec un etat {2 extrudeurs presents,
     * plateau present}. Le tampon reste plein (120/120) : ces 3 pousser
     * evincent exactement les 3 points les plus anciens (valeurs 10, 11, 12
     * de la serie 0 poussees ci-dessus). */
    etat_klipper_t e1 = etat_temperatures(200.0f, true, 190.0f, true, 60.0f, true);
    etat_klipper_t e2 = etat_temperatures(201.0f, true, 191.0f, true, 61.0f, true);
    etat_klipper_t e3 = etat_temperatures(202.4f, true, 192.6f, true, 62.5f, true);
    klipper_temp_historique_pousser(&e1);
    klipper_temp_historique_pousser(&e2);
    klipper_temp_historique_pousser(&e3);
    VERIFIER(klipper_temp_historique_generation() == 133);

    int16_t dernier_e0, dernier_e1, dernier_plateau;
    VERIFIER(klipper_temp_historique_dernier(0, &dernier_e0));
    VERIFIER(klipper_temp_historique_dernier(1, &dernier_e1));
    VERIFIER(klipper_temp_historique_dernier(KLIPPER_EXTRUDEURS_MAX, &dernier_plateau));
    VERIFIER(dernier_e0 == 202);      /* lroundf(202.4) */
    VERIFIER(dernier_e1 == 193);      /* lroundf(192.6) */
    VERIFIER(dernier_plateau == 63);  /* lroundf(62.5) -- moitie arrondie loin de zero */

    n = klipper_temp_historique_serie(0, buf, KLIPPER_HISTO_POINTS);
    VERIFIER(n == KLIPPER_HISTO_POINTS);
    VERIFIER(buf[0] == 13);   /* nouveau plus ancien : 10, 11, 12 evinces */
    VERIFIER(buf[116] == 129);
    VERIFIER(buf[117] == 200);
    VERIFIER(buf[118] == 201);
    VERIFIER(buf[119] == 202);

    /* (c) serie_presente reflete `presente` tel que recu au dernier pousser. */
    VERIFIER(klipper_temp_historique_serie_presente(0) == true);
    VERIFIER(klipper_temp_historique_serie_presente(1) == true);
    VERIFIER(klipper_temp_historique_serie_presente(2) == false); /* jamais mis a true */
    VERIFIER(klipper_temp_historique_serie_presente(KLIPPER_EXTRUDEURS_MAX) == true);

    /* (e) serie/indice invalide -- au-dela de KLIPPER_HISTO_SERIES. */
    VERIFIER(klipper_temp_historique_serie_presente(KLIPPER_HISTO_SERIES) == false);
    int16_t hors_bornes = -1;
    VERIFIER(!klipper_temp_historique_dernier(KLIPPER_HISTO_SERIES, &hors_bornes));
    VERIFIER(!klipper_temp_historique_dernier(255, &hors_bornes));
    VERIFIER(!klipper_temp_historique_dernier(0, NULL));
    n = klipper_temp_historique_serie(KLIPPER_HISTO_SERIES, buf, KLIPPER_HISTO_POINTS);
    VERIFIER(n == 0);
    n = klipper_temp_historique_serie(0, NULL, KLIPPER_HISTO_POINTS);
    VERIFIER(n == 0);

    /* pousser(NULL) : no-op, generation inchangee. */
    klipper_temp_historique_pousser(NULL);
    VERIFIER(klipper_temp_historique_generation() == 133);

    /* (f) actuelle non finie (NaN/+Inf/-Inf) : pas de plantage (ASan/UBSan
     * actifs sur cette cible, voir host-test/CMakeLists.txt), valeur finie
     * stockee (repliee sur 0 plutot que de propager l'indetermine). */
    etat_klipper_t e_nan = etat_temperatures(0.0f / 0.0f, true, 1.0f / 0.0f, true, -1.0f / 0.0f, true);
    klipper_temp_historique_pousser(&e_nan);
    VERIFIER(klipper_temp_historique_generation() == 134);
    int16_t v_nan;
    VERIFIER(klipper_temp_historique_dernier(0, &v_nan));
    VERIFIER(v_nan == 0);
    VERIFIER(klipper_temp_historique_dernier(1, &v_nan));
    VERIFIER(v_nan == 0);
    VERIFIER(klipper_temp_historique_dernier(KLIPPER_EXTRUDEURS_MAX, &v_nan));
    VERIFIER(v_nan == 0);
}
