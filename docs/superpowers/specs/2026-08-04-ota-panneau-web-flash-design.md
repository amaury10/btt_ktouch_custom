# Panneau OTA navigateur + écran propre pendant le flash — Design

**Date :** 2026-08-04
**Branche :** jalon-3b-accueil-idle
**Contexte :** le lot OTA (backup BTT→spiffs, dry-run, commit gardé, restore) est
construit ET validé matériel le 2026-08-04 (garde 409, backup `valide`, dry-run,
restore vers BTT, flash de notre fw via `/update` BTT, vrai commit `slot:app0`).
Aujourd'hui l'écriture réelle se déclenche uniquement en `curl`. Ce design ajoute
l'usage **navigateur** et rend le glitch d'écran **propre**.

## Objectif

1. Piloter tout le cycle OTA depuis un navigateur (flash réel avec progression,
   sauvegarde BTT, restauration BTT), sans `curl`.
2. Pendant l'écriture flash, éteindre le rétroéclairage (écran noir volontaire)
   au lieu de laisser la dalle RGB afficher du bruit.

## Cause racine du glitch d'écran (documentée, pas un bug)

Sur l'ESP32-S3, un `esp_partition_erase_range`/`esp_ota_write` **désactive le
cache flash** le temps de l'opération. La dalle RGB lit son framebuffer en PSRAM
en continu par DMA ; pendant la coupure de cache ce flux est affamé → le panneau
perd la synchro et affiche du bruit. Purement cosmétique, se remet au reboot.
Couper le rétroéclairage (`pt_backlight_set(0)`) rend le bruit **invisible**.

## Architecture

Deux composants, **tous deux confinés à `firmware/main/web.c`** (fichier ESP-only ;
`ota.c` — validé opus — n'est PAS touché). Aucune nouvelle route HTTP :
`GET /ota`, `POST /ota`, `POST /backup-btt`, `POST /restore-btt` existent déjà et
sont enregistrées (`max_uri_handlers=16`). Le backend flash est inchangé.

### Composant A — Panneau de contrôle `GET /ota` (`gestion_ota_page`)

Réécriture de la page HTML servie par `gestion_ota_page` (web.c) en panneau :

- **État** : slot / version / sauvegarde BTT (valeurs déjà disponibles :
  `esp_ota_get_running_partition()`, `esp_app_get_description()`,
  `ota_backup_etat_nom(ota_backup_etat())`). Bouton « Rafraîchir l'état » qui
  relit `GET /status` (JSON) et met à jour l'affichage.
- **Mise à jour firmware** :
  - champ SHA-256 optionnel (existant),
  - `<input type="file">` (lu via File API, POST du contenu **brut** — surtout PAS
    un `<form multipart>`, qui casserait le contrôle du magic 0xE9 ; contrainte
    déjà documentée dans le code actuel),
  - bouton **« Vérifier (dry-run) »** → `POST /ota?dry_run=1` (inchangé),
  - bouton **« Flasher (écriture réelle) »**, **désactivé par défaut** et activé
    UNIQUEMENT après un dry-run réussi (HTTP 200 + message contenant « image
    valide ») sur le **fichier actuellement sélectionné**. Changer de fichier
    ré-désactive le bouton (garde-fou choisi : « dry-run obligatoire d'abord »),
  - au clic Flasher : `confirm()` explicite, puis `XMLHttpRequest` `POST /ota`
    (corps = fichier brut) avec **barre de progression** (`xhr.upload.onprogress`),
    puis message « écriture faite, l'écran va noircir puis redémarrer » ; gère
    409 (garde), 400 (image invalide), 5xx.
- **Sauvegarde / Restauration BTT** :
  - bouton **« Sauvegarder BTT → spiffs »** → `confirm()` → `POST /backup-btt` →
    affiche le texte de réponse + rafraîchit l'état,
  - bouton **« Restaurer BTT »** → `confirm()` **renforcé** (« REDÉMARRE la dalle
    sur le firmware BTT d'origine ») → `POST /restore-btt` → affiche la réponse.

**Rendu HTTP (anti-`-Werror=format-truncation`)** : la page est envoyée en
**chunks** (`httpd_resp_send_chunk`). Seul un petit en-tête (les 3 valeurs d'état
`%s`) passe par `snprintf` dans un tampon modeste ; tout le gros corps HTML+JS
est un **littéral statique sans format** envoyé tel quel, puis un chunk final
`NULL,0`. Cela évite un tampon géant et met le format-truncation hors de portée
sur la partie volumineuse.

### Composant B — Écran noir pendant l'écriture (enveloppe web.c)

Autour des DEUX appels bloquants qui écrivent en flash, dans web.c :
`gestion_ota_post` (commit, appelle `ota_appliquer_flux`) et `gestion_restore_btt`
(restore, appelle `ota_restaurer_btt`) :

```c
uint32_t retro = pt_backlight_get();
pt_backlight_set(0);                 /* ecran noir : masque le bruit RGB pendant l'ecriture */
esp_err_t resultat = ota_appliquer_flux(req, msg, sizeof(msg));
if (resultat != ESP_OK) {
    pt_backlight_set(retro);         /* echec: pas de reboot -> restaurer l'ecran */
}
/* succes: le chemin existant repond puis esp_restart() -> l'ecran revient au boot */
```

`web.c` gagne `#include "pandatouch_display.h"`. `pt_backlight_set` écrit un duty
LEDC (registre, rapide, autonome) : effectif avant la coupure de cache et
maintenu par le périphérique pendant l'écriture. Le dry-run ne touche jamais au
rétroéclairage (branche séparée). Un refus (409/400) restaure aussitôt : au pire
un bref clignotement sur une image refusée, acceptable.

## Sécurité / invariants préservés

- `ota.c` (logique flash, garde 409, ordering backup/erase, rollback rescue)
  **inchangé** — aucune régression sur le cœur validé opus.
- La garde serveur reste l'autorité : le bouton Flasher n'est qu'un garde-fou UX
  ; même si on le forçait, `POST /ota` refuse toujours 409 sans sauvegarde valide.
- POST du contenu **brut** (pas multipart) — cohérent avec `ota_verifier_flux` /
  `ota_appliquer_flux` qui lisent `httpd_req_recv` en octet brut.
- Aucun GET à effet de bord (le flash/backup/restore restent des POST).
- Aucune donnée personnelle dans la page ni les commits.

## Tests / validation

- `web.c` est **ESP-only** (pas compilé en host-test) : validation = **gate
  `idf.py build`** (contrôleur) + **build sim** + validation **matériel** à
  l'écran (comme tout le lot OTA). Pièges connus à re-vérifier au gate : `*/`
  dans un commentaire d'un fichier ESP compilé, et `-Werror=format-truncation`.
- Validation matériel : ouvrir `http://<ip>/ota`, vérifier état, dry-run d'un
  `.bin`, activation du bouton Flasher après dry-run, flash réel (écran noircit,
  reboot, `slot` bascule), boutons Sauvegarder/Restaurer.

## Hors périmètre

- Pas de message LVGL « mise à jour en cours » dessiné depuis la tâche flash
  (thread-safety `esp_lvgl_port` risquée, et le bruit grignoterait le message) :
  le rétroéclairage noir suffit et est robuste.
- Pas de refonte des pages sœurs `/backup-btt` / `/restore-btt` (elles restent ;
  le panneau les double via ses boutons).
