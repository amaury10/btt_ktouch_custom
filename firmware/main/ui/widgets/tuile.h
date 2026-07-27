/* Tuile de valeur : le widget partagé par toutes les mesures affichées en
 * grand (température de buse, de plateau, etc. — voir la mise en page de
 * l'écran d'accueil, tâche 6). Trois lignes empilées : un libellé fixe posé
 * une fois à la création (« Nozzle »), une valeur courante en gros (le champ
 * qui bouge), et une consigne en plus petit juste dessous.
 *
 * `tuile_t` est opaque du point de vue de l'écran au sens où celui-ci
 * n'écrit jamais directement dans ses champs — il les LIT (`racine` pour le
 * positionner/dimensionner, comme g_barre dans habillage.c) et passe
 * toujours par les fonctions ci-dessous pour les modifier. La structure vit
 * dans le contexte de l'écran (calloc par navigation.c, voir ecran.h),
 * jamais par malloc ici : ce fichier ne fait jamais qu'y ranger des
 * lv_obj_t* déjà créés.
 *
 * Ce widget ne connaît aucune unité, aucun format : les valeurs qu'on lui
 * passe sont déjà du texte prêt à afficher (voir ui_format_temperature
 * ci-dessous, utilisé par l'écran appelant). Il n'y a donc rien de
 * spécifique à Klipper ici — un futur fork non-Klipper le réutilise tel
 * quel. */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "lvgl.h"

typedef struct {
    lv_obj_t *racine;    /* conteneur ; l'écran le positionne/dimensionne */
    lv_obj_t *libelle;   /* nom fixe, ex. "Nozzle" */
    lv_obj_t *valeur;    /* valeur courante, gros caractères */
    lv_obj_t *consigne;  /* consigne/second texte, caractères plus petits */
} tuile_t;

/* Crée les quatre objets LVGL et les range dans `t` (déjà alloué par
 * l'appelant, voir le commentaire de tête). `libelle` est posé une fois ici
 * (NULL traité comme chaîne vide) ; `valeur`/`consigne` démarrent vides,
 * à remplir par les fonctions ci-dessous. `t` ou `parent` NULL : ne fait
 * rien plutôt que déréférencer. */
void tuile_creer(tuile_t *t, lv_obj_t *parent, const char *libelle);

/* Remplace le texte de la valeur courante. `texte` NULL est traité comme
 * une chaîne vide (même politique que habillage_notifier(), voir
 * habillage.h) plutôt que de déréférencer un pointeur NULL dans LVGL. */
void tuile_definir_valeur(tuile_t *t, const char *texte);

/* Identique, pour la ligne de consigne. */
void tuile_definir_consigne(tuile_t *t, const char *texte);

/* Grise ou dégrise la tuile (spécification §5.3 : `donnees_perimees`
 * transmis par mettre_a_jour, jamais une décision prise par le widget
 * lui-même). Recolorie systématiquement les trois labels à chaque appel —
 * jamais un gris à sens unique, voir la leçon de la tâche 4
 * (habillage_pomper() applique le même ternaire à chaque cycle). */
void tuile_griser(tuile_t *t, bool grise);

/* Pur, sans lien avec `tuile_t` : écrit la représentation texte d'une
 * température (un chiffre après la virgule, ex. "205.0") dans `sortie`, ou
 * "--" si `celsius` n'est pas plausible. Moonraker renvoie parfois 0 pour
 * une sonde absente et des valeurs aberrantes pendant un redémarrage de
 * klippy ; afficher "--" plutôt qu'un nombre faux est la même règle que le
 * grisage — ne jamais présenter comme mesuré ce qui ne l'est pas. Bornes
 * retenues : [-5, 500] °C (0.0 °C, lui, EST une mesure réelle et rend
 * "0.0", pas "--"). NaN et infini rendent aussi "--". `sortie` NULL ou
 * `taille` nulle : ne fait rien. Tronque proprement (toujours NUL-terminé)
 * si `taille` est trop petite pour le résultat complet. */
void ui_format_temperature(char *sortie, size_t taille, float celsius);
