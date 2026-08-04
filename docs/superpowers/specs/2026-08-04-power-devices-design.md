# Power devices Moonraker — Design

**Date :** 2026-08-04
**Branche :** jalon-3b-accueil-idle
**Contexte :** l'écran `ECRAN_POWER` est un stub (« Requires Moonraker power API »),
déjà listé dans le sous-menu Configuration (`ecran_menu_reglages.c:98`). Le
transport Moonraker (WS + commande corrélée) existe déjà. Objectif : lister et
piloter les prises pilotées par Moonraker (`[power ...]`).

## Objectif

Depuis l'écran Power : voir chaque prise Moonraker avec son état (ON/OFF), et la
basculer (avec confirmation). Refléter les changements externes (notification
`notify_power_changed`).

## API Moonraker utilisée

- `machine.device_power.devices` → `{"devices":[{"device":"nom","status":"on|off",...}]}`
  (liste + états).
- `machine.device_power.set_device` params `{"device":"nom","action":"toggle"}`
  → bascule ; renvoie le nouvel état.
- Notification asynchrone `notify_power_changed` params `[{"device":"nom","status":"on"}...]`
  → mise à jour poussée par Moonraker.

Tout passe par `moonraker_ws_commande(methode, params_json, ...)` (déjà présent,
appelé depuis `boucle_traiter_commandes()`), et le dispatch des notifications se
greffe là où `notify_gcode_response` est aujourd'hui ignoré
(`moonraker_ws.c:532`, cas `default`).

## Architecture

### Store dédié — `main/apps/klipper/power_devices.{h,c}`

Sur le patron EXACT de `klipper_fichiers.c` (verrou, copie unique, **JAMAIS dans
`etat_klipper_t`** — cf. la note mémoire « état trop gros = crash WS »). POD :

```c
#define POWER_DEVICES_MAX   8
#define POWER_NOM_MAX       32
typedef struct {
    char nom[POWER_NOM_MAX];
    bool allumee;        /* status == "on" */
    bool connue;         /* etat recu (sinon inconnu) */
} power_device_t;
typedef struct {
    power_device_t devices[POWER_DEVICES_MAX];
    uint8_t nb;
    bool tronque;
    uint32_t generation;  /* incremente a chaque definir() -> l'UI detecte le neuf */
} power_devices_t;
void power_devices_definir(const power_devices_t *src);   /* sous verrou (tache WS) */
void power_devices_lire(power_devices_t *dest);           /* sous verrou (UI) */
void power_devices_maj_un(const char *nom, bool allumee); /* MAJ ciblee (notify) */
```

### Parseur pur — `main/apps/klipper/moonraker_rpc.{h,c}`

Deux fonctions pures testables hôte (patron `rpc_lire_fichiers`) :
- `bool rpc_lire_power_devices(power_devices_t *dest, const char *json, size_t n)`
  — parse la réponse de `machine.device_power.devices`.
- `bool rpc_lire_power_changed(power_devices_t *dest, const char *json, size_t n)`
  — parse le tableau de `notify_power_changed` (params[0] est le tableau).

### Câblage WS — `main/apps/klipper/moonraker_ws.c`

- Au connect / `notify_klippy_ready` : envoyer `machine.device_power.devices`
  (nouvel id `g_id_power`, patron `g_id_fichiers`), dispatcher la réponse vers
  `rpc_lire_power_devices` → `power_devices_definir`. (Moonraker sert cette
  méthode même Klippy non prêt, donc on peut aussi la lancer au connect.)
- Dans le dispatch des notifications : intercepter `notify_power_changed` (avant
  le `default`) → `rpc_lire_power_changed` → `power_devices_definir`.

### Envoi de la bascule — chaîne de commande

Nouvelle action backend `BACKEND_ACTION_POWER "power"` (`core/backend.h`), params
`{"device":"nom","action":"toggle"}`. Dispatch dans `backend_moonraker.c` (patron
du gcode) → `moonraker_ws_commande("machine.device_power.set_device", params, ...)`.
L'UI appelle `ui_commander(BACKEND_ACTION_POWER, args)`. Après une bascule
réussie, Moonraker émet `notify_power_changed` qui rafraîchit le store — pas
besoin de re-fetch manuel.

### Écran — `main/apps/klipper/ecrans/ecran_power.c` (remplace le stub)

- `construire()` : titre "Power", une ligne par prise (nom + pastille ON/OFF
  colorée), colonne pleine largeur scrollable (patron `ecran_fichiers.c`). Si
  `nb==0` : « Aucune prise Moonraker ».
- `mettre_a_jour()` : relit `power_devices_lire`, redessine si `generation` a
  changé (mémoriser la dernière génération vue dans le contexte).
- Tap sur une prise → confirmation « Basculer <nom> ? » (patron confirmation de
  `ecran_fichiers.c`) → `ui_commander(BACKEND_ACTION_POWER, "{\"device\":\"<nom>\",\"action\":\"toggle\"}")`.
  Confirmation car une prise peut couper l'imprimante/une impression.
- Retirer `ECRAN_POWER` de `ecran_stub.c` (`STUBS(X)`), ajouter `ecran_power.c`
  au CMake, garder l'export `ECRAN_POWER` (même symbole, le menu ne change pas).

## Contraintes / invariants

- **Rien dans `etat_klipper_t`** : le store power est dédié et à copie unique.
- Parseurs purs testés hôte (host-test), écran/WS validés idf + matériel.
- `moonraker_ws.c` / `web.c` / `ota.c` : pièges `*/` et format-truncation si on
  touche des fichiers ESP-only. La plupart du nouveau code est host-testable
  (store + parseurs).
- Le fetch power au connect ne doit pas gonfler la pile WS : réutiliser les
  tampons existants, pas de `power_devices_t` sur la pile du handler (c'est un
  POD ~300 o, acceptable en une copie locale ponctuelle, mais préférer le passer
  au store directement).

## Tests

- Host-test : `rpc_lire_power_devices` / `rpc_lire_power_changed` (JSON réels
  Moonraker : liste vide, 1-N prises, on/off, tableau notify), store
  définir/lire/maj_un, bornage `POWER_DEVICES_MAX`/troncature.
- idf gate + matériel : écran, bascule, notification live.

## Hors périmètre

- Pas de création/suppression de prises (lecture + toggle seulement).
- Pas d'action on/off explicite séparée (toggle suffit ; extensible plus tard).
