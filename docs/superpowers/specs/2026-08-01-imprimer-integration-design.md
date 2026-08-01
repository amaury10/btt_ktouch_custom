# Intégration Imprimer / statut d'impression — sous-projet 5

Date : 2026-08-01
Statut : design (autonomie, suit KlipperScreen ; à relire).

## 1. Contexte & objectif

Dernière case du menu hub : **Imprimer**. L'écran de statut d'impression
`ECRAN_ACCUEIL` (jalon 2b) existe déjà — tuiles température, barre de
progression, boutons Pause/Cancel/E-STOP câblés via `ui_commander` +
confirmation — mais il est bâti pour l'ancienne mise en page **800px** (avant le
rail) et n'est **atteignable par aucun chemin** depuis la refonte. Par ailleurs,
`accueil_impression_actif(etat)` (helper pur, testé, `accueil_choix.h`) existe
mais n'est consulté **qu'au démarrage** — la **bascule vivante** repos↔impression
pendant une session est le morceau différé depuis le jalon 3b.

Objectif : rendre le statut d'impression (1) atteignable et correctement
dimensionné, et (2) **affiché automatiquement** quand une impression tourne.

## 2. Architecture / navigation

- `ECRAN_ACCUEIL` (id `"accueil"`, statut impression) converti en **742px**
  (conteneur nav à droite du rail), comme le hub/Déplacer l'ont été.
- Case menu **Imprimer** → `navigation_empiler(&ECRAN_ACCUEIL)` (accès manuel au
  statut, même idiome que les autres cases).
- **Bascule vivante** : l'écran de FOND (profondeur 1) suit l'état d'impression
  — hub au repos, `ECRAN_ACCUEIL` en impression — recalculé à chaque cycle par
  l'habillage, **sans jamais arracher l'utilisateur d'un sous-écran** (bascule
  uniquement quand on est déjà au fond de pile, profondeur 1).

## 3. Composants

### 3.1 (Tâche 1) Conversion 742 + câblage menu
- `ecran_accueil.c` : `LARGEUR_CONTENU 800 → 742`, ajuster `TUILE_LARGEUR`
  (hardcodé 370, `MARGE`=20) pour que les `_Static_assert` passent
  (`3*MARGE + 2*TUILE_LARGEUR == 742` ⇒ TUILE_LARGEUR = 341), maj commentaires.
- Hub : case `ECRAN_ACCUEIL_HUB_MENU_IMPRIMER` (index 4) → empile `ECRAN_ACCUEIL` ;
  `sous_titre = ""`.

### 3.2 (Tâche 2) Bascule automatique repos↔impression
- **Nouvelle primitive** `navigation_remplacer_base(const ecran_desc_t *desc)` :
  dépile jusqu'à la profondeur 1 (comme `navigation_accueil`), puis REMPLACE
  l'écran du fond par `desc` (détruit l'ancien fond, construit le nouveau) ;
  no-op si le fond est déjà `desc` (même id). Incrémente `navigation_sequence`.
- **Chooser injecté** (comme le rail/les réglages sont injectés dans
  l'habillage générique) : `habillage_definir_choix_accueil(const ecran_desc_t*
  (*choisir)(const void *etat, void *ctx), void *ctx)`. L'app enregistre un
  chooser qui enveloppe `accueil_impression_actif` :
  impression → `ECRAN_ACCUEIL`, repos → `ECRAN_ACCUEIL_HUB`.
- `habillage_pomper` : après la mise à jour normale, SI `navigation_profondeur()
  == 1` ET qu'un chooser est enregistré, calcule le fond voulu ; s'il diffère de
  l'id du fond courant, appelle `navigation_remplacer_base`. À profondeur > 1
  (l'utilisateur est dans un sous-écran), NE bascule PAS.
- App (`app_main.c`, `simulateur/main.c`) : enregistre le chooser. Le choix AU
  BOOT reste inchangé (déjà géré).

## 4. Flux de données

- `impression_en_cours`/`impression_en_pause` (etat_klipper) → `accueil_impression_actif`
  → fond voulu. Progression/temps/fichier alimentent `ECRAN_ACCUEIL` (déjà fait).
- Pause/Cancel/E-STOP : déjà câblés dans `ECRAN_ACCUEIL` (inchangé).

## 5. Gestion d'erreurs / anti-thrash

Le chooser rend le MÊME écran tant que l'état ne bascule pas ; `navigation_remplacer_base`
est no-op si le fond est déjà le bon → aucun rebuild en boucle. La garde
profondeur==1 empêche d'arracher un sous-écran. Données périmées → grisage (déjà
dans `ECRAN_ACCUEIL`).

## 6. Tests (host-test)

- Nav (`test_navigation.c`) : `navigation_remplacer_base` — remplace le fond à
  profondeur 1 ; à profondeur >1, dépile d'abord puis remplace ; no-op si même id ;
  incrémente la séquence.
- Intégration (`test_commandes.c` ou dédié, via `habillage_pomper`) : état
  impression → fond bascule vers `ECRAN_ACCUEIL` ; retour repos → hub ; dans un
  sous-écran (profondeur 2) → PAS de bascule.
- Hub (`test_ecran_accueil_hub.c`) : case Imprimer → empile `ECRAN_ACCUEIL`
  (profondeur 2, id `"accueil"`), libellé sans « A venir ».

## 7. Périmètre & hors-scope

- **Dans** : conversion 742 + câblage Imprimer (T1) ; bascule vivante (T2).
- **Hors** : navigateur de fichiers pour DÉMARRER une impression (nécessite la
  liste de fichiers Moonraker, absente de `etat_klipper_t` — évolution backend
  séparée). L'impression se démarre depuis l'hôte (Mainsail/console) ;
  l'appareil en montre le statut et permet Pause/Cancel/E-STOP.
