/* Dialogue de confirmation modal : deuxième moitié du plus gros morceau
 * partagé du jalon (§6 de la spécification, voir clavier.h) — habille
 * `lv_msgbox` (fourni par LVGL) pour rendre un booléen plutôt qu'un simple
 * texte affiché. Utilisé partout où une action ne doit pas partir d'un
 * simple effleurement accidentel (annuler une impression, effacer un
 * réglage) : voir `destructif` ci-dessous.
 *
 * Même politique de singleton que clavier.h, et pour la même raison : un
 * état statique de fichier dans confirmation.c, jamais alloué par le widget
 * lui-même, et un second confirmation_ouvrir() alors qu'un dialogue est déjà
 * ouvert est refusé plutôt qu'empilé. Le brief ne l'exige explicitement que
 * pour clavier_ouvrir(), mais le même défaut s'applique ici à l'identique :
 * avec un état statique unique, un second appel non refusé écraserait le
 * rappel du premier dialogue avant qu'il n'ait jamais été invoqué — zéro
 * appel pour l'utilisateur qui attend une réponse à sa première
 * confirmation, exactement la classe de défaut que la propriété 1 du brief
 * de clavier.h interdit. Voir confirmation.c pour le détail. */
#pragma once

#include <stdbool.h>

/* Rappel appelé exactement une fois par confirmation_ouvrir() : `confirme`
 * vaut true si l'utilisateur a activé le bouton d'action, false s'il a
 * annulé. Comme pour clavier_rappel_t, le dialogue s'est déjà refermé (ses
 * objets LVGL programmés à la destruction) au moment de cet appel. */
typedef void (*confirmation_rappel_t)(bool confirme, void *contexte);

/* Ouvre un dialogue de confirmation centré, avec `titre`, `message` et un
 * bouton d'action libellé `libelle_action` à côté d'un bouton d'annulation
 * fixe. NULL pour `titre`/`message`/`libelle_action` est traité comme
 * chaîne vide (même politique que le reste de ui/).
 *
 * `destructif` vrai colore le bouton d'action en rouge et lui refuse tout
 * état "par défaut" (pas de mise en avant visuelle/de focus) — une action
 * qui efface ou annule quelque chose ne doit jamais être celle qu'un
 * effleurement rapide ou une navigation au clavier physique activerait sans
 * intention.
 *
 * `rappel` NULL est refusé (journalisé), comme clavier_ouvrir(). Un second
 * appel alors qu'un dialogue est déjà ouvert est également refusé et
 * journalisé (voir le commentaire de tête ci-dessus). */
void confirmation_ouvrir(const char *titre, const char *message,
                          const char *libelle_action, bool destructif,
                          confirmation_rappel_t rappel, void *contexte);

/* Fermeture : les deux boutons de pied sont les SEULES sorties. Un
 * effleurement du fond (hors de la boîte) et une touche ECHAP (clavier
 * physique/encodeur) ne ferment PAS le dialogue — délibérément :
 * confirmation.c n'ajoute ni bouton de fermeture d'en-tête ni gestionnaire
 * sur le fond (voir lv_msgbox_add_close_button(), jamais appelée ici). Un
 * dialogue qui peut porter une action destructive ne doit avoir aucune
 * sortie silencieuse : un tap accidentel à côté de la boîte ou une touche
 * échappement ne doivent jamais se comporter comme un `confirme=false`
 * implicite que l'utilisateur n'a pas réellement choisi. Un futur
 * contributeur qui ajouterait un tel raccourci doit appeler
 * `confirmation_rappel_t` avec `confirme=false` explicitement, jamais
 * laisser le dialogue disparaître sans invoquer le rappel (ce qui
 * romprait la propriété « appelé exactement une fois » du haut de ce
 * fichier). */
