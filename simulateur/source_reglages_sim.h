/* Petit hors-contrat de simulateur/source_reglages_sim.c, dans le même esprit
 * que source_etat_sim.h : `source_reglages_sim_reinit()` n'appartient pas à
 * la façade elle-même (ui/source_reglages.h, identique des deux côtés), elle
 * n'a de sens que pour un consommateur qui sait qu'il tourne sur
 * l'implémentation PC.
 *
 * Réutilisé par host-test/tests/test_ecran_configuration.c (revue tâche 8,
 * round 1, Q8) : sans elle, `g_hote`/`g_configure` de source_reglages_sim.c
 * restent des variables statiques de FICHIER, partagées par toute la durée
 * de vie du binaire de tests -- un ui_reglages_definir_hote() appelé par une
 * section de la suite reste visible aux sections suivantes, rendant l'ordre
 * d'exécution significatif (défaut récurrent de ce jalon, déjà vu ailleurs
 * sous forme de singletons file-static -- clavier.c, confirmation.c,
 * habillage.c). Jamais utilisée par simulateur/main.c lui-même : chaque
 * lancement du simulateur est un process neuf, qui repart déjà de zéro. */
#pragma once

/* Remet l'hôte simulé à son état initial ("" / port par défaut / non
 * configuré) -- exactement l'état de source_reglages_sim.c avant tout premier
 * ui_reglages_definir_hote(). À appeler en tête de toute suite de tests qui
 * doit connaître l'état de départ de la façade réglages. */
void source_reglages_sim_reinit(void);
