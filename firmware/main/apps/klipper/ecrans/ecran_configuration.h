/* Écran de première configuration (tâche 8, sous-jalon 2b) : le premier
 * écran qu'un appareil jamais configuré affiche (voir app_main.c et
 * reglages_configures()) — adresse de l'imprimante et type de machine.
 *
 * Cette tâche solde une dette du jalon 2a : reglages_definir_hote() n'avait
 * jusqu'ici aucun appelant (revue finale, Critical C2), un repli Kconfig
 * comblant provisoirement le trou. Le bouton Save de cet écran est ce
 * premier appelant réel — via ui/source_reglages.h, jamais reglages.h
 * directement (voir ce fichier pour pourquoi).
 *
 * `ecran_configuration_ctx_t` est exposé ici plutôt qu'opaque, exactement
 * pour la même raison que ecran_accueil_ctx_t (voir son en-tête) :
 * host-test/tests/test_ecran_configuration.c a besoin de relire/écrire ses
 * champs directement (lv_label_get_text(), forcer une saisie sans passer par
 * le clavier tactile) sans qu'aucun accesseur ne soit ajouté pour la seule
 * occasion des tests. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "backend.h"
#include "clavier.h"
#include "ecran.h"
#include "lvgl.h"

typedef struct {
    lv_obj_t *valeur_label;        /* adresse courante affichee, ou placeholder */
    lv_obj_t *bouton_modifier;     /* ouvre le clavier sur "Printer address" */
    lv_obj_t *dropdown_type;       /* selecteur de type de machine, une seule entree */
    lv_obj_t *bouton_enregistrer;  /* valide, enregistre, notifie, revient a l'accueil */
    /* Derniere valeur connue du champ adresse : pre-remplie depuis
     * ui_reglages_hote() a la construction, mise a jour par le rappel du
     * clavier a chaque validation, relue par le bouton Save. Seule source de
     * verite de ce que Save enregistre -- jamais relue depuis valeur_label
     * (qui n'est qu'un affichage). CLAVIER_VALEUR_MAX (clavier.h) : la valeur
     * rendue par clavier_ouvrir() est deja bornee a cette taille, ce tampon
     * n'a donc jamais besoin d'etre plus grand. */
    char saisie[CLAVIER_VALEUR_MAX];
} ecran_configuration_ctx_t;

extern const ecran_desc_t ECRAN_CONFIGURATION;

/* Validation pure de la saisie du champ adresse -- testable sur PC sans LVGL
 * ni NVS, voir ecran_configuration.c pour la règle exacte (dérivée de
 * hote_parse(), core/hote_parse.h, jamais un second analyseur).
 *
 * `saisie` NULL ou vide : refusé. Un `saisie` sans ':' est traité comme une
 * adresse seule, port par défaut appliqué (contrairement à hote_parse() tout
 * seul, qui rejette l'absence totale de ':' -- voir son commentaire de tête ;
 * c'est le cas normal d'un utilisateur qui tape juste une IP). Sinon, délégué
 * intégralement à hote_parse() : accepté/refusé et adresse/port obtenus sont
 * exactement les siens (adresse vide malgré un ':', adresse trop longue pour
 * BACKEND_HOTE_LONGUEUR_MAX, port non numérique retombant sur le port par
 * défaut plutôt que de refuser, etc. -- voir host-test/tests/test_hote_parse.c
 * pour le détail exhaustif de ces cas).
 *
 * Rend vrai et écrit `*hote_sortie` (si non NULL) si la saisie décrit un hôte
 * exploitable. Rend faux et écrit un message dans `erreur` (si `erreur` non
 * NULL et `taille_erreur` non nulle, toujours NUL-terminé) sinon --
 * `hote_sortie` n'est alors jamais touché. */
bool ecran_configuration_valider(const char *saisie, backend_hote_t *hote_sortie,
                                  char *erreur, size_t taille_erreur);
