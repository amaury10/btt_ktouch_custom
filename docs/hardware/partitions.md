*Cette page est également disponible en [anglais](partitions.en.md).*

# Partitionnement de la flash K-Touch

## Vue d'ensemble

La K-Touch dispose d'une flash de 16 Mio (0x1000000 octets) partitionnée selon le schéma suivant, identique à celui du firmware officiel BIGTREETECH v1.1.0 (`K-Touch_v1.1.0_partition.bin`).

## Table de partitions

| Partition | Type | Sous-type | Offset | Taille | Fins |
|-----------|------|-----------|--------|--------|------|
| nvs | data | nvs | 0x9000 | 0x5000 (20 Kio) | Stockage clé-valeur NVS |
| otadata | data | ota | 0xE000 | 0x2000 (8 Kio) | Métadonnées OTA |
| app0 | app | ota_0 | 0x10000 | 0x480000 (4608 Kio) | Slot OTA 0 (firmware actif ou passif) |
| app1 | app | ota_1 | 0x490000 | 0x480000 (4608 Kio) | Slot OTA 1 (firmware actif ou passif) |
| spiffs | data | spiffs | 0x910000 | 0x6E0000 (7040 Kio) | Système de fichiers |
| coredump | data | coredump | 0xFF0000 | 0x10000 (64 Kio) | Vidage de cœur (crash) |

## Mécanisme OTA (Over-The-Air)

La K-Touch supporte deux slots d'application (`app0` et `app1`), permettant une mise à jour sans interruption de service. Les métadonnées de sélection sont stockées dans la partition `otadata`.

### Structure de l'otadata

La partition `otadata` (8 Kio à l'adresse 0xE000) contient **deux copies indépendantes** de 32 octets, une au début de chaque secteur de 4 Kio — donc aux adresses absolues **0xE000** et **0xF000**, et non aux octets 0-31 et 32-63 d'un seul bloc. Ces deux copies ne sont normalement **pas** identiques : c'est justement la différence d'`ota_seq` entre elles qui porte tout le mécanisme de sélection.

Disposition d'une entrée (32 octets) :

- **Octets 0-3** : `ota_seq` — nombre de séquence (incrémenté à chaque mise à jour)
- **Octets 4-23** : `seq_label` — étiquette de séquence (20 octets, généralement 0xFF)
- **Octets 24-27** : `ota_state` — état du slot
  - `0x00000002` (VALID) : slot valide et peut être bootable
  - `0x00000003` (INVALID) : slot invalide, à ignorer
  - `0x00000004` (ABORTED) : mise à jour avortée, à ignorer
  - `0xFFFFFFFF` (UNDEFINED) : non initialisé
  - à noter : `0x00000001` est PENDING_VERIFY, pas VALID — à ne pas confondre
- **Octets 28-31** : `crc` — CRC-32-LE calculé **uniquement sur les 4 octets d'`ota_seq`**, pas sur l'entrée entière

Le bootloader retient, parmi les entrées dont le CRC est correct, celle qui a le `ota_seq` le plus élevé, et démarre le slot **`(ota_seq - 1) % 2`**.

Lorsqu'une mise à jour OTA arrive :
1. Le nouveau firmware est écrit dans le slot inactif, et l'updater écrit une nouvelle entrée `otadata` avec `ota_state = NEW` (0x0, si le rollback automatique est activé) ou `UNDEFINED` (0xFFFFFFFF, si le rollback est désactivé), avec le `ota_seq` le plus élevé des deux copies + 1.
2. Au redémarrage, c'est le **bootloader** qui effectue la transition NEW → PENDING_VERIFY avant de sauter dans le nouveau slot.
3. Une fois démarrée, c'est l'**application** elle-même qui doit s'auto-valider en appelant `esp_ota_mark_app_valid_cancel_rollback()` ; c'est cet appel qui fait passer l'état à VALID.
4. Si l'application ne s'auto-valide pas (crash, watchdog, appel jamais atteint) avant le prochain redémarrage, le bootloader considère la tentative comme un échec, revient au slot précédent et marque le nouveau comme ABORTED.

**Si aucune entrée n'est valide, le démarrage n'échoue pas.** Le bootloader se rabat sur la partition `factory` ; la K-Touch n'en ayant pas, il démarre alors le premier slot OTA — c'est-à-dire le firmware d'origine dans `app0`. C'est le chemin de secours du projet : **effacer l'`otadata` ramène au firmware stock, ça ne brique jamais l'appareil.**

## Règles de sécurité critiques

Les adresses suivantes **ne doivent jamais être écrites** sans une vérification minutieuse :

- **0x0** — bootloader principal (ESP-IDF)
- **0x8000** — table de partitions
- **0x10000** — slot OTA 0 (peut être l'application active)

Toute erreur d'écriture à ces adresses rend l'appareil inutilisable.

## Vérification d'une sauvegarde — si vous en avez une

> **Sur l'appareil de développement de ce projet, aucune sauvegarde n'est
> possible.** Son port USB-C est inexploitable, donc `esptool` est hors jeu et le
> flash ne peut pas être lu. La réversibilité repose entièrement sur des
> mécanismes embarqués dans le firmware — voir
> [`flashing.md`](flashing.md), à lire avant toute manipulation.
>
> Cette section vaut donc pour qui **dispose** d'un accès série. Si c'est votre
> cas, prenez la sauvegarde : c'est un filet strictement supérieur à tout ce que
> le firmware peut offrir, puisqu'il permet une restauration octet par octet.

Avec un accès série, la sauvegarde se prend par `esptool` puis se vérifie ainsi :

```bash
python ktouch-cli.py verify <sauvegarde.bin>
```

(à lancer depuis la racine du dépôt — le lanceur `ktouch-cli.py` rend le paquet `tools/ktouch` accessible sans manipuler `PYTHONPATH`. Le tiret dans son nom est délibéré : il rend le fichier non importable comme module Python, ce qui empêche structurellement qu'il masque le paquet `tools/ktouch/`.)

Cette commande :
1. Vérifie que la taille est exactement 16 Mio
2. Valide que la table de partitions correspond exactement au schéma officiel ci-dessus
3. Rapporte le contenu des deux slots OTA (versions de firmware, dates, versions IDF)
4. Indique quel slot est actuellement actif
5. Affiche un verdict : **« Sauvegarde exploitable : OUI »** ou **« NON — ne pas reprogrammer »**

La sauvegarde ne doit être utilisée pour la restauration que si le rapport indique `safe_to_flash = True`.

## Références

- Partition officielle : `K-Touch_v1.1.0_partition.bin` du dépôt BIGTREETECH
- Documentation ESP-IDF : [OTA Updates](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/ota.html)
