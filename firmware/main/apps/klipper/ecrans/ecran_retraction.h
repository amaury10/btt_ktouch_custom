/* Écran Retraction (sous-projet "panneaux KlipperScreen", tâche 6) : quatre
 * réglages de rétraction firmware que Klipper expose sur l'objet
 * `firmware_retraction` (retract_length, retract_speed,
 * unretract_extra_length, unretract_speed) -- KlipperScreen les regroupe sur
 * un seul panneau "Retraction", même raisonnement que Limits (tâche 5) pour
 * les limites globales de vitesse/accélération.
 *
 * Layout (voir retraction.png du brief) : QUATRE lignes label + valeur relue
 * + boutons `-`/`+`, dans l'ordre Retract Length / Retract Speed / Unretract
 * Extra / Unretract Speed -- le MÊME ordre que l'énum `klipper_retr_champ_t`
 * (klipper_gcode.h), réutilisée directement comme index de ligne (voir
 * PAS[]/DOMAINE[]/ecran_retraction_ctx_t ci-dessous, pas de table de
 * correspondance séparée). Pas de sélecteur de pas : chaque ligne a un pas
 * FIXE, imposé par le brief --
 *   Retract Length    ±0.1 mm  (±100 µm)
 *   Retract Speed      ±5  mm/s
 *   Unretract Extra   ±0.1 mm  (±100 µm)
 *   Unretract Speed    ±5  mm/s
 *
 * DEUX DOMAINES D'UNITÉ, pas un seul comme Limits : Length/Extra sont des
 * LONGUEURS (µm en interne, klipper_gcode_retraction_longueur()) tandis que
 * Speed/Unretract Speed sont des VITESSES entières (mm/s,
 * klipper_gcode_retraction_vitesse()) -- Task 1 a scindé l'API en deux
 * fonctions justement pour qu'aucune des deux ne mélange les unités (voir
 * klipper_gcode.h). Ce panneau respecte ce découpage : DOMAINE[] (dans le
 * .c) indique, par ligne, laquelle des deux fonctions appeler et comment
 * interpréter `valeurs_connues[]` (mm flottant dans les deux cas -- c'est la
 * CONVERSION vers l'unité de la fonction gcode qui diffère, voir
 * bouton_cb()).
 *
 * Écart assumé vis-à-vis de KlipperScreen (documenté explicitement par le
 * brief, pas un oubli) : AUCUN bouton "Reset" dans cette v1, exactement pour
 * la même raison que ecran_limites.h (pas de canal pour lire
 * printer.configfile.settings) -- voir son commentaire complet, il
 * s'applique mot pour mot ici.
 *
 * Grisage (C3) et "pas encore reçu" : même choix délibéré que
 * ecran_limites.c -- donnees_perimees ET valeur à 0 partagent la même
 * couleur grise et le même affichage "-". Ici, "valeur à 0" recouvre en plus
 * un cas FRÉQUENT et légitime : une machine dont le printer.cfg n'a PAS de
 * section [firmware_retraction] -- Klipper renvoie alors `{}` pour cet objet
 * (voir moonraker_rpc.h) et les quatre champs restent à 0 pour toujours, pas
 * seulement le temps d'un premier chargement. Afficher "-" grisé pour ce cas
 * est donc le comportement NORMAL et attendu sur beaucoup de machines, pas
 * une anomalie transitoire -- documenté ici pour que ce ne soit pas pris
 * pour un bug lors d'une validation matérielle sur une machine sans
 * [firmware_retraction]. Contrepartie assumée : `retract_length=0` est une
 * valeur de config VALIDE chez Klipper (désactive la rétraction) et
 * s'affichera aussi "-" plutôt que "0.00 mm" -- même arbitrage que
 * ecran_limites.c, qui n'a pas de canal pour distinguer "objet absent" de
 * "valeur explicitement nulle" sans lire configfile.settings (hors scope de
 * ce jalon, voir plus haut).
 *
 * `ecran_retraction_ctx_t` est exposé ici plutôt qu'opaque, même raison que
 * ecran_limites_ctx_t (voir son .h) : host-test/tests/test_ecran_retraction.c
 * relit les libellés/couleurs directement, sans jamais regarder un pixel. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ecran.h"
#include "klipper_gcode.h"
#include "lvgl.h"

/* Quatre lignes, ORDRE FIXE -- identique à klipper_retr_champ_t
 * (klipper_gcode.h), réutilisée directement comme index de ligne. */
#define ECRAN_RETRACTION_LIGNE_NB 4

/* Huit boutons +/- (deux par ligne), ORDRE FIXE -- réutilisé par la boucle de
 * construction (BOUTON_DEFS dans le .c) et par
 * host-test/tests/test_ecran_retraction.c, même convention que
 * ECRAN_LIMITES_BOUTON_* (ecran_limites.h). */
#define ECRAN_RETRACTION_BOUTON_LENGTH_NEG          0
#define ECRAN_RETRACTION_BOUTON_LENGTH_POS          1
#define ECRAN_RETRACTION_BOUTON_SPEED_NEG           2
#define ECRAN_RETRACTION_BOUTON_SPEED_POS           3
#define ECRAN_RETRACTION_BOUTON_EXTRA_NEG           4
#define ECRAN_RETRACTION_BOUTON_EXTRA_POS           5
#define ECRAN_RETRACTION_BOUTON_UNRETRACT_SPEED_NEG 6
#define ECRAN_RETRACTION_BOUTON_UNRETRACT_SPEED_POS 7
#define ECRAN_RETRACTION_BOUTON_NB                  8

/* user_data d'un rappel de clic +/- : le contexte de l'écran (pour relire la
 * dernière valeur connue de CE champ, AU MOMENT DU CLIC -- jamais depuis
 * l'état backend directement, même discipline que
 * ecran_limites_bouton_info_t) et le champ + signe que ce bouton précis
 * représente. */
typedef struct {
    struct ecran_retraction_ctx_s *ctx;   /* jamais NULL une fois construire() passé */
    klipper_retr_champ_t           champ;
    int8_t                          signe; /* +1 ou -1 */
} ecran_retraction_bouton_info_t;

typedef struct ecran_retraction_ctx_s {
    /* Valeurs relues, une par ligne, INDEXÉES par klipper_retr_champ_t (donc
     * dans l'ordre du panneau : Length, Speed, Extra, Unretract Speed). */
    lv_obj_t *valeurs[ECRAN_RETRACTION_LIGNE_NB];

    /* Huit boutons +/- : voir ECRAN_RETRACTION_BOUTON_* plus haut pour
     * l'indexation. `bouton_infos[i].ctx` pointe toujours vers ce contexte
     * -- posé une fois par construire(), jamais recalculé. */
    lv_obj_t                        *boutons[ECRAN_RETRACTION_BOUTON_NB];
    ecran_retraction_bouton_info_t   bouton_infos[ECRAN_RETRACTION_BOUTON_NB];

    /* Dernières valeurs vues par mettre_a_jour(), INDEXÉES par
     * klipper_retr_champ_t, en MILLIMÈTRES (ou mm/s pour les lignes vitesse)
     * -- même unité que etat_klipper_t.retr_*, relues par bouton_cb() AU
     * MOMENT DU CLIC (jamais depuis l'état backend directement, même
     * discipline que ecran_limites_ctx_t.valeurs_connues). Une valeur à 0.0f
     * signifie "pas encore reçue" (ou objet firmware_retraction absent, voir
     * le commentaire de tête ci-dessus) : bouton_cb() ne la traite pas
     * différemment, elle sert simplement de base à `± pas`. */
    float valeurs_connues[ECRAN_RETRACTION_LIGNE_NB];
} ecran_retraction_ctx_t;

extern const ecran_desc_t ECRAN_RETRACTION;
