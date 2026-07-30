/* Fait tourner, côté PC et sans FreeRTOS, le même protocole que
 * core/boucle.c (magasin d'état + liaison + file de commandes), pour que
 * source_etat_sim.c puisse implémenter ui_etat_instantane()/ui_commander()
 * (voir firmware/main/ui/source_etat.h) en lisant exactement ce qu'un écran
 * lirait sur cible.
 *
 * Ce petit couple de fonctions n'appartient pas au contrat de la façade
 * elle-même (source_etat.h, identique des deux côtés) : c'est ce dont
 * simulateur/main.c a besoin pour la piloter — l'équivalent, en un appel
 * explicite plutôt qu'une tâche FreeRTOS en boucle, de boucle_demarrer() et
 * d'une itération de boucle_tache(). Réutilisé tel quel par
 * host-test/CMakeLists.txt : le harnais de tests hôte a besoin des mêmes
 * symboles ui_etat_instantane()/ui_commander() pour lier habillage.c, même
 * si aucun test ne les exerce directement (voir test_habillage.c, qui ne
 * teste que les fonctions pures). */
#pragma once

#include <stdbool.h>

#include "backend.h"

/* Démarre la boucle simulée : initialise le magasin d'état à la taille du
 * backend, appelle une fois `desc->demarrer(..., NULL)` (aucun hôte réseau
 * réel côté simulateur), puis valide l'état initial — même séquence que
 * boucle_demarrer() (core/boucle.c), sans tâche ni mutex ni file avec
 * attente : le simulateur est mono-thread, rien ici ne peut être préempté
 * entre deux instructions.
 *
 * Rend false si `desc` est incomplet (un des quatre pointeurs de rappel est
 * NULL), si un backend est déjà démarré, ou si l'initialisation du magasin
 * d'état échoue — mêmes cas que boucle_demarrer(), qui rend ESP_ERR_INVALID_ARG
 * / ESP_ERR_INVALID_STATE / ESP_ERR_NO_MEM pour les trois mêmes situations. */
bool source_etat_sim_demarrer(const backend_desc_t *desc);

/* Un cycle : dépile et exécute les commandes en attente (déposées par
 * ui_commander()), puis appelle boucle_cycle() et valide le magasin d'état
 * en cas de succès — même ordre que boucle_tache() (traiter les commandes
 * avant de rafraîchir, pour qu'une commande acceptée entre deux cycles ne
 * dorme jamais plus d'un cycle avant d'être exécutée).
 *
 * Ne fait rien si source_etat_sim_demarrer() n'a pas été appelé. Ne fait
 * jamais avancer le temps ni ne bloque : c'est à l'appelant (simulateur/main.c)
 * de décider du rythme auquel il appelle cette fonction. */
void source_etat_sim_cycle(void);

/* EXPOSÉ POUR LES TESTS UNIQUEMENT (tâche 9) : nombre de commandes
 * actuellement en attente dans la file interne -- jamais consommé par
 * simulateur/main.c, qui n'a besoin que des deux fonctions ci-dessus.
 * ui_commander() (source_etat.h) ne dit que "acceptée ou non", jamais combien
 * de commandes attendent déjà ; host-test/tests/test_commandes.c s'en sert
 * pour prouver qu'un dialogue de confirmation DÉCLINÉ n'empile RIEN (la
 * taille de la file ne bouge pas), ce qu'aucune fonction publique de
 * source_etat.h ne permet d'observer directement. 0 avant tout démarrage. */
size_t source_etat_sim_file_taille(void);

/* EXPOSÉ POUR LES TESTS UNIQUEMENT, même raison que ci-dessus : vrai si
 * source_etat_sim_demarrer() a déjà réussi (singleton process-wide déjà en
 * place). Ajoutée à la revue finale du jalon 2b -- voir le commentaire de
 * habillage_est_construit() (firmware/main/ui/habillage.h) pour le SEGV muet
 * que cette paire de fonctions transforme en erreur nommée dans
 * host-test/tests/main.c. */
bool source_etat_sim_est_demarre(void);

/* EXPOSÉ POUR LES TESTS UNIQUEMENT (fix round 1, revue tâche 6, M2) : efface
 * le sceau à un seul emplacement de ui_commande_echec() (l'échec ASYNCHRONE
 * le plus récent, voir source_etat.h) SANS le consommer/rendre à l'appelant
 * -- contrairement à ui_commande_echec() lui-même, qui exige un tampon de
 * sortie et fait partie du contrat façade partagé avec la cible. Un test qui
 * PRODUIT un échec asynchrone dont il ne se sert pas (ex. une action inconnue
 * envoyée juste pour prouver le câblage bouton -> ui_commander(), voir
 * test_jouet.c) doit nettoyer APRÈS LUI, ici, plutôt que de laisser un sceau
 * armé survivre à sa propre suite et se faire consommer par erreur par la
 * suite suivante qui appellerait habillage_pomper() (c'était exactement le
 * bug -- un sceau "reset" de test_jouet.c hérité à tort par
 * test_ecran_macros.c). Sans effet si aucun échec n'est en attente. */
void source_etat_sim_reset_echec(void);

/* EXPOSÉ POUR LES TESTS UNIQUEMENT (tâche 4, jalon 3b) : copie dans `action`/
 * `arguments_json` (au moins `taille_action`/`taille_arguments` octets
 * chacun) l'action et les arguments JSON de la commande LA PLUS RÉCEMMENT
 * empilée par ui_commander() -- une simple LECTURE (peek), qui ne dépile
 * RIEN, contrairement à source_etat_sim_cycle() (voir son commentaire de
 * tête). Complète source_etat_sim_file_taille() (tâche 9), qui dit
 * seulement "combien de commandes attendent" jamais "avec quel contenu" :
 * un test qui veut prouver qu'un clic envoie précisément
 * BACKEND_ACTION_GCODE avec un script contenant "G1 X10 F3000" (voir
 * test_ecran_accueil_idle.c, trace du seam pad de jog -> ui_commander())
 * n'avait jusqu'ici aucun moyen d'inspecter CE que la file contient, seulement
 * sa profondeur.
 *
 * `arguments_json`/`taille_arguments` peuvent valoir NULL/0 si l'appelant ne
 * s'intéresse qu'à `action` -- copie alors "" (chaîne vide) si la commande la
 * plus récente n'avait pas d'arguments (voir `avec_arguments` dans
 * commande_t), jamais un pointeur non initialisé. Rend false SANS RIEN
 * TOUCHER si la file est vide, si `action` est NULL, ou si `taille_action`
 * vaut 0. */
bool source_etat_sim_derniere_commande(char *action, size_t taille_action,
                                        char *arguments_json, size_t taille_arguments);

/* EXPOSÉ POUR LES TESTS UNIQUEMENT (tâche 6, jalon 3b) : comme
 * source_etat_sim_derniere_commande() ci-dessus, mais l'AVANT-DERNIÈRE
 * commande poussée -- nécessaire pour prouver qu'un préréglage de
 * température (tâche 6, ecran_accueil_idle.c) pousse VRAIMENT DEUX gcodes
 * dans le MÊME clic (buse puis plateau) : source_etat_sim_cycle() dépile
 * TOUTE la file d'un coup (voir traiter_commandes() dans
 * source_etat_sim.c), impossible donc d'inspecter la première des deux
 * commandes en cyclant entre les deux. Rend false SANS RIEN TOUCHER si
 * moins de deux commandes sont actuellement en file. */
bool source_etat_sim_avant_derniere_commande(char *action, size_t taille_action,
                                              char *arguments_json, size_t taille_arguments);
