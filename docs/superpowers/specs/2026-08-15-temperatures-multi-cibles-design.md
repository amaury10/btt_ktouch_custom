# Températures : cocher les cibles, consigne commune — conception

**Date :** 2026-08-15
**Statut :** validé (demande utilisateur + réponses de cadrage)

## Demande

> « dans le menu température, j'aimerais pouvoir cocher les cibles
> (extrudeurs et lit) pour appliquer une consigne commune (soit entrée à la
> main, soit un preset) »

Cadrage obtenu : **un préréglage = une paire buse/lit** ; ajouter **TPU** et
**Manuel** à la liste des préréglages.

## Le problème réel

Sur une multi-têtes, chauffer quatre buses au même PLA demande aujourd'hui
quatre allers-retours clavier. Le préréglage, lui, ne sait viser que la buse
**active** — ce qui est le bon défaut à une tête, et inutile à huit.

## Ce qui change

### 1. Cocher des cibles

Chaque tuile de température (extrudeurs présents + plateau) reçoit une **case
à cocher** de 28x28, posée en haut à gauche **hors du flux flex** de la tuile
(`LV_OBJ_FLAG_IGNORE_LAYOUT`) : la mise en page existante des trois libellés
(nom / valeur / consigne) n'est pas touchée, aux trois paliers (mono, moyen,
compact).

- La case a son **propre** rappel de clic : LVGL ne fait pas remonter le
  clic d'un enfant cliquable vers le parent sans `EVENT_BUBBLE`, donc taper
  la case coche, taper ailleurs dans la tuile ouvre le clavier de CE
  chauffeur — le geste existant est préservé intégralement.
- Cochée : fond accentué + `LV_SYMBOL_OK`. Décochée : contour seul.

**Où vit la sélection** : dans `cellule_infos[i].selectionne`, à côté de
l'identité du chauffeur (`est_plateau`, `indice_extrudeur`) que la tuile
porte déjà. `mettre_a_jour()` **efface la sélection d'un emplacement dont
l'identité change** — un redémarrage Klipper avec un nombre de têtes
différent réaffecte les emplacements, et une coche héritée viserait alors un
autre chauffeur que celui que l'utilisateur avait coché. Silencieusement
chauffer la mauvaise buse est exactement ce qu'il ne faut pas.

### 2. Préréglages : 4 → 6

| Bouton | Buse | Plateau |
| --- | --- | --- |
| PLA | 210 | 60 |
| PETG | 240 | 80 |
| ABS | 250 | 100 |
| **TPU** | 230 | 50 |
| Off | 0 | 0 |
| **Manuel** | — | — |

Six boutons à largeur égale sur 714 px = ~111 px chacun, libellés en police
14 (`PETG 240/80` ≈ 80 px) : ça tient sans changer la hauteur de la rangée.

**Application d'un préréglage :** la cible buse part vers **chaque extrudeur
coché**, la cible plateau vers le plateau **s'il est coché** — un gcode par
chauffeur, jamais un combiné (règle déjà en vigueur).

**Si rien n'est coché**, le préréglage retombe sur le comportement
historique : **buse active + plateau**. C'est le bon défaut à une tête, et
ça évite un bouton mort pour qui n'a jamais coché quoi que ce soit.

### 3. Manuel

Ouvre le clavier numérique (bornes 0–350 inchangées) et applique **la même
valeur à toutes les cibles cochées**, buses et plateau confondus.

**Si rien n'est coché, Manuel ne fait rien** et le dit
(`habillage_notifier("Select one or more targets first")`). Asymétrie
délibérée avec les préréglages : un préréglage EST une paire buse/lit, il a
donc un sens sans sélection ; un nombre unique n'en a aucun — appliquer 210
au plateau « par défaut » serait une supposition dangereuse.

## Ce qui ne change pas

Le tap sur une tuile ouvre le clavier de ce seul chauffeur, comme
aujourd'hui. La géométrie des trois paliers, les bornes de saisie, la
politique « un gcode par chauffeur » : intactes.

## Tests hôte

Étendre `test_ecran_temperatures.c` :

- taper une case coche/décoche, sans ouvrir le clavier ;
- taper la tuile hors de la case ouvre toujours le clavier du bon chauffeur ;
- PLA avec T0 et T2 cochés, plateau non coché → **deux** gcodes, sur
  `extruder` et `extruder2`, aucun sur `heater_bed` ;
- PLA sans rien de coché → buse active + plateau (repli historique) ;
- Manuel avec des cibles cochées → la même valeur sur toutes ;
- Manuel sans sélection → **aucun** gcode ;
- la sélection est effacée quand l'emplacement change d'identité
  (nb_extrudeurs qui diminue).
