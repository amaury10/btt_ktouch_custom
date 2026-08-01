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
 * opaques sur `void *etat`), habillage.c est couplé à l'application Klipper
 * en DEUX sites (tâche 9 : un second est apparu depuis la version initiale de
 * ce commentaire, qui n'en comptait qu'un). Le premier : l'appel à
 * ui_etat_instantane() dans habillage_pomper(), qui doit connaître la taille
 * exacte de l'état du backend (`etat_klipper_t`) pour en obtenir quoi que ce
 * soit — voir le commentaire de source_etat.h. Le second : libelle_commande()
 * dans habillage.c, qui traduit les actions BACKEND_ACTION_* (core/backend.h)
 * en mots anglais pour le bandeau de notification, après un échec de commande
 * asynchrone (voir ui_commande_echec() dans source_etat.h). Un fork avec une
 * autre application (une machine non-Klipper) adapte ce fichier-là aux deux
 * sites, pas ecran.h/navigation.c qui restent réutilisables tels quels. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

#include "ecran.h"
#include "liaison.h"
#include "widgets/rail.h"

/* Enregistre l'écran de réglages que le bouton engrenage de la barre d'état
 * ouvre (visible seulement à la profondeur 1, un accueil au sommet). NULL (par
 * défaut) masque le bouton. L'application le fixe après habillage_construire()
 * -- l'habillage reste ainsi générique, sans connaître ECRAN_CONFIGURATION
 * (l'application Klipper) ni l'écran de config d'un fork. `desc` doit rester
 * valide tant que l'habillage vit (typiquement un descripteur statique). */
void habillage_definir_ecran_reglages(const ecran_desc_t *desc);

/* Enregistre le comportement (couche APPLICATION) des clics du rail
 * persistant, et les identifiants d'écran servant au surlignage. L'habillage
 * reste générique : au clic d'un bouton du rail, il appelle `handler(action,
 * ctx)` (voir apps/klipper/rail_actions.h pour l'implémentation Klipper) --
 * comme habillage_definir_ecran_reglages() injecte l'écran de config sans que
 * l'habillage ne connaisse ECRAN_CONFIGURATION en dur. `handler` NULL : un
 * clic ne fait rien. `id_accueil`/`id_macros` sont comparés à
 * navigation_id_courant() (voir navigation.h) à chaque changement de sommet de
 * pile pour surligner le bon bouton (RAIL_ACCUEIL / RAIL_MACROS, RAIL_NB pour
 * tout autre écran) -- NULL désactive simplement ce mappage-là. `ctx`, les
 * deux ids et `handler` doivent rester valides tant que l'habillage vit. */
void habillage_definir_action_rail(void (*handler)(rail_action_t, void *), void *ctx,
                                   const char *id_accueil, const char *id_macros);

/* Enregistre le CHOOSER (couche APPLICATION) de l'écran de fond : la bascule
 * vivante repos<->impression. À chaque habillage_pomper(), SI un chooser est
 * enregistré, que l'état applicatif est disponible ET que la pile est à
 * profondeur 1 (un accueil seul au sommet, jamais un sous-écran par-dessus),
 * l'habillage appelle `choisir(&etat, ctx)` et, si le descripteur rendu n'est
 * pas déjà le fond courant, remplace le fond via navigation_remplacer_base()
 * (voir navigation.h). `etat` est OPAQUE (`const void *`) : l'habillage reste
 * générique, il ne connaît ni etat_klipper_t ni accueil_impression_actif() --
 * l'application injecte ici le couplage Klipper (choix ECRAN_ACCUEIL /
 * ECRAN_ACCUEIL_HUB), exactement comme habillage_definir_ecran_reglages() et
 * habillage_definir_action_rail() injectent le leur.
 *
 * `choisir` NULL (par défaut) désactive toute bascule : le fond reste celui
 * empilé au démarrage. Un chooser qui rend NULL pour un état donné laisse
 * aussi le fond inchangé pour ce pompage-là (aucune bascule forcée vers un
 * écran manquant). `choisir` et `ctx` doivent rester valides tant que
 * l'habillage vit. */
void habillage_definir_choix_accueil(const ecran_desc_t *(*choisir)(const void *etat, void *ctx),
                                     void *ctx);

/* Le rail persistant du shell (état de fichier, comme la barre d'état) : un
 * accesseur générique, exposé pour le surlignage applicatif et les tests
 * (parcours de `->racine`/`->boutons[]`). Toujours non-NULL ; ses champs ne
 * sont peuplés qu'après habillage_construire() (avant, `->racine` vaut NULL). */
rail_t *habillage_rail(void);

/* Construit la barre d'état (bande 44 px en haut), le bandeau de
 * notifications (masqué, 60 px, superposé en bas) et la zone de contenu
 * (le reste), scindée en [rail 58 px | conteneur de navigation 742 px] sur
 * `ecran_racine`, puis appelle navigation_init() sur ce conteneur (742x436).
 * `ecran_racine` doit rester valide tant que l'habillage est utilisé —
 * typiquement lv_screen_active(), comme pour navigation_init() (voir
 * navigation.h).
 *
 * Un second appel sans avoir détruit l'habillage précédent journalise une
 * alerte et ne fait rien : il n'y a jamais qu'une seule barre d'état, et
 * reconstruire par-dessus l'existant laisserait les anciens widgets fuir. */
void habillage_construire(lv_obj_t *ecran_racine);

/* Vrai si habillage_construire() a déjà réussi (singleton déjà en place),
 * faux sinon. Ajoutée à la revue finale du jalon 2b : `habillage_construire`/
 * `source_etat_sim_demarrer` sont deux singletons process-wide dont
 * host-test/tests/main.c impose l'ordre d'enregistrement des suites (voir son
 * commentaire de tête) — une inversion accidentelle de cet ordre plantait
 * jusqu'ici en SEGV brut, sans le moindre compte de vérifications affiché,
 * quelque part au fond de LVGL (arbre d'objets attendu par une suite
 * postérieure mais jamais construit). Cette fonction permet à une suite
 * dépendante de transformer ce SEGV muet en une erreur nommée dès son entrée
 * (voir suite_commandes() dans host-test/tests/test_commandes.c). */
bool habillage_est_construit(void);

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
