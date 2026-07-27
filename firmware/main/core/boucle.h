/* Boucle d'interrogation : la tâche FreeRTOS qui fait tourner un backend.
 *
 * Le socle possède ici la seule tâche qui touche le réseau pour parler à la
 * machine : elle rafraîchit l'état une fois par seconde et dépile les
 * commandes qu'on lui a confiées. Rien d'autre dans le firmware n'appelle un
 * backend directement — c'est ce qui garantit qu'un rappel d'interface (jalon
 * 2b) n'attend jamais la seconde ou deux que peut prendre une requête HTTP :
 * il empile une commande et rend la main tout de suite. */
#pragma once

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

/* Dernier état validé, à lire depuis n'importe quelle tâche (LVGL comprise) :
 * voir etat_store.h pour la garantie de double tampon. NULL si la boucle n'a
 * pas encore démarré. */
const void *boucle_etat(void);

/* Compteur de générations de l'état, pour qu'un afficheur sache s'il doit
 * redessiner sans comparer lui-même la structure entière. 0 avant tout
 * démarrage. */
uint32_t boucle_generation(void);

/* Santé de la liaison avec l'hôte. LIAISON_CONNEXION avant tout démarrage. */
liaison_etat_t boucle_liaison(void);

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
