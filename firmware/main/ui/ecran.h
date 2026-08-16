/* Contrat que remplit un écran pour s'accrocher à la pile de navigation.
 *
 * Un écran ne connaît ni le backend ni le réseau (voir la contrainte globale
 * du jalon : jamais d'appel HTTP, jamais de blocage, jamais de vTaskDelay
 * dans un rappel LVGL) : il reçoit un état déjà mis en forme via
 * `mettre_a_jour`, construit ses widgets dans `construire`, et rend son
 * contexte dans `detruire`. C'est le point d'extension de tout ce projet et
 * de ses forks : un nouvel écran se résume à remplir cette structure, sans
 * toucher une ligne du socle de navigation.
 *
 * Écart assumé par rapport à la spécification (section 4.2) : le champ
 * `taille_contexte` n'y figure pas, chaque écran étant censé gérer son
 * contexte lui-même. Il est ajouté ici par symétrie exacte avec
 * `backend_desc_t.taille_etat` (voir core/backend.h) et pour la même raison
 * énoncée en section 4.1 de la spécification : sur un appareil sans port
 * série, une fuite dans un chemin appelé en boucle se manifeste par un
 * redémarrage plusieurs heures plus tard, quand personne n'est devant. La
 * navigation détruit et reconstruit un écran à chaque aller-retour dans la
 * pile ; c'est exactement un chemin appelé en boucle. Confier l'allocation
 * au socle (voir navigation.c) plutôt qu'à chaque écran élimine cette classe
 * de fuite à la source, au prix d'une indirection (`void *contexte`) que
 * chaque écran doit re-caster vers son propre type. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "lvgl.h"

typedef struct {
    const char *id;    /* identifiant stable de l'écran, ex. "accueil" */
    const char *titre;              /* affiché dans la barre d'état */
    size_t      taille_contexte;    /* le socle alloue ; l'écran n'alloue jamais */

    /* `parent` est le conteneur plein cadre créé par la navigation pour cet
     * écran (voir navigation_empiler()) ; `contexte` pointe vers un tampon
     * de `taille_contexte` octets remis à zéro par le socle juste avant cet
     * appel (calloc), ou vaut NULL si `taille_contexte` vaut 0. Un écran qui
     * a besoin de mémoriser un état entre deux appels (un widget créé dans
     * `construire` et relu dans `mettre_a_jour`, par exemple) le range dans
     * ce contexte — jamais dans une variable statique du fichier, qui
     * survivrait à une destruction et confondrait deux instances du même
     * écran empilées à des profondeurs différentes. Un écran purement
     * statique, sans widget à créer dynamiquement, peut laisser ce pointeur
     * à NULL (navigation.c le vérifie avant d'appeler). */
    void (*construire)(lv_obj_t *parent, void *contexte);

    /* Appelé uniquement quand cet écran est au sommet de la pile (voir
     * navigation_mettre_a_jour()) : un écran couvert par un autre ne reçoit
     * aucune mise à jour tant qu'il n'est pas redevenu visible. `etat`
     * pointe vers une structure dont la forme dépend de l'application (voir
     * apps/klipper) ; un écran générique la traite comme opaque. Un écran
     * qui n'affiche rien de dynamique (un menu de boutons fixes, par
     * exemple) peut laisser ce pointeur à NULL.
     *
     * `donnees_perimees` est calculé par l'habillage à partir du seul état
     * de la liaison (`habillage_donnees_perimees()`, voir ui/habillage.h) —
     * jamais du contenu de `etat` lui-même. Vrai signifie « grise tes
     * valeurs, ce que tu affiches ne reflète plus la machine en ce moment »,
     * RIEN DE PLUS : ce n'est pas un signal d'erreur, et un écran ne doit
     * jamais le traduire par sa propre boîte d'erreur réseau — la
     * spécification §5.3 réserve cet affichage à l'habillage seul (pastille
     * + texte de la barre d'état). Un écran qui ignore ce champ (le
     * transmet à `false` en l'oubliant, par exemple) republierait des
     * zéros en blanc franc comme s'il s'agissait d'une mesure réelle —
     * exactement le défaut C3 du jalon 2a que ce champ existe pour empêcher
     * structurellement, plutôt que de compter sur chaque écran pour relire
     * la liaison lui-même via ui_etat_instantane(). */
    void (*mettre_a_jour)(const void *etat, bool donnees_perimees, void *contexte);

    /* Appelé par navigation_depiler() avant que le socle ne détruise le
     * conteneur LVGL de cet écran et ne libère son contexte (voir
     * navigation.c pour l'ordre exact et pourquoi il est fixe). Un écran qui
     * n'a rien à faire à la destruction peut laisser ce pointeur à NULL. */
    void (*detruire)(void *contexte);
} ecran_desc_t;
