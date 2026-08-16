/* Implémentation : voir accueil_choix.h pour le contrat. */
#include "accueil_choix.h"

#include <stddef.h> /* NULL */

bool accueil_impression_actif(const etat_klipper_t *etat)
{
    if (etat == NULL) {
        return false;
    }
    /* impression_en_pause n'est jamais lu ici : une impression en pause a
     * TOUJOURS impression_en_cours vrai (c'est la même invariante que
     * ecran_accueil.c suppose déjà pour basculer son bouton Pause/Resume,
     * voir son commentaire de tête) -- le lire séparément n'ajouterait
     * qu'une seconde source de vérité qui pourrait diverger. */
    return etat->impression_en_cours;
}
