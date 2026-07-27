/* Barre de progression : le second widget partagé, pour l'avancement d'une
 * impression (voir la mise en page de l'écran d'accueil, tâche 6— « barre
 * de progression pleine largeur avec le pourcentage à un décimale au
 * centre »).
 *
 * Même politique que tuile.h : `progression_t` vit dans le contexte de
 * l'écran (jamais malloc ici), l'écran lit `racine` pour la
 * positionner/dimensionner et passe toujours par les fonctions ci-dessous
 * pour la faire évoluer. Contrairement à la tuile, une barre de progression
 * n'a qu'UNE seule grandeur (la fraction accomplie) — d'où un seul
 * `progression_definir`, pas de paire valeur/consigne. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lvgl.h"

typedef struct {
    lv_obj_t *racine;    /* conteneur ; l'écran le positionne/dimensionne */
    lv_obj_t *barre;     /* lv_bar interne */
    lv_obj_t *etiquette; /* pourcentage à un décimale, centré sur la barre */
} progression_t;

/* Crée les trois objets LVGL et les range dans `p` (déjà alloué par
 * l'appelant). `racine` sort avec une taille explicite (700x36 — voir le
 * commentaire de RACINE_LARGEUR_DEFAUT/RACINE_HAUTEUR_DEFAUT dans
 * progression.c pour la justification des deux chiffres), PAS
 * LV_SIZE_CONTENT : `barre`, son enfant direct, est en LV_PCT(100), et
 * LVGL 9.2 résout un enfant en pourcentage dans un parent en
 * LV_SIZE_CONTENT en clouant l'enfant à zéro plutôt qu'en formant une
 * dépendance circulaire (revue tâche 5, fix round 1 : la barre était
 * invisible tant que l'écran appelant ne redimensionnait pas `racine`
 * avant la première passe de mise en page — un piège d'autant plus
 * probable que `tuile_creer()`, juste à côté, utilise LV_SIZE_CONTENT avec
 * succès). L'écran appelant garde le droit de lv_obj_set_size(p.racine,
 * ...) ensuite pour adapter la largeur à sa mise en page réelle (voir
 * simulateur/main.c) ; ce n'est plus nécessaire pour que la barre soit
 * visible, seulement pour l'ajuster. `p` ou `parent` NULL : ne fait rien. */
void progression_creer(progression_t *p, lv_obj_t *parent);

/* Fait avancer la barre et le texte du pourcentage à `fraction` (0.0 = vide,
 * 1.0 = pleine). Une fraction NaN, infinie ou hors [0, 1] est bornée avant
 * toute conversion en entier — c'est exactement la classe de défaut déjà
 * rencontrée dans ce jalon (conversion float -> entier d'une valeur
 * infinie, voir le commentaire de host-test/CMakeLists.txt sur
 * float-cast-overflow), et `etat_klipper_t.progression` vient de Moonraker,
 * une source qui peut renvoyer n'importe quoi pendant un redémarrage de
 * klippy. */
void progression_definir(progression_t *p, float fraction);

/* Grise ou dégrise la barre et son étiquette (même politique que
 * tuile_griser() : recoloration systématique à chaque appel, réversible). */
void progression_griser(progression_t *p, bool grise);

/* Pur, sans lien avec `progression_t` : écrit "1h 23m" dans `sortie`, ou
 * "--" si `secondes` vaut 0 (Klipper encode ainsi un temps restant
 * inconnu — voir le commentaire de KLIPPER_TEMPS_RESTANT_MAX_S dans
 * core/etat_klipper.h). La borne haute de ce champ (359999 s) rend "99h
 * 59m" sans cas particulier. `sortie` NULL ou `taille` nulle : ne fait
 * rien. Tronque proprement (toujours NUL-terminé) si `taille` est trop
 * petite pour le résultat complet. */
void ui_format_duree(char *sortie, size_t taille, uint32_t secondes);
