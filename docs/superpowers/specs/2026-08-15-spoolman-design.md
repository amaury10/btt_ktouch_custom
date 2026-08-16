# Panneau Spoolman — conception

**Date :** 2026-08-15
**Statut :** validé (« ok tu peux y aller »)

## Contexte

Spoolman v0.26.1 tourne désormais sur le Raspberry Pi de l'imprimante
(port 7912, service systemd, base SQLite) et Moonraker y est câblé
(`[spoolman]`, `spoolman_connected: true`). Le panneau Spoolman de l'écran
K-Touch est le **dernier stub** du menu Configuration ; il n'a plus de raison
de le rester.

Objectif : voir les bobines connues de Spoolman et **désigner celle qui est
chargée**, pour que Moonraker décompte automatiquement le filament consommé.

## Ce que le panneau fait (et ne fait pas)

**Fait :**

- liste des bobines non archivées : pastille de couleur, fabricant, nom du
  filament, matière, poids restant ;
- marque visuellement la bobine active ;
- un tap sur une rangée propose (confirmation) de la déclarer active ;
- bouton **Clear** : plus aucune bobine active ;
- bouton **Refresh** : redemande la liste (une bobine ajoutée depuis Mainsail
  apparaît sans redémarrer l'écran) ;
- dit clairement quand Spoolman est injoignable, plutôt que d'afficher une
  liste vide trompeuse.

**Ne fait pas (hors périmètre, assumé) :** créer/modifier/archiver une bobine,
saisir un poids, gérer les emplacements. Ces opérations d'inventaire se font
au clavier, sur l'interface web Spoolman — pas sur une dalle 5" au doigt.

## Chemin de données

Tout passe par **Moonraker**, jamais par Spoolman en direct : l'écran ne
connaît qu'une seule adresse serveur, et Moonraker relaie déjà tout ce qu'il
faut (vérifié sur la machine réelle, Moonraker v0.9.3) :

| Besoin | Méthode JSON-RPC | Charge utile |
| --- | --- | --- |
| Liste des bobines | `server.spoolman.proxy` | `{"request_method":"GET","path":"/v1/spool","query":"allow_archived=false"}` → `result` = tableau de bobines |
| Bobine active + état | `server.spoolman.status` | `result.spool_id` (ou `null`), `result.spoolman_connected` |
| Désigner l'active | `server.spoolman.post_spool_id` | `{"spool_id": N}` — **`{}`, clé omise**, pour effacer (voir ci-dessous) |
| Changement d'active | notification `notify_active_spool_set` | `params[0].spool_id` |

**Pourquoi `server.spoolman.post_spool_id` et pas la macro gcode
`SET_ACTIVE_SPOOL`** (installée sur le Pi pour Mainsail/slicers) : elle ne
dépend pas de `printer.cfg`, ne traverse pas la file gcode, et fonctionne
même si Klippy est en erreur — trois raisons pour lesquelles un panneau
d'écran ne doit pas emprunter le chemin gcode ici.

**Deuxième piège, même endpoint (2026-08-15)** : le nom JSON-RPC n'est PAS
`server.spoolman.spool_id` mais **`server.spoolman.post_spool_id`**. Moonraker
préfixe le verbe au dernier segment du chemin dès qu'un endpoint accepte
plusieurs verbes (`APIDefinition.create()`, `moonraker/common.py`) ;
`/server/spoolman/spool_id` étant GET|POST, il expose `get_spool_id` et
`post_spool_id`. Le nom sans préfixe répond `-32601 Method not found`. Les
endpoints à verbe unique (`status`, `proxy`) gardent leur nom tel quel — c'est
pourquoi la liste s'affichait alors que la sélection échouait.

**Piège vérifié sur la machine (2026-08-15)** : pour désactiver la bobine,
la clé `spool_id` doit être **absente**, pas à `null`. Le handler Moonraker
fait `web_request.get_int("spool_id", None)`, qui tente `int(None)` dès que
la clé est présente : `{"spool_id": null}` répond `HTTP 400 — Unable to
convert argument [spool_id] to <class 'int'>` et la bobine reste active.
Clé omise = le défaut `None` du handler = désactivation.

Forme réelle d'une bobine (relevée sur la machine, ~540 octets) : `id`,
`filament.name`, `filament.vendor.name`, `filament.material`,
`filament.color_hex`, `filament.weight`, `remaining_weight`, `location`,
`archived`. `remaining_weight` peut être **absent** (bobine sans poids
initial) : ce cas s'affiche « ? g », jamais 0 g.

## Découpage

### Store `spoolman_store.h/.c`

Même patron que `bed_mesh_store` (PSRAM paresseuse + CAS de publication,
portMUX court, compteur de génération unique) :

```c
#define SPOOLMAN_BOBINES_MAX 12
typedef struct {
    int32_t  id;
    char     filament[24], fabricant[24], matiere[12];
    uint32_t couleur;  bool couleur_connue;
    float    restant_g; bool restant_connu;
    float    total_g;
} spoolman_bobine_t;                    /* ~84 o */

typedef struct { uint8_t nb; bool tronquee; spoolman_bobine_t bobines[12]; }
        spoolman_liste_t;               /* ~1 Ko -> scratch PSRAM obligatoire */

typedef struct { int32_t id_actif; bool connecte; bool statut_connu; }
        spoolman_etat_t;                /* léger -> pile autorisée */
```

Liste et état ont des setters distincts mais **partagent le compteur de
génération** : l'écran consomme les deux, un compteur par source ne lui ferait
rien redessiner de plus (même choix que `bed_mesh` + ses profils).

`id_actif` vaut `SPOOLMAN_AUCUNE_BOBINE` (-1) quand aucune n'est active —
jamais 0, qui est un identifiant valide côté Spoolman.

### Parseurs (`moonraker_rpc.c`, purs et host-testés)

- `rpc_lire_spoolman_liste(spoolman_liste_t*, json, n)` — enveloppe `result`
  = tableau ; bobine archivée ignorée ; au-delà de 12 → `tronquee` ;
  `color_hex` accepte `RRGGBB` **et** `RRGGBBAA` (l'alpha est ignoré),
  toute autre forme → couleur inconnue (pastille grise), jamais une couleur
  inventée.
- `rpc_lire_spoolman_statut(spoolman_etat_t*, json, n)` — `result.spool_id`
  (`null` → aucune) + `result.spoolman_connected`.
- `rpc_lire_notif_spool_actif(int32_t*, json, n)` — `params[0].spool_id`.

Politique de poison inchangée : un champ absent/mal typé n'invalide que ce
champ ; seule une enveloppe cassée fait rendre `false`.

### Classification

`notify_active_spool_set` devient un type à part entière du classifieur
(`RPC_MSG_SPOOL_ACTIF`) plutôt qu'un `RPC_MSG_AUTRE` re-parsé après coup :
une classification, un parse — la leçon de la revue Bed Mesh (constats
F1/F6, détection par sous-chaîne supprimée).

### Câblage WS

- au connect **et** à chaque `notify_klippy_ready` : `server.spoolman.status`
  puis la requête de liste (mêmes drapeaux différés `g_*_a_demander` que
  macros/power, jamais d'envoi sous `g_verrou`) ;
- `notify_active_spool_set` → `spoolman_definir_actif()` ;
- `moonraker_ws_demander_bobines()` : public, appelable depuis la tâche UI
  (bouton Refresh) — envoie directement comme `moonraker_ws_commande()` le
  fait déjà, l'identifiant de corrélation étant posé **sous `g_verrou`**
  puisqu'il est écrit hors tâche WS.

### Action backend

`BACKEND_ACTION_SPOOLMAN` — `arguments_json` = `{"spool_id":N}` ou `{}`
(clé omise, voir le piège ci-dessus), relayé tel quel à `server.spoolman.spool_id` : copie
exacte du patron `BACKEND_ACTION_POWER` (pas de repli HTTP, WS hors ligne =
refus honnête).

### Écran `ecran_spoolman.c`

Gabarit de `ecran_fichiers.c` : colonne unique de boutons pleine largeur,
**page de 5**, pagination Prev/Next, grisage quand les données sont périmées.
Différences :

- une pastille de couleur (16x16, coins arrondis) à gauche du libellé ;
- libellé `Fabricant — Filament (PETG)` et, à droite, `820 / 1000 g` ;
- la bobine active porte `LV_SYMBOL_OK` et un fond distinct ;
- en-tête : `Active: <nom>` / `No active spool` / `Spoolman offline` ;
- rangée du bas : `Refresh` et `Clear active` (confirmation sur Clear).

## Tests hôte

- parseurs : bobine nominale, `remaining_weight` absent, `color_hex` invalide
  et à 8 chiffres, archivée ignorée, 15 bobines → troncature, enveloppes
  cassées → `false` sans toucher la destination ;
- statut : `spool_id` nul / entier / absent ;
- store : génération commune liste+état, gardes NULL, bornes ;
- écran : libellés et pagination relus par `lv_label_get_text()` (patron
  `test_ecran_fichiers.c`), y compris « aucune bobine » et « hors ligne ».

## Risque connu

La liste n'est rafraîchie qu'au connect, sur `notify_klippy_ready` et au tap
sur Refresh : un poids restant peut donc être légèrement périmé pendant une
impression (Moonraker synchronise Spoolman toutes les 5 s côté serveur). Un
abonnement continu n'existe pas pour cet objet, et rafraîchir en boucle une
liste de 12 bobines pour un chiffre qui bouge lentement ne le vaut pas.
