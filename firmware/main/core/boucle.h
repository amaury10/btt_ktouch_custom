/* Boucle d'interrogation : la tâche FreeRTOS qui fait tourner un backend.
 *
 * Le socle possède ici la seule tâche qui touche le réseau pour parler à la
 * machine : elle rafraîchit l'état une fois par seconde et dépile les
 * commandes qu'on lui a confiées. Rien d'autre dans le firmware n'appelle un
 * backend directement — c'est ce qui garantit qu'un rappel d'interface (jalon
 * 2b) n'attend jamais la seconde ou deux que peut prendre une requête HTTP :
 * il empile une commande et rend la main tout de suite. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "backend.h"
#include "esp_err.h"
#include "liaison.h"

/* Démarre la boucle : initialise le magasin d'état à la taille du backend,
 * appelle une fois `desc->demarrer`, puis lance la tâche d'interrogation.
 *
 * Ne peut être appelé qu'une fois — un second appel rend ESP_ERR_INVALID_STATE
 * sans toucher à la boucle déjà en cours, plutôt que de créer une seconde
 * tâche qui se disputerait le même magasin d'état. */
esp_err_t boucle_demarrer(const backend_desc_t *desc, const backend_hote_t *hote);

/* Dernier état validé — pointeur BRUT vers le tampon interne du magasin
 * d'état (voir etat_store.h). Il ne reste valable QUE jusqu'au prochain
 * `etat_store_valider()` exécuté par la tâche d'interrogation, c'est-à-dire
 * au plus tard le cycle suivant (~1 s) : passé ce point, il désigne le
 * tampon arrière, que le cycle d'après remet à zéro avant de le remplir. Un
 * appelant qui le garde au travers d'un yield (un rappel LVGL, par exemple)
 * peut relire une structure remise à zéro sans le moindre avertissement.
 *
 * N'appeler cette fonction QUE depuis la tâche d'interrogation elle-même, ou
 * en copiant son résultat avant de rendre la main sous quelque forme que ce
 * soit. Depuis toute autre tâche — en particulier l'interface LVGL du
 * sous-jalon 2b — préférer boucle_etat_copier(), qui ne laisse fuir aucun
 * pointeur vers ce tampon interne.
 *
 * NULL si la boucle n'a pas encore démarré. */
const void *boucle_etat(void);

/* Copie sous mutex le dernier état validé dans `dest`, qui doit faire
 * exactement `taille` octets (la taille du backend démarré, typiquement
 * sizeof(etat_klipper_t)). Contrairement à boucle_etat(), aucun pointeur vers
 * le tampon interne du magasin n'est jamais rendu à l'appelant : `dest` reste
 * valable et exact quel que soit le temps que l'appelant met à s'en servir
 * ensuite, y compris depuis une tâche qui cède la main entre deux instructions
 * (l'interface LVGL du sous-jalon 2b). C'est la façon documentée de lire
 * l'état depuis une tâche autre que celle d'interrogation.
 *
 * Rend false sans toucher `dest` si la boucle n'a pas démarré, si `dest` est
 * NULL, ou si `taille` ne correspond pas à la taille réelle de l'état du
 * backend en cours. */
bool boucle_etat_copier(void *dest, size_t taille);

/* Compteur de générations de l'état, pour qu'un afficheur sache s'il doit
 * redessiner sans comparer lui-même la structure entière. 0 avant tout
 * démarrage. */
uint32_t boucle_generation(void);

/* Santé de la liaison avec l'hôte. LIAISON_CONNEXION avant tout démarrage. */
liaison_etat_t boucle_liaison(void);

/* Lit, en UNE seule prise de mutex, la copie de l'état ET sa génération ET la
 * santé de la liaison — contrairement à enchaîner boucle_etat_copier(),
 * boucle_generation() et boucle_liaison() séparément, ce qui laisse la tâche
 * d'interrogation permuter le magasin d'état ENTRE deux de ces trois appels :
 * un appelant verrait alors une `generation` qui ne correspond pas au
 * contenu qu'il vient de copier (N+1 affiché à côté du contenu de N), et
 * comme rien ne redéclenche une nouvelle lecture avant le prochain
 * changement réel, cet écart peut ne jamais se corriger tout seul —
 * exactement le défaut qu'un compteur de génération est censé éviter. `dest`
 * et `taille` suivent le contrat de boucle_etat_copier() ; `generation` et
 * `liaison` peuvent valoir NULL si l'appelant ne s'y intéresse pas.
 *
 * Rend false sans toucher `dest`/`*generation`/`*liaison` dans les mêmes cas
 * que boucle_etat_copier() (boucle non démarrée, `dest` NULL, ou `taille`
 * incorrecte). Les accesseurs séparés ci-dessus restent disponibles pour un
 * appelant qui n'a besoin que d'une seule de ces trois valeurs. */
bool boucle_instantane(void *dest, size_t taille, uint32_t *generation, liaison_etat_t *liaison);

/* Empile une commande pour la tâche d'interrogation et rend la main
 * immédiatement — la commande n'est JAMAIS exécutée par l'appelant.
 * `arguments_json` peut être NULL.
 *
 * Rend ESP_ERR_INVALID_STATE si la boucle n'a pas démarré, ESP_ERR_INVALID_ARG
 * si `action` est NULL ou trop long pour le tampon interne, et ESP_ERR_NO_MEM
 * si la file (profondeur 4) est pleine — un appelant averti peut alors griser
 * son bouton un instant plutôt que de rester silencieux sur une commande
 * perdue. */
esp_err_t boucle_commander(const char *action, const char *arguments_json);
