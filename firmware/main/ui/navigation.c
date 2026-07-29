/* Implémentation de la pile de navigation : voir navigation.h pour le
 * contrat. */
#include "navigation.h"

#include <stdlib.h>

#include "journal.h"

static const char *TAG = "navigation";

typedef struct {
    const ecran_desc_t *desc;
    void      *contexte;
    lv_obj_t  *racine;
} entree_pile_t;

/* Pile statique : aucun malloc pour la pile elle-même, seuls les contextes
 * d'écran (ci-dessous) viennent du tas. Une pile de taille fixe, bornée à la
 * compilation, ne peut pas fragmenter la mémoire au fil des allers-retours
 * de navigation — contrairement à une liste chaînée qu'on réallouerait à
 * chaque empilement. */
static entree_pile_t pile[NAVIGATION_PROFONDEUR_MAX];
static size_t profondeur;
static lv_obj_t *conteneur_racine;

/* Voir navigation_sequence() (navigation.h) pour le contrat complet. Partie
 * de 0 comme g_derniere_generation dans habillage.c : un premier empilement
 * après un navigation_init() (qui ne touche jamais ce compteur, voir plus
 * bas) le fait passer à 1, ce qui suffit à déclencher la toute première
 * propagation dès que habillage_pomper() a quelque chose à transmettre. */
static uint32_t sequence;

/* Détruit l'écran au sommet de la pile et décrémente `profondeur`. Ne
 * touche PAS à la visibilité de l'écran qui devient le nouveau sommet :
 * c'est aux appelants (navigation_depiler(), navigation_init()) de décider
 * s'il faut le redémasquer, selon qu'ils construisent quelque chose
 * ensuite ou remettent la pile à plat. */
static void detruire_sommet(void)
{
    entree_pile_t *sommet = &pile[profondeur - 1];

    /* Ordre imposé : détruire() d'abord, puis l'objet LVGL, puis le
     * contexte. Supprimer l'objet LVGL en premier ferait tourner les
     * rappels de suppression de ses widgets (event LV_EVENT_DELETE en
     * cascade sur les enfants) alors que `detruire()` n'a pas encore eu la
     * main sur un contexte que ces rappels peuvent lire — et libérer le
     * contexte avant que lv_obj_delete() n'ait fini de parcourir l'arbre
     * produirait une lecture après libération sur exactement ce même
     * contexte. Cet ordre est ce qui protège des deux à la fois. */
    if (sommet->desc->detruire != NULL) {
        sommet->desc->detruire(sommet->contexte);
    }
    lv_obj_delete(sommet->racine);
    free(sommet->contexte);

    sommet->desc = NULL;
    sommet->contexte = NULL;
    sommet->racine = NULL;
    profondeur--;
}

void navigation_init(lv_obj_t *conteneur)
{
    /* Rappelable : une pile déjà peuplée est détruite proprement (même
     * ordre que navigation_depiler()) avant que le nouveau conteneur ne
     * prenne effet, plutôt que de fuir ses contextes ou de continuer à
     * empiler dans un conteneur qui n'est plus le bon. */
    while (profondeur > 0) {
        detruire_sommet();
    }
    conteneur_racine = conteneur;
}

esp_err_t navigation_empiler(const ecran_desc_t *desc)
{
    if (desc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (profondeur >= NAVIGATION_PROFONDEUR_MAX) {
        JOURNAL_ALERTE(TAG, "pile pleine (%d), \"%s\" refuse", NAVIGATION_PROFONDEUR_MAX,
                        desc->id != NULL ? desc->id : "?");
        return ESP_ERR_NO_MEM;
    }
    if (conteneur_racine == NULL) {
        JOURNAL_ERREUR(TAG, "navigation_empiler avant navigation_init");
        return ESP_ERR_INVALID_STATE;
    }

    /* `taille_contexte == 0` doit rendre un contexte NULL, jamais le retour
     * d'un calloc(1, 0) : la norme C laisse ce retour implémentation-
     * dépendante (NULL ou un pointeur non déréférençable mais valide pour
     * free()), et un écran sans état ne doit dépendre d'aucun des deux. */
    void *contexte = NULL;
    if (desc->taille_contexte > 0) {
        contexte = calloc(1, desc->taille_contexte);
        if (contexte == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    lv_obj_t *racine = lv_obj_create(conteneur_racine);
    if (racine == NULL) {
        free(contexte);
        return ESP_ERR_NO_MEM;
    }
    /* Conteneur plein cadre, sans style hérité : c'est à l'écran de décider
     * de son propre habillage (fond, marges) dans `construire`, pas à la
     * navigation de lui en imposer un par défaut. */
    lv_obj_remove_style_all(racine);
    lv_obj_set_size(racine, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(racine, 0, 0);

    if (profondeur > 0) {
        /* Cache l'écran précédent SANS le détruire : la navigation ne met
         * jamais en cache que l'écran visible reçoit des mises à jour
         * (spécification 5.4), mais un écran couvert reste vivant, prêt à
         * réapparaître tel quel au prochain dépilement. */
        lv_obj_add_flag(pile[profondeur - 1].racine, LV_OBJ_FLAG_HIDDEN);
    }

    if (desc->construire != NULL) {
        desc->construire(racine, contexte);
    }

    pile[profondeur].desc = desc;
    pile[profondeur].contexte = contexte;
    pile[profondeur].racine = racine;
    profondeur++;

    /* Le sommet visible vient de changer (le nouvel écran, jamais celui
     * caché juste au-dessus) : voir navigation_sequence(). */
    sequence++;

    return ESP_OK;
}

void navigation_depiler(void)
{
    if (profondeur <= 1) {
        /* Piège volontairement bloqué ici plutôt que laissé à la charge de
         * chaque appelant : cet appareil n'a pas de bouton physique, un
         * dépilement jusqu'au vide laisserait un écran noir sans aucun
         * moyen d'y revenir. */
        return;
    }

    detruire_sommet();

    /* Redémasque l'écran qui redevient le sommet : c'est le pendant du
     * masquage fait par navigation_empiler(), et sans lui l'écran du dessous
     * resterait invisible pour toujours après ce dépilement. */
    lv_obj_clear_flag(pile[profondeur - 1].racine, LV_OBJ_FLAG_HIDDEN);

    /* Le sommet visible vient de changer (l'écran redémasqué ci-dessus,
     * qui n'a reçu aucune mise à jour depuis qu'il a été couvert -- voir
     * navigation_mettre_a_jour(), spécification 5.4) : voir
     * navigation_sequence(). Jamais atteint quand la garde ci-dessus a
     * refusé le dépilement (rien n'a changé). */
    sequence++;
}

void navigation_accueil(void)
{
    /* detruire_sommet() directement, pas navigation_depiler() en boucle :
     * navigation_depiler() redémasque l'écran qui devient sommet à CHAQUE
     * itération, y compris les écrans intermédiaires qu'une itération
     * suivante va immédiatement redétruire — travail inutile, sans effet
     * visible puisqu'un écran cache-puis-détruit dans le même passage
     * n'est jamais rendu, mais du travail quand même. Même schéma que
     * navigation_init() : détruire d'abord tout ce qui doit disparaître,
     * ne redémasquer qu'une seule fois à la fin. */
    bool sommet_a_change = profondeur > 1;
    while (profondeur > 1) {
        detruire_sommet();
    }
    if (profondeur > 0) {
        lv_obj_clear_flag(pile[profondeur - 1].racine, LV_OBJ_FLAG_HIDDEN);
    }

    /* Une seule incrémentation même si plusieurs écrans intermédiaires ont
     * été détruits ci-dessus : un seul changement de sommet visible a eu
     * lieu (du sommet d'origine directement au fond) -- voir
     * navigation_sequence(). Rien à faire si la pile ne contenait déjà
     * qu'un seul écran (ou aucun) : le sommet n'a pas bougé. */
    if (sommet_a_change) {
        sequence++;
    }
}

void navigation_mettre_a_jour(const void *etat, bool donnees_perimees)
{
    if (etat == NULL || profondeur == 0) {
        return;
    }

    entree_pile_t *sommet = &pile[profondeur - 1];
    if (sommet->desc->mettre_a_jour != NULL) {
        sommet->desc->mettre_a_jour(etat, donnees_perimees, sommet->contexte);
    }
}

const char *navigation_titre_courant(void)
{
    return profondeur == 0 ? NULL : pile[profondeur - 1].desc->titre;
}

const char *navigation_id_courant(void)
{
    return profondeur == 0 ? NULL : pile[profondeur - 1].desc->id;
}

size_t navigation_profondeur(void)
{
    return profondeur;
}

uint32_t navigation_sequence(void)
{
    return sequence;
}
