/* Pile de navigation entre écrans (voir ecran.h pour le contrat que remplit
 * chaque écran).
 *
 * Politique délibérément simple, sans cache : empiler construit, dépiler
 * détruit. Un écran qui redevient visible après un dépilement est donc
 * retrouvé tel qu'il l'a laissé dans son contexte (voir ecran_desc_t), mais
 * pas reconstruit — seul l'écran qu'on dépile est reconstruit à un futur
 * empilement. Cette simplicité est un choix : un cache économiserait des
 * reconstructions, mais sur un appareil aux ressources modestes, une pile de
 * profondeur bornée, sans état caché à faire vivre, est plus facile à
 * garantir sans fuite que le serait un cache dont l'éviction devrait être
 * pensée séparément. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "lvgl.h"

#include "ecran.h"

/* Profondeur maximale de la pile. Quatre couvre les parcours prévus par la
 * spécification (accueil -> détail -> clavier modal -> confirmation) avec
 * une marge d'un niveau ; au-delà, navigation_empiler() rend ESP_ERR_NO_MEM
 * plutôt que de faire grossir la pile sans borne — voir navigation.c pour le
 * choix de ne jamais l'allouer dynamiquement. */
#define NAVIGATION_PROFONDEUR_MAX 4

/* Initialise la pile de navigation sur `conteneur`, qui doit rester valide
 * tant que la navigation est utilisée (typiquement l'écran actif LVGL,
 * lv_screen_active()). Rappelable : si la pile contient déjà des écrans,
 * ils sont détruits proprement d'abord (même séquence que
 * navigation_depiler()) avant que le nouveau conteneur ne prenne effet. */
void navigation_init(lv_obj_t *conteneur);

/* Empile `desc` : alloue son contexte (calloc, remis à zéro), crée son
 * conteneur LVGL plein cadre dans le conteneur racine, cache le conteneur de
 * l'écran précédent sans le détruire, puis appelle `desc->construire`.
 *
 * Rend ESP_ERR_INVALID_ARG si `desc` est NULL, ESP_ERR_NO_MEM si la pile est
 * déjà à NAVIGATION_PROFONDEUR_MAX ou si une allocation échoue — dans les
 * deux cas de ESP_ERR_NO_MEM, rien n'est construit ni modifié dans la pile. */
esp_err_t navigation_empiler(const ecran_desc_t *desc);

/* Dépile l'écran courant : appelle `detruire`, puis détruit son conteneur
 * LVGL, puis libère son contexte, dans cet ordre précis (voir navigation.c).
 * Rend à nouveau visible l'écran qui devient le sommet de la pile.
 *
 * Ne fait rien si la pile ne contient qu'un seul écran : cet appareil n'a
 * pas de bouton physique, dépiler jusqu'au vide laisserait un écran noir
 * sans aucun moyen d'y revenir. La pile garde donc toujours au moins un
 * écran une fois qu'elle en contient un. */
void navigation_depiler(void);

/* Dépile jusqu'à ne garder que l'écran du fond de la pile (équivalent à
 * appeler navigation_depiler() en boucle jusqu'à ce que
 * navigation_profondeur() vaille 1 ou 0). */
void navigation_accueil(void);

/* Remplace l'écran du FOND de la pile par `desc`, sans jamais arracher
 * l'utilisateur d'un sous-écran : ramène d'abord la pile à la profondeur 1
 * (même effet que navigation_accueil() : dépile jusqu'au fond en appelant la
 * vraie séquence de destruction pour chaque écran dépilé), puis, si le fond
 * restant n'est pas DÉJÀ `desc` (comparaison sur l'id), détruit ce fond
 * (même ordre exact que navigation_depiler() : detruire + conteneur LVGL +
 * contexte) et construit `desc` à sa place (contexte neuf, conteneur plein
 * cadre, construire). La pile reste à profondeur 1, `desc` au fond.
 *
 * Sert à la bascule vivante repos<->impression de l'écran de fond (voir
 * habillage_definir_choix_accueil(), ui/habillage.h) : le fond suit l'état
 * applicatif, mais UNIQUEMENT quand il est seul (profondeur 1) — l'appelant
 * (habillage_pomper()) ne l'invoque jamais à profondeur > 1, où l'utilisateur
 * est dans un sous-écran qui doit rester intact.
 *
 * Ne fait rien si `desc` est NULL ou si la pile est vide. Ne fait rien non
 * plus, et n'incrémente PAS navigation_sequence() (voir plus bas), si le fond
 * est déjà `desc` alors que la pile est déjà à profondeur 1 : un no-op strict,
 * sans reconstruction ni repeinture superflue. Un remplacement réel, lui,
 * incrémente navigation_sequence() (le sommet visible a changé) pour que
 * l'habillage propage un premier mettre_a_jour au fond fraîchement construit
 * au prochain pompage. */
void navigation_remplacer_base(const ecran_desc_t *desc);

/* Transmet `etat` et `donnees_perimees` au seul écran actuellement visible
 * (le sommet de la pile), via son rappel `mettre_a_jour` — jamais aux écrans
 * couverts en dessous (spécification 5.4). `etat` NULL n'est pas une
 * erreur : c'est le cas de la boucle applicative pas encore démarrée, où il
 * n'y a simplement rien de neuf à montrer ; cet appel ne fait alors rien. Ne
 * fait rien non plus si la pile est vide, ou si l'écran visible laisse
 * `mettre_a_jour` à NULL (voir ecran.h). `donnees_perimees` est transmis tel
 * quel, sans jugement : c'est à l'appelant (habillage_pomper(), voir
 * ui/habillage.c) de le calculer depuis la liaison — cette fonction ne fait
 * que le relayer au sommet de la pile. */
void navigation_mettre_a_jour(const void *etat, bool donnees_perimees);

/* Titre de l'écran au sommet de la pile, destiné à la barre d'état. Rend
 * NULL si la pile est vide. */
const char *navigation_titre_courant(void);

/* Identifiant de l'écran au sommet de la pile. Rend NULL si la pile est
 * vide. */
const char *navigation_id_courant(void);

/* Nombre d'écrans actuellement empilés (0 avant le premier
 * navigation_empiler()). */
size_t navigation_profondeur(void);

/* Compteur monotone, jamais remis à zéro, incrémenté à chaque changement
 * RÉUSSI de l'écran visible au sommet de la pile (navigation_empiler() qui
 * réussit, navigation_depiler() ou navigation_accueil() qui dépilent
 * réellement quelque chose) — jamais sur une tentative refusée ou un
 * dépilement qui ne change rien (garde « dernier écran », voir
 * navigation_depiler()).
 *
 * Signal « la pile a changé » consommé par habillage_pomper() (voir son
 * commentaire dans ui/habillage.c) : un écran qui vient tout juste de
 * devenir le sommet a été construit avec un contexte vide (construire() ne
 * reçoit jamais l'état applicatif, voir ecran.h) et a besoin d'un premier
 * mettre_a_jour au prochain pompage MÊME SI l'état applicatif lui-même n'a
 * pas changé entre-temps (ex. température constante sur une imprimante au
 * repos) — sans ce signal, un écran fraîchement empilé pendant une période
 * calme reste vide jusqu'au prochain changement réel de generation/liaison,
 * qui peut ne jamais survenir. */
uint32_t navigation_sequence(void);
