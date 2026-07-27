/* Habillage : barre d'état, bandeau de notifications, et la politique
 * d'affichage de la liaison qui les gouverne toutes les deux.
 *
 * Spécification §5.3 : la barre d'état est le SEUL endroit où l'état de la
 * liaison est jamais montré. Un écran ne montre jamais de boîte d'erreur
 * réseau ; quand les données sont périmées, il les grise, et l'habillage
 * explique pourquoi dans son coin. Sans cette règle, chaque écran inventerait
 * sa propre façon de dire « je n'ai pas de nouvelles ». Les trois fonctions
 * pures ci-dessous encodent cette politique une seule fois, pour qu'elle
 * reste testable sans jamais regarder un pixel.
 *
 * Contrairement au reste de ui/ (ecran.h, navigation.h : entièrement
 * opaques sur `void *etat`), habillage.c est couplé à `etat_klipper_t` en
 * UN seul site (l'appel à ui_etat_instantane() dans habillage_pomper(), qui
 * doit connaître la taille exacte de l'état du backend pour en obtenir quoi
 * que ce soit — voir le commentaire de source_etat.h). Un fork avec une
 * autre application (une machine non-Klipper) adapte ce fichier-là, pas
 * ecran.h/navigation.c qui restent réutilisables tels quels. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#include "liaison.h"

/* Construit la barre d'état (bande 44 px en haut), le bandeau de
 * notifications (masqué, 60 px, superposé en bas) et la zone de contenu
 * (le reste, 800x436) sur `ecran_racine`, puis appelle navigation_init() sur
 * cette zone de contenu. `ecran_racine` doit rester valide tant que
 * l'habillage est utilisé — typiquement lv_screen_active(), comme pour
 * navigation_init() (voir navigation.h).
 *
 * Un second appel sans avoir détruit l'habillage précédent journalise une
 * alerte et ne fait rien : il n'y a jamais qu'une seule barre d'état, et
 * reconstruire par-dessus l'existant laisserait les anciens widgets fuir. */
void habillage_construire(lv_obj_t *ecran_racine);

/* Rafraîchit la barre d'état (titre, bouton retour, liaison, wifi, batterie,
 * heure) à chaque appel, puis lit ui_etat_instantane() (voir source_etat.h)
 * et ne transmet le résultat à navigation_mettre_a_jour() QUE si la
 * génération de l'état ou l'état de la liaison a changé depuis le dernier
 * appel — le grisage d'un écran dépend de la liaison, pas du contenu, donc
 * un changement de liaison à contenu identique doit tout de même redessiner
 * l'écran visible. Ne fait rien si habillage_construire() n'a pas été
 * appelé. */
void habillage_pomper(void);

/* Affiche `texte` dans le bandeau de notifications (superposé en bas de
 * l'écran) ; `erreur` choisit sa teinte (alerte ou information). Le bandeau
 * disparaît seul 4 secondes plus tard. Un second appel pendant qu'un bandeau
 * est déjà visible REMPLACE son texte et réarme entièrement le délai de 4
 * secondes — les notifications ne s'empilent jamais.
 *
 * Ne fait rien (journalise une alerte) si habillage_construire() n'a pas été
 * appelé. `texte` NULL est traité comme une chaîne vide plutôt que de
 * déréférencer un pointeur NULL dans LVGL. */
void habillage_notifier(const char *texte, bool erreur);

/* Pur : quel texte pour un état de liaison donné. Ne rend jamais NULL, y
 * compris pour une valeur hors de l'énumération liaison_etat_t —
 * lv_label_set_text(NULL) déréférence et fait tomber l'interface, un état
 * inconnu doit donc rester affichable plutôt que fatal. */
const char *habillage_texte_liaison(liaison_etat_t etat);

/* Pur : quelle couleur (0xRRGGBB) pour un état de liaison donné — la pastille
 * de la barre d'état. */
uint32_t habillage_couleur_liaison(liaison_etat_t etat);

/* Pur : les données affichées doivent-elles être grisées ? Vrai dès qu'on
 * n'est pas EN_LIGNE, y compris pendant la connexion initiale : afficher des
 * zéros en blanc franc pendant les premières secondes se lirait comme une
 * mesure réelle plutôt que comme l'absence de toute mesure. */
bool habillage_donnees_perimees(liaison_etat_t etat);
