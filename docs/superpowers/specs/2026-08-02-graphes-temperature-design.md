# Graphes de température + accueil interactif — conception

**Date :** 2026-08-02
**Statut :** design validé (dialogue utilisateur). Décisions : accueil en
**2 colonnes façon KlipperScreen** (gauche = chauffants interactifs + grand
graphe ; droite = 5 tuiles) ; courbe = **température réelle uniquement** (pas de
courbe de consigne pour l'instant) ; fenêtre **~10 min, 1 point / 5 s**.

## Objectif

Ajouter le graphe de température au main_panel et rendre le résumé interactif :
sur l'accueil, **taper le NOM d'un chauffant affiche/masque sa courbe**, **taper
sa VALEUR édite la consigne**. Se rapproche du vrai main_panel KlipperScreen.

## Contraintes globales

- **RAM** : l'historique vit dans un **store séparé**, JAMAIS dans
  `etat_klipper_t` (règle [[etat-klipper-taille-piles]]). Store statique borné,
  protégé par portMUX (même patron que `klipper_fichiers.{h,c}`).
- **Perf LCD** : le `lv_chart` ne se **redessine que quand un nouveau point
  arrive** (~toutes les 5 s), pas à la cadence UI (200 ms) — le tear-free a été
  durement acquis, on ne stresse pas le redraw. Les valeurs texte du résumé,
  elles, se rafraîchissent à la cadence UI normale.
- Contrat `ecran_desc_t`, contexte socle, grisage C3, cibles tactiles ≥ 44 px,
  `_Static_assert` largeur/hauteur + clearance bandeau.
- Textes ASCII/anglais + `LV_SYMBOL_*` ; commentaires français.
- `lv_chart` est déjà activé (`CONFIG_LV_USE_CHART=y`) — aucun ajout Kconfig.
- Builds host-test + idf + sim verts ; SDD.

## Architecture

### Store `klipper_temp_historique.{h,c}` (nouveau)

Tampon circulaire d'historique de température réelle, **une série par
chauffant** (mapping FIXE : série `i` = `extrudeurs[i]` pour `i∈[0,7]` ;
dernière série = `plateau`). `KLIPPER_HISTO_POINTS = 120` (~10 min à 1 pt/5 s),
valeurs en `int16_t` (°C entier ; suffisant pour un graphe). Taille :
`9 séries × 120 × 2 o ≈ 2,2 Ko` statique — sûr (borné, hors pile, hors
`etat_klipper_t`).

API :
- `void klipper_temp_historique_pousser(const etat_klipper_t *e)` — lit les
  températures actuelles (extrudeurs présents + plateau) et pousse UN point par
  série ; avance la tête + incrémente une **génération** (compteur monotone).
  Appelé par l'échantillonneur.
- `uint32_t klipper_temp_historique_generation(void)` — la génération courante,
  pour que l'accueil sache s'il y a un nouveau point (redraw seulement alors).
- Lecture **incrémentale** (chemin normal) : `bool klipper_temp_historique_dernier(
  uint8_t serie, int16_t *sortie)` — le dernier point d'une série. L'accueil
  appelle ça par série et fait `lv_chart_set_next_value` : **aucune copie du
  gros tampon**.
- Lecture **de remplissage** (à la (re)construction de l'accueil seulement) :
  `size_t klipper_temp_historique_serie(uint8_t serie, int16_t *dest, size_t max)`
  — copie les points d'UNE série (≤120 × 2 o) dans un tampon fourni, dans
  l'ordre chronologique (wraparound géré ici). L'accueil backfill le chart
  série par série.

**RÈGLE RAM anti-piège** : ne JAMAIS copier le store entier (~2,2 Ko) sur la
pile du fil LVGL. Pas d'API « lire tout le store dans un `klipper_temp_historique_t`
local ». Le chart LVGL détient déjà les 120×N points en interne (socle) ; on
l'alimente point par point (incrémental) et on backfill série par série dans un
tampon `int16_t[120]` local (240 o, sûr sur la pile).

### Échantillonneur global et continu

Un point est poussé **toutes les ~5 s, indépendamment de l'écran affiché**, pour
que l'historique se remplisse même hors accueil. Placé dans le chemin de
sondage applicatif (là où l'état Klipper frais est disponible en continu — à
préciser au plan : boucle de sondage Klipper décimée à 5 s, ou timer global).
Le push (portMUX, section critique = copie de scalaires) est sûr vis-à-vis de
la lecture par le fil LVGL.

## Accueil 2 colonnes (réécriture de ECRAN_ACCUEIL_HUB)

Symbole/id `ECRAN_ACCUEIL_HUB` / `"accueil_hub"` / titre `"Home"` **conservés**
(app_main.c, rail, chooser inchangés). Nouveau contenu, 742×436 :

- **Colonne gauche (~360 px)** :
  - **Lignes chauffants interactives** (bornées en hauteur) : chaque chauffant
    présent = une ligne avec un **NOM tappable** (`T0`, `Bed`, …) et une
    **VALEUR tappable** (`210/210`). Puis ligne position `X:.. Y:.. Z::` + outil
    actif, ligne vitesse/flux, mini-progression si `impression_en_cours`.
  - **Graphe** `lv_chart` (~250 px de haut) : une série par chauffant présent,
    température réelle, alimenté depuis le store.
- **Colonne droite (~360 px)** : les **5 tuiles** empilées verticalement
  (Homing / Temperature / Actions / Configuration / Print), `navigation_empiler`
  vers les écrans existants (mapping inchangé par rapport à l'actuel).

Cas multi-outils : la zone des lignes chauffants est **bornée** ; au-delà de N
lignes visibles, elles se compactent (le réglage détaillé de chaque chauffant
reste derrière la tuile Temperature). Le graphe garde sa hauteur.

Grisage C3 : les valeurs relues (chauffants, position, vitesse/flux) grisent sur
`donnees_perimees` ; les tuiles et les boutons de toggle ne grisent pas.

## Interactions (les raccourcis)

- **Tap NOM** → bascule la visibilité de la série du chauffant
  (`lv_chart_hide_series`) ; le nom **se grise** quand la courbe est masquée
  (retour visuel de l'état on/off). État de visibilité stocké dans le contexte
  socle de l'écran.
- **Tap VALEUR** → `clavier_ouvrir(CLAVIER_NUMERIQUE, prérempli avec la consigne
  actuelle)` → au retour, borne et envoie `klipper_gcode_consigne_temp`
  (SET_HEATER_TEMPERATURE), **mêmes bornes que ecran_temperatures.c**
  (extrudeur 0-350, plateau selon borne existante). Réutilise le parsing borné
  (`strtol`, rejet hors bornes, notif d'erreur) de cet écran.

## Hors périmètre (lots ultérieurs)

- **Courbe de consigne** (2ᵉ série pointillée par chauffant) — non demandée pour
  l'instant.
- Graphe sur l'écran Températures dédié (ce lot met le graphe sur l'accueil).
- Icônes bitmap.

## Ordre de réalisation

① Store `klipper_temp_historique` + tests hôte (push/lire, wraparound, mapping).
② Échantillonneur continu (câblage + décimation 5 s). ③ Réécriture accueil
2 colonnes : colonne droite (5 tuiles) + lignes chauffants + graphe. ④ Raccourci
**valeur → consigne** (clavier + gcode). ⑤ Raccourci **nom → toggle courbe**
(+ grisage du nom). Chaque étape testable ; le store d'abord (fondation),
l'accueil ensuite, les deux raccourcis pour finir.
