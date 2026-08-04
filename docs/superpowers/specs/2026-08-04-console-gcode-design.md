# Console gcode Moonraker — Design

**Date :** 2026-08-04
**Contexte :** l'écran `ECRAN_CONSOLE` est un stub (« Requires gcode_response
capture »), déjà câblé dans les menus Configuration ET Actions. La notification
Moonraker `notify_gcode_response` est reçue mais jetée (cas `default` du dispatch,
`moonraker_ws.c:532`). L'envoi d'un gcode fonctionne déjà
(`ui_commander(BACKEND_ACTION_GCODE, {"script":"..."})`).

## Objectif

Une console : afficher le flux des réponses Klipper (`gcode_response`) + les
commandes envoyées (écho local), et taper/envoyer une commande gcode au clavier
tactile.

## Architecture

### Store dédié — `main/apps/klipper/console_log.{h,c}`

Ring-buffer de lignes, patron `klipper_fichiers.c`/`power_devices.c` (verrou
portMUX ESP / no-op hôte, copie unique, **JAMAIS dans `etat_klipper_t`**).

```c
#define CONSOLE_LIGNES_MAX  24
#define CONSOLE_LIGNE_MAX   96
typedef struct {
    char lignes[CONSOLE_LIGNES_MAX][CONSOLE_LIGNE_MAX];
    uint8_t debut;   /* index de la plus ancienne ligne (ring) */
    uint8_t nb;      /* nb de lignes valides (<= MAX) */
    uint32_t generation;
} console_log_t;
void console_log_ajouter(const char *ligne);      /* append ring, sous verrou, generation++ */
void console_log_lire(console_log_t *dest);        /* snapshot sous verrou */
void console_log_effacer(void);                    /* vide + generation++ */
```

`console_log_ajouter` tronque la ligne à `CONSOLE_LIGNE_MAX-1`. Les réponses
multi-lignes de Klipper (séparées par `\n`) sont découpées en lignes distinctes
(par l'appelant WS ou une petite fonction utilitaire — au choix, documenté).

### Parseur pur — `main/apps/klipper/moonraker_rpc.{h,c}`

```c
bool rpc_lire_gcode_response(char *dest, size_t dest_n, const char *json, size_t n);
```

Extrait la chaîne réponse de `notify_gcode_response` dont `params[0]` est une
**string** (ex. `{"method":"notify_gcode_response","params":["// echo: ..."]}`).
Retourne false si la forme ne correspond pas (protège le dispatch, voir plus
bas). Pur, testé hôte.

### Câblage WS — dispatch par NOM DE MÉTHODE (robuste)

`moonraker_ws.c` : aujourd'hui `notify_power_changed` (feature Power) et
`notify_gcode_response` tombent tous deux dans `RPC_MSG_AUTRE` puis sont
distingués par la FORME de leur payload. En ajoutant la console, **refactorer**
le traitement de `RPC_MSG_AUTRE` pour extraire le champ `"method"` de la
notification et dispatcher explicitement :
- `notify_power_changed` → `rpc_lire_power_changed` → boucle `power_devices_maj_un`
- `notify_gcode_response` → `rpc_lire_gcode_response` → `console_log_ajouter`
  (en découpant sur `\n` si multi-lignes).

Extraire la méthode via une fonction pure (regarder si `rpc_classifier` /
`moonraker_rpc` expose déjà de quoi lire `method` ; sinon ajouter
`bool rpc_lire_methode(char *dest, size_t n, const char *json, size_t len)`
testée hôte). Le dispatch par méthode remplace le dispatch par forme (plus sûr).

### Écran — `main/apps/klipper/ecrans/ecran_console.{c,h}` (remplace le stub)

- Retirer `ECRAN_CONSOLE` de `STUBS(X)` (`ecran_stub.c`), ajouter `ecran_console.c`
  au CMake, garder le symbole `ECRAN_CONSOLE` visible pour les menus (Config +
  Actions) — même précaution que la feature Power (déclaration dans
  `ecran_console.h`, includes des menus ajustés si besoin).
- Layout : zone de scrollback plein largeur en haut (label/textarea LVGL en
  lecture seule, police mono si dispo sinon montserrat_14, scroll auto vers le
  bas), + en bas un champ de saisie et un bouton **Envoyer**, alimentés par le
  **clavier tactile** existant (patron de la saisie mot de passe WiFi
  `ecran_reglages_wifi.c` — réutiliser le widget clavier). Un bouton **Effacer**.
- `mettre_a_jour` : relit `console_log_lire`, ré-affiche si `generation` a changé
  (mémoriser la dernière vue dans le contexte), auto-scroll en bas.
- Envoi : à la validation, écho local `console_log_ajouter(">> <cmd>")` puis
  `ui_commander(BACKEND_ACTION_GCODE, "{\"script\":\"<cmd>\"}")` (JSON construit
  avec snprintf borné ; **échapper** correctement la commande utilisateur pour le
  JSON — guillemets/backslash au minimum, car l'utilisateur tape du texte libre).

## Contraintes / invariants

- **Rien dans `etat_klipper_t`** (store console dédié, ring-buffer).
- Échappement JSON de la saisie libre = point de sécurité à ne pas rater
  (contrairement aux noms de prises Power qui étaient contraints).
- Découpage multi-lignes borné (une réponse énorme ne doit pas déborder).
- Pièges ESP-only habituels (`*/`, format-truncation) sur moonraker_ws.c / écran.

## Tests

- Host-test : `rpc_lire_gcode_response` (string simple, multi-lignes, params
  absent/mauvais type → false), `rpc_lire_methode` (si ajouté), ring-buffer
  console (append au-delà de MAX = éviction FIFO, troncature de ligne, effacer,
  génération), échappement JSON de la saisie (fonction utilitaire si extraite).
- idf gate + matériel : affichage flux, saisie clavier, envoi, auto-scroll.

## Hors périmètre

- Pas d'historique de commandes (rappel des précédentes) en v1.
- Pas de coloration syntaxique ; distinction écho/réponse par un simple préfixe.
