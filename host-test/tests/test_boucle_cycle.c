#include <stdint.h>

#include "backend_factice.h"
#include "boucle_cycle.h"
#include "etat_store.h"
#include "liaison.h"
#include "petit_test.h"

/* Rejoue, sur PC, l'enchaînement exact de boucle_tache() (core/boucle.c) sur
 * une douzaine de cycles : c'est le chemin qui portait CRITICAL 1 de la revue
 * de fin de jalon. boucle_cycle() remet le tampon arrière à zéro juste avant
 * d'appeler desc->rafraichir() (voir backend.h et boucle_cycle.h) ; un
 * backend qui lirait sa propre progression depuis ce même tampon (comme le
 * faisait backend_factice.c avant son correctif) la verrait donc toujours à
 * zéro, et ni la progression ni la génération n'avanceraient jamais — écrit
 * avant le correctif de CRITICAL 1, ce test échoue ; après, il passe. C'est
 * précisément le but de l'écrire d'abord. */
void suite_boucle_cycle(void)
{
    printf("suite : boucle_cycle\n");

    const backend_desc_t *d = backend_factice_desc();
    VERIFIER(d != NULL);
    if (d == NULL) {
        return;
    }

    etat_store_t store;
    VERIFIER(etat_store_init(&store, d->taille_etat));

    liaison_t liaison;
    liaison_init(&liaison, 3, 10);

    /* Scénario 1 : impression en cours, seul scénario dont la progression est
     * censée avancer d'un appel à l'autre (voir backend_factice.h). */
    backend_factice_scenario(1);

    /* Démarrage, comme le fait boucle_demarrer() (core/boucle.c) : demarrer()
     * écrit sur le tampon arrière (déjà à zéro par etat_store_init()), puis
     * une première validation. Les deux tampons restent identiques (les deux
     * calloc'és à zéro par etat_store_init(), et demarrer() du backend
     * factice remet lui aussi tout à zéro), donc cette première validation ne
     * change rien : generation reste à 0, exactement le comportement
     * documenté dans boucle.c. */
    backend_hote_t hote = { .adresse = "factice", .port = 0 };
    void *initial = etat_store_tampon_arriere(&store);
    VERIFIER(d->demarrer(initial, &hote) == ESP_OK);
    etat_store_valider(&store);

    uint32_t generation_avant = etat_store_generation(&store);
    VERIFIER(generation_avant == 0);

    /* Une douzaine de cycles : boucle_cycle() rend true sur succès, à charge
     * pour l'appelant de valider le magasin — exactement comme boucle_tache()
     * le fait sous mutex sur cible (ici sans mutex : un seul fil
     * d'exécution, comme tout ce harnais). progression_premier_cycle capture
     * l'état juste après le TOUT PREMIER cycle : c'est le point de
     * comparaison qui piège vraiment CRITICAL 1. Avec le défaut (backend_factice.c
     * relit sa progression depuis le tampon arrière que boucle_cycle() vient
     * de remettre à zéro), chaque cycle produit exactement 0.01 sans jamais
     * varier — comparer à la valeur d'avant la boucle (0.0) masquerait ce
     * défaut, puisque 0.01 > 0.0 resterait vrai même figé pour toujours ;
     * comparer au premier cycle, non. */
    int succes_compte = 0;
    float progression_premier_cycle = 0.0f;
    for (int i = 0; i < 12; i++) {
        bool succes = boucle_cycle(&store, &liaison, d);
        VERIFIER(succes);
        if (succes) {
            succes_compte++;
            etat_store_valider(&store);
        }
        if (i == 0) {
            progression_premier_cycle = ((const etat_klipper_t *)etat_store_lire(&store))->progression;
        }
    }
    VERIFIER(succes_compte == 12);

    uint32_t generation_apres = etat_store_generation(&store);
    float progression_finale = ((const etat_klipper_t *)etat_store_lire(&store))->progression;

    /* Le coeur de CRITICAL 1, décrit par la revue : "progression pinned at
     * 0.0100 forever". Sans le correctif, progression_finale ==
     * progression_premier_cycle (0.01 les deux fois) et ce VERIFIER échoue. */
    VERIFIER(progression_finale > progression_premier_cycle);

    /* Corollaire, décrit par la même revue : "generation stops at 1". Le
     * magasin d'état ne détecte un changement, donc n'avance sa génération,
     * que si le contenu change réellement d'un cycle à l'autre
     * (etat_store_valider()) — sans progression qui avance après le premier
     * cycle, generation resterait bloquée à 1 au lieu d'atteindre 12. */
    VERIFIER(generation_apres > generation_avant);
    VERIFIER(generation_apres == 12);

    VERIFIER(liaison_etat(&liaison) == LIAISON_EN_LIGNE);
    VERIFIER(liaison_echecs_consecutifs(&liaison) == 0);

    etat_store_liberer(&store);
}
