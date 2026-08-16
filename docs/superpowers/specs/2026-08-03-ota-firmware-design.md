# OTA du firmware K-Touch — conception

**Date :** 2026-08-03
**Statut :** design validé (dialogue utilisateur). Décisions : **A/B + rollback
via `rescue.c`** (les deux slots deviennent « nous ») ; **sauvegarde de l'image
BTT dans spiffs** avant le premier écrasement (lève l'irréversibilité) ;
**pipeline testable sans le pas irréversible** ; le tout premier écrasement de
app0 n'est déclenché QUE sur action explicite de l'utilisateur. Échafaudages
(backup, dry-run) nettoyables une fois l'OTA éprouvé.

## Objectif

Mettre à jour NOTRE firmware over-WiFi depuis notre propre firmware, sans
repasser par le `/update` du firmware BTT d'origine + `/revert` manuel. Pas de
port série (WiFi only) → la sûreté repose sur le rollback A/B et une sauvegarde
restaurable de BTT.

## Contexte matériel (immuable ici)

Table de partitions d'origine, NON reprogrammable sans USB/série : `app0`
(ota_0, 4,5 Mo) + `app1` (ota_1, 4,5 Mo) + `spiffs` (6,8 Mo) + nvs/otadata/
coredump, sur 16 Mo. Deux slots app seulement. Le firmware tourne depuis app1 ;
le seul slot inactif est **app0**, qui porte aujourd'hui le firmware BTT.
`esp_ota_begin()` **efface** la partition cible avant d'écrire → une OTA écrase
app0 (BTT). Il n'existe pas de 3ᵉ slot ; repartitionner exigerait un flash série.

## Invariants de sûreté (le cœur — non négociables)

1. **Jamais effacer app0 (BTT) tant qu'une sauvegarde VÉRIFIÉE de BTT n'existe
   pas** dans spiffs (magic + taille + SHA-256 relus et confirmés). L'endpoint
   d'OTA REFUSE le premier écrasement de BTT sans backup valide.
2. **Jamais `esp_ota_set_boot_partition()` sur une image non validée** :
   `esp_ota_end()` doit réussir (valide l'en-tête + le hash de l'image ESP) ;
   taille ≤ taille de partition ; magic 0xE9 en tête.
3. **Rollback = `rescue.c` inchangé** : après OTA + reboot dans le nouveau slot,
   le compteur de boot (RTC) bascule sur l'AUTRE slot après `RESCUE_DEMARRAGES_MAX`
   boots ratés ; `rescue_reset_boot_count()` sur GOT_IP confirme la viabilité.
   Le slot « précédent » (ancienne version de nous, ou BTT au tout début) reste
   donc le filet. **On n'active PAS le rollback IDF natif** (redondant avec rescue.c).
4. **app1 (le firmware courant) n'est jamais la cible d'une écriture** tant qu'il
   tourne — l'OTA cible toujours `esp_ota_get_next_update_partition()` (= le slot
   inactif). Le premier écrasement touche app0 ; les suivants alternent A/B.
5. `/revert` reste disponible (dernier recours logiciel).

## Architecture

Tout se greffe sur le serveur httpd existant (`web.c`), même style que
`/status` / `/revert`. Delivery = **upload HTTP** (le navigateur/curl POSTe le
`.bin`), aucun serveur externe requis — cohérent avec le flux actuel.

### 1. Sauvegarde BTT → spiffs (échafaudage, avant le 1er écrasement)
- `POST /backup-btt` : lit app0 par blocs (`esp_partition_read`) et l'écrit BRUT
  dans la partition spiffs (pas de système de fichiers — stockage brut : un
  petit en-tête {magic, taille, SHA-256} puis l'image), puis RELIT et vérifie le
  SHA-256. Rend un statut clair (OK + SHA, ou échec).
- `GET /backup-btt` : sert une page avec le bouton + l'état de la sauvegarde
  (présente/valide/absente), comme la page `/revert`.

### 2. Réception + vérification SANS commit (dry-run, testable à zéro risque)
- `POST /ota?dry_run=1` : reçoit le `.bin` en flux, vérifie taille + magic +
  SHA-256 (fourni en query/header par l'appelant, ou calculé et renvoyé), **sans
  jamais appeler `esp_ota_begin`** (donc sans toucher app0). Prouve « on reçoit
  et on valide une image » à zéro risque. Écrit dans un scratch (ou calcule le
  hash à la volée sans stocker).

### 3. OTA réel (commit — le pas irréversible, gardé)
- `POST /ota` : gate de sûreté (invariant 1 : si la cible est app0/BTT et qu'il
  n'y a pas de backup BTT valide → **refuse** avec un message clair). Sinon :
  `esp_ota_get_next_update_partition()` → `esp_ota_begin` → `esp_ota_write` en
  flux → `esp_ota_end` (validation) → si OK `esp_ota_set_boot_partition` →
  `rescue_arm(...)` (armer le filet) → `esp_restart`. Le style d'exécution
  (tâche dédiée, jamais sur la pile httpd) suit le motif de `rescue_switch_now()`.
- `GET /ota` : page d'upload (formulaire multipart) + l'état (slot courant,
  version courante via `esp_app_get_description()`, présence backup BTT).

### 4. Restauration BTT (échafaudage, insurance)
- `POST /restore-btt` : relit la sauvegarde spiffs, la réécrit dans le slot
  inactif via le même chemin OTA, set_boot, reboot → on retrouve BTT. Utile si
  un jour tu veux revenir. Nettoyable plus tard.

### UI (minimale en v1)
Le stub **Updater** de l'accueil→Configuration devient un écran d'ÉTAT (lecture
seule) : slot courant, version, présence de la sauvegarde BTT, et un texte
« Mise à jour via /ota (navigateur) ». L'upload lui-même reste HTTP (un `.bin`
ne se choisit pas à l'écran tactile). Pas de déclenchement d'écrasement depuis
l'écran en v1.

## Ordre de réalisation (le risqué en dernier, gardé)

1. **`/status` enrichi + page d'état** : version courante, slot, place — zéro risque.
2. **Sauvegarde BTT → spiffs + vérif SHA** (`/backup-btt`) — lit app0, écrit
   spiffs ; n'écrase RIEN d'exécutable. Testable seul.
3. **Dry-run `/ota?dry_run=1`** : réception + vérif sans `esp_ota_begin` — zéro risque.
4. **Commit `/ota`** avec la gate (refus si pas de backup BTT), rollback via
   rescue.c. C'est le SEUL pas qui écrase app0 ; déclenché explicitement.
5. **`/restore-btt`** (insurance) + écran d'état Updater.
6. (Plus tard, « une fois validé on pourra nettoyer ») : retrait des échafaudages
   backup/restore/dry-run si tu ne les veux plus, récupération de spiffs.

## Hors périmètre (v1)
- Repartitionnement (impossible sans série).
- Pull automatique depuis une URL / vérif de version en ligne (upload manuel v1).
- Signature d'image (au-delà du SHA-256 fourni) — le firmware d'origine ne
  signe pas ; on reste sur intégrité (SHA) + validation `esp_ota_end`, pas
  authenticité cryptographique.

## Contraintes globales
- Réutilise `rescue.c` (ne pas ré-implémenter le rollback). Réutilise le httpd
  de `web.c`. Aucune écriture flash sur la pile httpd/timer (tâche dédiée).
- Le chemin `/log` + `/revert` + le serveur HTTP doivent survivre à tout (ce
  sont les voies de secours) — ne rien casser dans `web.c` qui les porte.
- Tests : le gros est du flash réel (non testable en host). Les FONCTIONS pures
  (parse d'en-tête d'image, vérif taille/magic, format de l'en-tête de backup,
  calcul SHA sur un buffer) sont extraites et testées sur hôte. Le reste est
  validé au matériel, étape par étape, dans l'ordre ci-dessus (le risqué en dernier).
